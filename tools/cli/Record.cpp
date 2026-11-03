#include "Record.h"

#include "util/ImageIO.h"

#include "core/IRecorder.h"

#include <windows.h>

#include <dxgi1_6.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <winrt/base.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <format>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

const char* stateName(elv::RecorderState s) {
    switch (s) {
    case elv::RecorderState::Idle: return "Idle";
    case elv::RecorderState::Starting: return "Starting";
    case elv::RecorderState::Recording: return "Recording";
    case elv::RecorderState::Paused: return "Paused";
    case elv::RecorderState::Stopping: return "Stopping";
    case elv::RecorderState::Error: return "Error";
    }
    return "?";
}

const char* reasonName(elv::StopReason r) {
    switch (r) {
    case elv::StopReason::User: return "user";
    case elv::StopReason::SourceClosed: return "source closed";
    case elv::StopReason::Error: return "error";
    }
    return "?";
}

// Collects recorder events and lets the main thread wait for them.
class EventLog final : public elv::IRecorderListener {
public:
    explicit EventLog(bool verbose) : verbose_(verbose) {}

    void onStateChanged(elv::RecorderState state) override {
        std::lock_guard lock(mutex_);
        state_ = state;
        if (verbose_) std::printf("[state] %s\n", stateName(state));
        cv_.notify_all();
    }
    void onStarted(const elv::RecordingInfo& info) override {
        std::lock_guard lock(mutex_);
        info_ = info;
        if (verbose_)
            std::printf("[started] %ux%u @ %u fps, %s (%s) on %s\n          -> %s\n", info.width, info.height,
                        info.fps, winrt::to_string(info.encoderName).c_str(),
                        info.hardwareEncoder ? "hardware" : "software",
                        winrt::to_string(info.adapterName).c_str(), winrt::to_string(info.path.wstring()).c_str());
    }
    void onStats(const elv::RecorderStats& stats) override {
        std::lock_guard lock(mutex_);
        stats_ = stats;
    }
    void onFinished(const elv::RecordingInfo&, const elv::RecorderStats& stats, elv::StopReason reason) override {
        std::lock_guard lock(mutex_);
        stats_ = stats;
        reason_ = reason;
        ++finishedCount_;
        if (verbose_)
            std::printf("[finished] reason=%s, %.2fs recorded, %llu frames\n", reasonName(reason),
                        stats.elapsed.count() / 1000.0, static_cast<unsigned long long>(stats.framesEncoded));
        cv_.notify_all();
    }
    void onReplaySaved(const std::filesystem::path& path, std::chrono::milliseconds length) override {
        std::printf("[replay saved] %.2fs -> %s\n", length.count() / 1000.0, winrt::to_string(path.wstring()).c_str());
    }
    void onWarning(const std::string& message) override { std::fprintf(stderr, "[warning] %s\n", message.c_str()); }
    void onError(const std::string& message) override {
        std::lock_guard lock(mutex_);
        error_ = message;
        std::fprintf(stderr, "[error] %s\n", message.c_str());
        cv_.notify_all();
    }

    bool waitForState(std::initializer_list<elv::RecorderState> states, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] {
            return std::find(states.begin(), states.end(), state_) != states.end();
        });
    }

    elv::RecorderState state() { std::lock_guard lock(mutex_); return state_; }
    elv::RecorderStats stats() { std::lock_guard lock(mutex_); return stats_; }
    elv::StopReason reason() { std::lock_guard lock(mutex_); return reason_; }
    int finishedCount() { std::lock_guard lock(mutex_); return finishedCount_; }
    std::string error() { std::lock_guard lock(mutex_); return error_; }

private:
    const bool verbose_;
    std::mutex mutex_;
    std::condition_variable cv_;
    elv::RecorderState state_ = elv::RecorderState::Idle;
    elv::RecordingInfo info_;
    elv::RecorderStats stats_;
    elv::StopReason reason_ = elv::StopReason::User;
    int finishedCount_ = 0;
    std::string error_;
};

// Process CPU usage as a percentage of all logical cores since the last call.
class CpuMeter {
public:
    CpuMeter() { sample(); }

