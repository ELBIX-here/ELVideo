// Engine-only test harness.
//   elvideo-cli info
//   elvideo-cli list
//   elvideo-cli capture <source> [seconds=10] [outdir=captures]
//     <source> is m<N> / w<N> (index from `list`) or a hex HWND such as 0x1A2B3C.
//     Saves a PNG every second and on size changes; stops early if the source closes.
//   elvideo-cli record <source> [seconds=10] [out=recording.mp4] [--fps N] [--bitrate KBPS] [--software] [--mp4]
//                      [--pause-at S --resume-at S]
//                      [--audio none|system|mic|both] [--system-device ID] [--mic-device ID]
//                      [--region X,Y,W,H] [--codec h264|hevc|av1] [--preview frame.png] [--app-audio]
//                      [--webcam default|test|ID] [--replay SECONDS --save-at S1,S2]
//     Records through IRecorder (fragmented MP4 unless --mp4) and prints per-second stats with CPU usage.
//   elvideo-cli stress <source> [cycles=30] [seconds=1] [outdir=stress]
//     Repeats start/pause/resume/stop and reports memory, handle, thread and GPU memory growth.
//   elvideo-cli probe <file> [frame.png]
//     Reads a recording back: frame count, timestamp gaps, bitrate; optionally decodes the middle frame.
//   elvideo-cli audio
//     Lists output (system sound) and input (microphone) devices with their ids.
//   elvideo-cli synccheck <file>
//     A/V offset of a recording made with scripts/test/sync_test.ps1 (flashes + beeps).
//   elvideo-cli dumpinfo <file.dmp>
//     Exception, faulting module and thread count of an app crash dump.
//   elvideo-cli thumb <source|all> [outdir=thumbs]
//     Saves 320x180 thumbnails through captureThumbnail() and prints how long each took.

#include "DumpInfo.h"
#include "LeakCheck.h"
#include "Probe.h"
#include "Record.h"
#include "capture/WgcCapture.h"
#include "core/IRecorder.h"
#include "util/D3DDevice.h"
#include "util/ImageIO.h"

#include <windows.h>

#include <winrt/base.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <format>
#include <vector>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;

int runInfo() {
    const auto info = elv::queryPlatform();
    std::printf("ELVideo engine %s\n", elv::version());
    std::printf("Windows build       : %s\n", info.windowsBuild.c_str());
    std::printf("WGC supported       : %s\n", info.captureSupported ? "yes" : "no");
    std::printf("Borderless capture  : %s\n", info.borderlessCaptureSupported ? "yes" : "no");
    std::printf("Video encoders      :\n");
    for (const auto& e : elv::enumerateVideoEncoders())
        std::printf("  %-6s %-8s %s\n", elv::codecName(e.codec), e.hardware ? "hardware" : "software",
                    winrt::to_string(e.name).c_str());
    // A real short encode per codec on each monitor's GPU.
    for (const auto& s : elv::enumerateCaptureSources()) {
        if (s.kind != elv::SourceKind::Monitor) continue;
        std::printf("Encode test on %s:\n", winrt::to_string(s.title).c_str());
        for (const auto codec : {elv::VideoCodec::H264, elv::VideoCodec::HEVC, elv::VideoCodec::AV1}) {
            const auto p = elv::probeVideoEncoder(codec, s);
            std::printf("  %-6s %s\n", elv::codecName(codec),
                        p.supported ? std::format("{} ({})", winrt::to_string(p.encoderName),
                                                  p.hardware ? "hardware" : "software").c_str()
                                    : ("unsupported: " + p.error).c_str());
        }
    }
    return info.captureSupported ? 0 : 1;
}

int runList() {
    int monitorIndex = 0;
    int windowIndex = 0;
    for (const auto& s : elv::enumerateCaptureSources()) {
        const bool monitor = s.kind == elv::SourceKind::Monitor;
        const auto id = std::format("{}{}", monitor ? 'm' : 'w', monitor ? monitorIndex++ : windowIndex++);
        std::printf("%-4s 0x%-10llX %5dx%-5d %s%s%s\n", id.c_str(), static_cast<unsigned long long>(s.handle),
                    s.width, s.height, winrt::to_string(s.title).c_str(), s.processName.empty() ? "" : "  - ",
                    winrt::to_string(s.processName).c_str());
    }
    return 0;
}

