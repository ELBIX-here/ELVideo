#include "util/MasterClock.h"

#include <windows.h>

namespace elv {

MasterClock::MasterClock() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    frequency_ = freq.QuadPart;
}

std::int64_t MasterClock::qpcNow() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

void MasterClock::start() {
    startQpc_ = qpcNow();
    pausedTotalQpc_ = 0;
    paused_ = false;
}

void MasterClock::pause() {
    if (paused_) return;
    pausedAtQpc_ = qpcNow();
    paused_ = true;
}

void MasterClock::resume() {
    if (!paused_) return;
    pausedTotalQpc_ += qpcNow() - pausedAtQpc_;
    paused_ = false;
}

std::int64_t MasterClock::now() const {
    return fromQpc(paused_ ? pausedAtQpc_ : qpcNow());
}

std::int64_t MasterClock::fromQpc(std::int64_t qpcTicks) const {
    return toHns(qpcTicks - startQpc_ - pausedTotalQpc_);
}

std::int64_t MasterClock::toHns(std::int64_t ticks) const {
    // Split to avoid overflow of ticks * 10'000'000.
    const std::int64_t whole = ticks / frequency_;
    const std::int64_t part = ticks % frequency_;
    return whole * 10'000'000 + part * 10'000'000 / frequency_;
}

}  // namespace elv