    double percent() {
        const auto [busy, wall] = sample();
        const auto busyDelta = busy - lastBusy_;
        const auto wallDelta = wall - lastWall_;
        lastBusy_ = busy;
        lastWall_ = wall;
        return wallDelta > 0 ? 100.0 * busyDelta / (wallDelta * cores_) : 0.0;
    }

private:
    std::pair<std::uint64_t, std::uint64_t> sample() {
        FILETIME creation, exit, kernel, user, now;
        GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
        GetSystemTimeAsFileTime(&now);
        const auto toU64 = [](FILETIME t) { return (std::uint64_t{t.dwHighDateTime} << 32) | t.dwLowDateTime; };
        const auto busy = toU64(kernel) + toU64(user);
        if (lastWall_ == 0) {
            lastBusy_ = busy;
            lastWall_ = toU64(now);
        }
        return {busy, toU64(now)};
    }

    std::uint64_t lastBusy_ = 0;
    std::uint64_t lastWall_ = 0;
    unsigned cores_ = std::max(1u, std::thread::hardware_concurrency());
};

struct ProcessMetrics {
    double privateMb = 0;
    DWORD handles = 0;
    int threads = 0;
    double gpuMb = 0;  // this process's video memory on all adapters
};

ProcessMetrics sampleMetrics() {
    ProcessMetrics m;

    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    m.privateMb = counters.PrivateUsage / (1024.0 * 1024.0);

    GetProcessHandleCount(GetCurrentProcess(), &m.handles);

    const DWORD pid = GetCurrentProcessId();
    if (HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{sizeof(entry)};
        for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry))
            if (entry.th32OwnerProcessID == pid) ++m.threads;
        CloseHandle(snapshot);
    }

    winrt::com_ptr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(factory.put())))) {
        winrt::com_ptr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, adapter.put()) != DXGI_ERROR_NOT_FOUND; ++i) {
            if (const auto adapter3 = adapter.try_as<IDXGIAdapter3>()) {
                DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonLocal{};
                adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
                adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal);
                m.gpuMb += (local.CurrentUsage + nonLocal.CurrentUsage) / (1024.0 * 1024.0);
            }
            adapter = nullptr;
        }
    }
    return m;
}

void printMetrics(const char* label, const ProcessMetrics& m) {
    std::printf("%-10s private=%7.1fMB  handles=%5lu  threads=%3d  gpu=%7.1fMB\n", label, m.privateMb, m.handles,
                m.threads, m.gpuMb);
}

// Sleeps until `deadline`, waking early if `stop` returns true.
void sleepUntil(std::chrono::steady_clock::time_point deadline, const std::function<bool()>& stop) {
    while (!stop() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(20ms);
}

}  // namespace

std::optional<RecordArgs> parseRecordArgs(int argc, wchar_t** argv) {
    RecordArgs args;
    std::vector<std::wstring_view> positional;
    for (int i = 2; i < argc; ++i) {
        const std::wstring_view a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (a == L"--fps" && hasValue) args.video.fps = std::stoi(argv[++i]);
        else if (a == L"--bitrate" && hasValue) args.video.bitrateKbps = std::stoi(argv[++i]);
        else if (a == L"--pause-at" && hasValue) args.pauseAt = std::stoi(argv[++i]);
        else if (a == L"--resume-at" && hasValue) args.resumeAt = std::stoi(argv[++i]);
        else if (a == L"--software") args.video.preferHardware = false;
        else if (a == L"--mp4") args.container = elv::Container::Mp4;
        else if (a == L"--audio" && hasValue) {
            const std::wstring_view v = argv[++i];
            args.audio.captureSystem = v == L"system" || v == L"both";
            args.audio.captureMicrophone = v == L"mic" || v == L"both";
            if (!args.audio.enabled() && v != L"none") return std::nullopt;
        }
        else if (a == L"--app-audio") args.appAudio = true;
        else if (a == L"--replay" && hasValue) args.replaySeconds = std::stod(argv[++i]);
        else if (a == L"--save-at" && hasValue) {
            std::wstring list = argv[++i];
            for (std::size_t pos = 0; pos < list.size();) {
                const auto comma = list.find(L',', pos);
                args.saveAt.push_back(std::stoi(list.substr(pos, comma - pos)));
                pos = comma == std::wstring::npos ? list.size() : comma + 1;
            }
        }
        else if (a == L"--webcam" && hasValue) {
            // device id from `elvideo-cli webcams`, "default" (first camera) or "test" (pattern)
            elv::WebcamOverlay w;
            const std::wstring_view v = argv[++i];
            w.deviceId = v == L"default" ? L"" : std::wstring(v);
            args.webcam = w;
        }
        else if (a == L"--system-device" && hasValue) args.audio.systemDeviceId = argv[++i];
        else if (a == L"--mic-device" && hasValue) args.audio.microphoneDeviceId = argv[++i];
        else if (a == L"--preview" && hasValue) args.previewPng = argv[++i];
        else if (a == L"--codec" && hasValue) {
            const std::wstring_view v = argv[++i];
            if (v == L"h264") args.video.codec = elv::VideoCodec::H264;
            else if (v == L"hevc") args.video.codec = elv::VideoCodec::HEVC;
            else if (v == L"av1") args.video.codec = elv::VideoCodec::AV1;
            else return std::nullopt;
        }
        else if (a == L"--region" && hasValue) {
            // x,y,width,height in physical pixels of the source
            elv::CaptureRegion r;
            if (swscanf_s(argv[++i], L"%d,%d,%d,%d", &r.x, &r.y, &r.width, &r.height) != 4) return std::nullopt;
            args.region = r;
        }
        else if (a.starts_with(L"--")) return std::nullopt;
        else positional.push_back(a);
    }
    if (positional.empty()) return std::nullopt;
    args.source = positional[0];
    if (positional.size() > 1) args.seconds = std::stoi(std::wstring(positional[1]));
    if (positional.size() > 2) args.output = positional[2];
    return args;
}

