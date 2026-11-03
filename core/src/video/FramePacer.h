#pragma once

#include <windows.h>

#include <winrt/base.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace elv {

// Produces a constant frame rate from irregular capture frames. Ticks run on a
// dedicated thread at frameIndex / fps, starting with the first new frame.
// A tick without a new frame is a duplicate; ticks missed because the handler
// ran late are skipped (dropped) so the timeline never drifts.
// While paused no ticks run; after resume the frame index continues where it
// stopped, so paused time does not appear in the output timeline.
class FramePacer {
public:
    using TickHandler = std::function<void(std::uint64_t frameIndex, bool duplicate)>;

    FramePacer(std::uint32_t fpsNumerator, std::uint32_t fpsDenominator = 1);
    ~FramePacer();

    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    void start(TickHandler onTick);
    void stop();
    void pause();
    void resume();

    // Called by the producer after new content is available.
    void notifyNewFrame() { ++generation_; }

    // Index of the next frame, i.e. the recorded length in frames (including dropped slots).
    std::uint64_t nextFrameIndex() const { return nextIndex_; }
    std::uint64_t ticks() const { return ticks_; }
    std::uint64_t duplicated() const { return duplicated_; }
    std::uint64_t dropped() const { return dropped_; }

private:
    void run();
    bool waitUntil(std::int64_t qpcDue);  // false if stop was requested

    std::uint32_t fpsNumerator_;
    std::uint32_t fpsDenominator_;
    std::int64_t qpcFrequency_ = 1;

    TickHandler onTick_;
    std::thread thread_;
    winrt::handle stopEvent_;
    winrt::handle resumeEvent_;  // manual reset; signaled while not paused
    winrt::handle timer_;

    std::atomic<bool> paused_ = false;
    std::atomic<std::uint64_t> nextIndex_ = 0;
    std::atomic<std::uint64_t> generation_ = 0;
    std::atomic<std::uint64_t> ticks_ = 0;
    std::atomic<std::uint64_t> duplicated_ = 0;
    std::atomic<std::uint64_t> dropped_ = 0;
};

}  // namespace elv
