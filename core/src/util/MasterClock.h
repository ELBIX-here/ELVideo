#pragma once

#include <cstdint>

namespace elv {

// QPC-based clock. All media timestamps are expressed in 100ns units (MF time)
// relative to start(), with paused intervals removed.
class MasterClock {
public:
    MasterClock();

    void start();
    void pause();
    void resume();

    // Current media time in 100ns units.
    std::int64_t now() const;

    // Converts a raw QPC tick value (e.g. from WGC SystemRelativeTime) to media time.
    std::int64_t fromQpc(std::int64_t qpcTicks) const;

    static std::int64_t qpcNow();

private:
    std::int64_t toHns(std::int64_t qpcTicks) const;

    std::int64_t frequency_ = 1;
    std::int64_t startQpc_ = 0;
    std::int64_t pausedAtQpc_ = 0;
    std::int64_t pausedTotalQpc_ = 0;
    bool paused_ = false;
};

}  // namespace elv