int runRecord(const RecordArgs& args, const elv::CaptureSource& source) {
    elv::RecordSettings settings;
    settings.source = source;
    settings.video = args.video;
    settings.container = args.container;
    settings.audio = args.audio;
    if (args.appAudio && source.kind == elv::SourceKind::Window) {
        DWORD pid = 0;
        GetWindowThreadProcessId(reinterpret_cast<HWND>(source.handle), &pid);
        settings.audio.captureSystem = true;
        settings.audio.systemProcessId = pid;
        std::printf("[app audio] process %lu\n", pid);
    }
    settings.region = args.region;
    settings.webcam = args.webcam;
    settings.replaySeconds = args.replaySeconds;
    settings.outputPath = std::filesystem::absolute(args.output);

    EventLog log(true);
    const auto recorder = elv::createRecorder();
    recorder->setListener(&log);

    if (!recorder->start(settings)) return 1;
    if (!log.waitForState({elv::RecorderState::Recording, elv::RecorderState::Error}, 10s) ||
        log.state() != elv::RecorderState::Recording) {
        recorder->setListener(nullptr);
        return 1;
    }

    const auto active = [&] {
        const auto s = log.state();
        return s == elv::RecorderState::Recording || s == elv::RecorderState::Paused;
    };
    CpuMeter cpu;
    double cpuTotal = 0;
    double cpuMax = 0;
    elv::RecorderStats previous;
    int elapsed = 0;
    const auto startTime = std::chrono::steady_clock::now();
    while (elapsed < args.seconds && active()) {
        // Sleep to the next whole second of wall time so the run length does not drift.
        sleepUntil(startTime + std::chrono::seconds(elapsed + 1), [&] { return !active(); });
        ++elapsed;
        if (elapsed == args.pauseAt) recorder->pause();
        if (std::find(args.saveAt.begin(), args.saveAt.end(), elapsed) != args.saveAt.end()) {
            auto path = settings.outputPath;
            path.replace_filename(path.stem().wstring() + std::format(L"_replay{}.mp4", elapsed));
            recorder->saveReplay(path);
        }
        if (!args.previewPng.empty() && elapsed == std::max(1, args.seconds / 2)) {
            const auto t0 = std::chrono::steady_clock::now();
            if (const auto p = recorder->preview(480, 270)) {
                elv::savePixelsAsPng(p->bgra.data(), p->width, p->height, p->width * 4, args.previewPng);
                std::printf("[preview] %ux%u in %.1f ms -> %s\n", p->width, p->height,
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                            winrt::to_string(args.previewPng.wstring()).c_str());
            } else {
                std::printf("[preview] none\n");
            }
        }
        if (elapsed == args.resumeAt) recorder->resume();

        const auto s = log.stats();
        const double load = cpu.percent();
        cpuTotal += load;
        cpuMax = std::max(cpuMax, load);
        // Audio peak of the last stats interval, in dBFS ("-" when not recorded).
        const auto level = [](float peak) {
            return peak < 0 ? std::string("   -") : std::format("{:4.0f}", peak > 0 ? 20 * std::log10(peak) : -99.0);
        };
        std::printf("t=%4ds  %-9s rec=%7.2fs  encoded=%3llu  dup=%3llu  dropped=%llu  sys=%sdB mic=%sdB  cpu=%.1f%%  "
                    "mem=%.1fMB\n",
                    elapsed, stateName(log.state()), s.elapsed.count() / 1000.0,
                    static_cast<unsigned long long>(s.framesEncoded - previous.framesEncoded),
                    static_cast<unsigned long long>(s.framesDuplicated - previous.framesDuplicated),
                    static_cast<unsigned long long>(s.framesDropped), level(s.systemPeak).c_str(),
                    level(s.microphonePeak).c_str(), load, sampleMetrics().privateMb);
        previous = s;
    }

    recorder->stop();
    log.waitForState({elv::RecorderState::Idle, elv::RecorderState::Error}, 15s);
    recorder->setListener(nullptr);

    const auto s = log.stats();
    std::printf("done: %ds wall, %.2fs recorded, encoded %llu, duplicated %llu, dropped %llu, cpu avg %.1f%% max %.1f%%, "
                "reason %s\n",
                elapsed, s.elapsed.count() / 1000.0, static_cast<unsigned long long>(s.framesEncoded),
                static_cast<unsigned long long>(s.framesDuplicated), static_cast<unsigned long long>(s.framesDropped),
                elapsed ? cpuTotal / elapsed : 0.0, cpuMax, reasonName(log.reason()));
    return log.state() == elv::RecorderState::Idle ? 0 : 1;
}