std::optional<elv::CaptureSource> resolveSource(std::wstring_view arg) {
    if (arg.starts_with(L"0x") || arg.starts_with(L"0X")) {
        const auto hwnd = reinterpret_cast<HWND>(std::wcstoull(arg.data(), nullptr, 16));
        if (!IsWindow(hwnd)) return std::nullopt;
        return elv::CaptureSource{elv::SourceKind::Window, reinterpret_cast<std::uintptr_t>(hwnd)};
    }
    if (arg.size() < 2 || (arg[0] != L'm' && arg[0] != L'w')) return std::nullopt;

    const auto kind = arg[0] == L'm' ? elv::SourceKind::Monitor : elv::SourceKind::Window;
    int wanted = std::stoi(std::wstring(arg.substr(1)));
    for (const auto& s : elv::enumerateCaptureSources()) {
        if (s.kind == kind && wanted-- == 0) return s;
    }
    return std::nullopt;
}

int runCapture(std::wstring_view sourceArg, int seconds, const std::filesystem::path& outDir) {
    const auto source = resolveSource(sourceArg);
    if (!source) {
        std::fwprintf(stderr, L"source not found: %.*s\n", static_cast<int>(sourceArg.size()), sourceArg.data());
        return 2;
    }

    elv::D3DDevice device(elv::monitorForSource(*source));
    std::printf("adapter: %s\n", winrt::to_string(device.adapterName()).c_str());
    std::filesystem::create_directories(outDir);

    std::atomic<std::uint64_t> frames = 0;
    std::atomic<std::uint32_t> lastWidth = 0;
    std::atomic<std::uint32_t> lastHeight = 0;
    std::atomic<int> pngCount = 0;
    std::atomic<bool> closed = false;
    auto nextPng = std::chrono::steady_clock::now();

    elv::WgcCapture capture(device, *source);
    capture.start(
        [&](const elv::CapturedFrame& f) {
            ++frames;
            const bool resized = f.width != lastWidth || f.height != lastHeight;
            lastWidth = f.width;
            lastHeight = f.height;

            const auto now = std::chrono::steady_clock::now();
            if (now >= nextPng || resized) {
                const auto path = outDir / std::format("frame_{:03}_{}x{}.png", pngCount++, f.width, f.height);
                elv::saveTextureAsPng(device, f.texture, f.width, f.height, path);
                nextPng = now + 1s;
            }
        },
        [&] { closed = true; });

    std::uint64_t prevFrames = 0;
    for (int s = 0; s < seconds && !closed; ++s) {
        for (int i = 0; i < 10 && !closed; ++i) std::this_thread::sleep_for(100ms);
        const std::uint64_t total = frames;
        std::printf("t=%2ds  fps=%3llu  size=%ux%u\n", s + 1, static_cast<unsigned long long>(total - prevFrames),
                    lastWidth.load(), lastHeight.load());
        prevFrames = total;
    }
    capture.stop();

    std::printf("done: %llu frames, %d png, source %s\n", static_cast<unsigned long long>(frames.load()),
                pngCount.load(), closed ? "closed" : "still open");
    return 0;
}

int runAudioDevices() {
    for (const auto kind : {elv::AudioDeviceKind::Output, elv::AudioDeviceKind::Input}) {
        std::printf("%s:\n", kind == elv::AudioDeviceKind::Output ? "output (system sound)" : "input (microphone)");
        for (const auto& d : elv::enumerateAudioDevices(kind))
            std::printf("  %s %s\n      %s\n", d.isDefault ? "*" : " ", winrt::to_string(d.name).c_str(),
                        winrt::to_string(d.id).c_str());
    }
    return 0;
}

