#include "video/FramePacer.h"

#include "util/MasterClock.h"

#include <spdlog/spdlog.h>

namespace elv {

FramePacer::FramePacer(std::uint32_t fpsNumerator, std::uint32_t fpsDenominator)
    : fpsNumerator_(fpsNumerator), fpsDenominator_(fpsDenominator) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    qpcFrequency_ = frequency.QuadPart;

    stopEvent_.attach(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    winrt::check_bool(static_cast<bool>(stopEvent_));
    resumeEvent_.attach(CreateEventW(nullptr, TRUE, TRUE, nullptr));
    winrt::check_bool(static_cast<bool>(resumeEvent_));

    // High-resolution timers (Windows 10 1803+) avoid the ~15.6 ms default granularity.
    timer_.attach(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS));
    if (!timer_) {
        spdlog::warn("high-resolution timer unavailable; frame pacing may jitter");
        timer_.attach(CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS));
    }
    winrt::check_bool(static_cast<bool>(timer_));
}

FramePacer::~FramePacer() { stop(); }

void FramePacer::start(TickHandler onTick) {
    if (thread_.joinable()) return;
    onTick_ = std::move(onTick);
    ResetEvent(stopEvent_.get());
    thread_ = std::thread([this] { run(); });
}

void FramePacer::stop() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_.get());
    thread_.join();
}

void FramePacer::pause() {
    ResetEvent(resumeEvent_.get());
    paused_ = true;
}

void FramePacer::resume() {
    paused_ = false;
    SetEvent(resumeEvent_.get());
}

bool FramePacer::waitUntil(std::int64_t qpcDue) {
    const std::int64_t remaining = qpcDue - MasterClock::qpcNow();
    if (remaining <= 0) return WaitForSingleObject(stopEvent_.get(), 0) != WAIT_OBJECT_0;

    LARGE_INTEGER relative;
    relative.QuadPart = -(remaining * 10'000'000 / qpcFrequency_);  // negative = relative, 100 ns units
    SetWaitableTimer(timer_.get(), &relative, 0, nullptr, nullptr, FALSE);
    const HANDLE handles[] = {stopEvent_.get(), timer_.get()};
    return WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0;
}

void FramePacer::run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    // The timeline starts with the first frame so the file never opens with a black frame.
    while (generation_ == 0) {
        if (WaitForSingleObject(stopEvent_.get(), 1) == WAIT_OBJECT_0) return;
    }

    const std::int64_t ticksPerSecond = qpcFrequency_ * fpsDenominator_;
    const auto timeOf = [&](std::uint64_t i) { return static_cast<std::int64_t>(i) * ticksPerSecond / fpsNumerator_; };
    std::int64_t start = MasterClock::qpcNow();
    std::uint64_t index = 0;
    std::uint64_t lastGeneration = 0;

    for (;;) {
        if (paused_) {
            const HANDLE handles[] = {stopEvent_.get(), resumeEvent_.get()};
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) return;
            // Continue the timeline as if no time had passed; the next frame is due now.
            start = MasterClock::qpcNow() - timeOf(index);
            continue;
        }

        const std::int64_t due = start + timeOf(index);
        if (!waitUntil(due)) return;
        if (paused_) continue;  // paused while waiting: this slot belongs after resume

        const std::int64_t late = MasterClock::qpcNow() - due;
        const auto missed = static_cast<std::uint64_t>(late * fpsNumerator_ / ticksPerSecond);
        if (missed > 0) {
            dropped_ += missed;
            index += missed;
        }

        const std::uint64_t generation = generation_;
        const bool duplicate = generation == lastGeneration;
        lastGeneration = generation;
        if (duplicate) ++duplicated_;
        ++ticks_;

        try {
            onTick_(index, duplicate);
        } catch (const winrt::hresult_error& e) {
            spdlog::error("frame tick failed: {}", winrt::to_string(e.message()));
        } catch (const std::exception& e) {
            spdlog::error("frame tick failed: {}", e.what());
        }
        nextIndex_ = ++index;
    }
}

}  // namespace elv