int runSelfTest(const elv::CaptureSource& source, const std::filesystem::path& outDir) {
    std::filesystem::create_directories(outDir);
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++failures;
    };
    const auto settingsFor = [&](const wchar_t* name) {
        elv::RecordSettings s;
        s.source = source;
        s.video.fps = 30;
        s.outputPath = std::filesystem::absolute(outDir / name);
        return s;
    };
    using S = elv::RecorderState;

    EventLog log(false);
    auto recorder = elv::createRecorder();
    recorder->setListener(&log);

    // 1. A bad path fails the start and leaves the recorder usable.
    {
        auto s = settingsFor(L"x.mp4");
        s.outputPath = L"relative.mp4";
        check(recorder->start(s), "start with relative path is accepted");
        check(log.waitForState({S::Error}, 5s) && !log.error().empty(), "relative path -> Error with message");
    }

    // 2. Recovery from Error, duplicate commands are rejected.
    {
        const auto s = settingsFor(L"normal.mp4");
        check(recorder->start(s), "start from Error is accepted");
        check(!recorder->start(s), "second start is rejected");
        check(log.waitForState({S::Recording}, 10s), "reaches Recording");
        std::this_thread::sleep_for(500ms);
        check(recorder->pause() && !recorder->pause(), "pause accepted once");
        check(log.waitForState({S::Paused}, 5s), "reaches Paused");
        check(!recorder->resume() || log.waitForState({S::Recording}, 5s), "resume reaches Recording");
        std::this_thread::sleep_for(500ms);
        const int finished = log.finishedCount();
        check(recorder->stop() && !recorder->stop(), "stop accepted once");
        check(log.waitForState({S::Idle}, 10s) && log.finishedCount() == finished + 1 &&
                  log.reason() == elv::StopReason::User && log.stats().framesEncoded > 0,
              "stops to Idle with a finished file");
        check(std::filesystem::file_size(s.outputPath) > 0, "file exists and is not empty");
    }

    // 3. Stop requested while still Starting.
    {
        const auto s = settingsFor(L"early_stop.mp4");
        check(recorder->start(s) && recorder->stop(), "stop accepted during Starting");
        check(log.waitForState({S::Idle}, 10s), "ends Idle");
    }