int runThumb(std::wstring_view sourceArg, const std::filesystem::path& outDir) {
    std::vector<elv::CaptureSource> sources;
    if (sourceArg == L"all") {
        sources = elv::enumerateCaptureSources();
    } else if (const auto source = resolveSource(sourceArg)) {
        sources.push_back(*source);
    } else {
        std::fwprintf(stderr, L"source not found: %.*s\n", static_cast<int>(sourceArg.size()), sourceArg.data());
        return 2;
    }
    std::filesystem::create_directories(outDir);
    int index = 0;
    for (const auto& source : sources) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto thumb = elv::captureThumbnail(source, 320, 180);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
        const auto file = outDir / std::format(L"thumb{:02}.png", index++);
        if (thumb) elv::savePixelsAsPng(thumb->bgra.data(), thumb->width, thumb->height, thumb->width * 4, file);
        std::printf("%-50s %4lldms %s\n", winrt::to_string(source.title).substr(0, 50).c_str(),
                    static_cast<long long>(ms.count()),
                    thumb ? std::format("{}x{}", thumb->width, thumb->height).c_str() : "none");
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleOutputCP(CP_UTF8);
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // keep output ordered when redirected
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    const std::wstring_view cmd = argc > 1 ? argv[1] : L"info";
    try {
        if (cmd == L"info") return runInfo();
        if (cmd == L"list") return runList();
        if (cmd == L"capture" && argc > 2) {
            const int seconds = argc > 3 ? std::stoi(argv[3]) : 10;
            const std::filesystem::path outDir = argc > 4 ? argv[4] : L"captures";
            return runCapture(argv[2], seconds, outDir);
        }
        if (cmd == L"record") {
            if (const auto args = parseRecordArgs(argc, argv)) {
                const auto source = resolveSource(args->source);
                if (!source) {
                    std::fwprintf(stderr, L"source not found: %s\n", args->source.c_str());
                    return 2;
                }
                return runRecord(*args, *source);
            }
        }
        if (cmd == L"selftest" && argc > 2) {
            const auto source = resolveSource(argv[2]);
            if (!source) {
                std::fwprintf(stderr, L"source not found: %s\n", argv[2]);
                return 2;
            }
            return runSelfTest(*source, argc > 3 ? argv[3] : L"selftest");
        }
        if (cmd == L"leakcheck" && argc > 2) {
            const auto source = resolveSource(argv[2]);
            if (!source) {
                std::fwprintf(stderr, L"source not found: %s\n", argv[2]);
                return 2;
            }
            return runLeakCheck(*source, argc > 3 ? argv[3] : L"leakcheck", argc > 4 ? std::stoi(argv[4]) : 20);
        }
        if (cmd == L"stress" && argc > 2) {
            const auto source = resolveSource(argv[2]);
            if (!source) {
                std::fwprintf(stderr, L"source not found: %s\n", argv[2]);
                return 2;
            }
            return runStress(*source, argc > 3 ? std::stoi(argv[3]) : 30, argc > 4 ? std::stod(argv[4]) : 1.0,
                             argc > 5 ? argv[5] : L"stress");
        }
        if (cmd == L"probe" && argc > 2) return runProbe(argv[2], argc > 3 ? argv[3] : L"");
        if (cmd == L"synccheck" && argc > 2) return runSyncCheck(argv[2]);
        if (cmd == L"audio") return runAudioDevices();
        if (cmd == L"webcams") {
            for (const auto& w : elv::enumerateWebcams())
                std::printf("%s\n    %s\n", winrt::to_string(w.name).c_str(), winrt::to_string(w.id).c_str());
            return 0;
        }
        if (cmd == L"dumpinfo" && argc > 2) return runDumpInfo(argv[2]);
        if (cmd == L"thumb" && argc > 2) return runThumb(argv[2], argc > 3 ? argv[3] : L"thumbs");
    } catch (const winrt::hresult_error& e) {
        std::fprintf(stderr, "error: %s\n", winrt::to_string(e.message()).c_str());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::fprintf(stderr,
                 "usage: elvideo-cli info | list\n"
                 "       elvideo-cli capture <m0|w3|0xHWND> [seconds] [outdir]\n"
                 "       elvideo-cli record <m0|w3|0xHWND> [seconds] [out.mp4] [--fps N] [--bitrate KBPS] "
                 "[--software] [--mp4] [--pause-at S --resume-at S]\n"
                 "                          [--audio none|system|mic|both] [--system-device ID] [--mic-device ID]\n"
                 "                          [--region X,Y,W,H] [--codec h264|hevc|av1] [--preview frame.png] [--app-audio]\n"
                 "                          [--webcam default|test|ID] [--replay SECONDS --save-at S1,S2]\n"
                 "       elvideo-cli stress <m0|w3|0xHWND> [cycles] [seconds] [outdir]\n"
                 "       elvideo-cli probe <file> [frame.png]\n"
                 "       elvideo-cli thumb <m0|w3|0xHWND|all> [outdir]\n"
                 "       elvideo-cli audio | webcams | synccheck <file> | dumpinfo <file.dmp>\n");
    return 2;
}