#ifndef NDEBUG
    // 4. Encoder failure mid-recording -> Error, file finalized, then recovery.
    {
        SetEnvironmentVariableW(L"ELV_TEST_FAIL_AFTER_FRAMES", L"30");
        const auto s = settingsFor(L"failure.mp4");
        const int finished = log.finishedCount();
        check(recorder->start(s), "start with simulated failure");
        check(log.waitForState({S::Error}, 10s), "simulated device removal -> Error");
        check(log.finishedCount() == finished + 1 && log.reason() == elv::StopReason::Error &&
                  log.stats().framesEncoded == 30,
              "partial file finalized with 30 frames");
        SetEnvironmentVariableW(L"ELV_TEST_FAIL_AFTER_FRAMES", nullptr);

        const auto again = settingsFor(L"after_failure.mp4");
        check(recorder->start(again) && log.waitForState({S::Recording}, 10s), "records again after failure");
        std::this_thread::sleep_for(500ms);
        recorder->stop();
        check(log.waitForState({S::Idle}, 10s), "and stops cleanly");
    }
#else
    std::printf("SKIP  failure injection (debug builds only)\n");
#endif

    // 5. Destroying the recorder while recording finalizes the file.
    {
        const auto s = settingsFor(L"destroyed.mp4");
        check(recorder->start(s) && log.waitForState({S::Recording}, 10s), "recording before destroy");
        std::this_thread::sleep_for(500ms);
        const int finished = log.finishedCount();
        recorder.reset();
        check(log.finishedCount() == finished + 1 && log.stats().framesEncoded > 0, "destroy finalizes the file");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}

int runStress(const elv::CaptureSource& source, int cycles, double secondsPerCycle,
              const std::filesystem::path& outDir) {
    std::filesystem::create_directories(outDir);
    const auto half = std::chrono::milliseconds(static_cast<int>(secondsPerCycle * 500));

    EventLog log(false);
    const auto recorder = elv::createRecorder();
    recorder->setListener(&log);

    printMetrics("before", sampleMetrics());
    ProcessMetrics baseline;
    int failures = 0;

    for (int cycle = 1; cycle <= cycles; ++cycle) {
        elv::RecordSettings settings;
        settings.source = source;
        settings.outputPath = std::filesystem::absolute(outDir / std::format("cycle_{:03}.mp4", cycle));

        const int finishedBefore = log.finishedCount();
        const bool ok =
            recorder->start(settings) &&
            log.waitForState({elv::RecorderState::Recording, elv::RecorderState::Error}, 10s) &&
            log.state() == elv::RecorderState::Recording &&
            (std::this_thread::sleep_for(half), recorder->pause()) &&
            log.waitForState({elv::RecorderState::Paused}, 5s) &&
            (std::this_thread::sleep_for(200ms), recorder->resume()) &&
            log.waitForState({elv::RecorderState::Recording}, 5s) &&
            (std::this_thread::sleep_for(half), recorder->stop()) &&
            log.waitForState({elv::RecorderState::Idle, elv::RecorderState::Error}, 15s) &&
            log.state() == elv::RecorderState::Idle && log.finishedCount() == finishedBefore + 1 &&
            log.stats().framesEncoded > 0;

        if (!ok) {
            ++failures;
            std::printf("cycle %d FAILED (state %s, error '%s')\n", cycle, stateName(log.state()), log.error().c_str());
            recorder->stop();
            log.waitForState({elv::RecorderState::Idle, elv::RecorderState::Error}, 15s);
        }

        std::error_code ec;
        std::filesystem::remove(settings.outputPath, ec);

        // The first cycle loads DLLs and driver state; measure growth after it.
        const auto m = sampleMetrics();
        if (cycle == 1) {
            baseline = m;
            printMetrics("cycle 1", m);
        } else if (cycle % 10 == 0 || cycle == cycles) {
            printMetrics(std::format("cycle {}", cycle).c_str(), m);
        }
    }
    recorder->setListener(nullptr);

    const auto last = sampleMetrics();
    std::printf("growth after cycle 1: private %+.1fMB, handles %+ld, threads %+d, gpu %+.1fMB; failures %d/%d\n",
                last.privateMb - baseline.privateMb, static_cast<long>(last.handles) - static_cast<long>(baseline.handles),
                last.threads - baseline.threads, last.gpuMb - baseline.gpuMb, failures, cycles);
    return failures == 0 ? 0 : 1;
}
