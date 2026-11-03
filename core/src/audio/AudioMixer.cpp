#include "audio/AudioMixer.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace elv {

namespace {

constexpr std::int64_t kHnsPerSecond = 10'000'000;
// Timestamp mismatch below this is jitter; above it, insert silence or trim.
constexpr std::int64_t kToleranceFrames = AudioMixer::kSampleRate / 50;  // 20 ms
constexpr std::int64_t kLatencyFrames = AudioMixer::kSampleRate / 10;    // 100 ms
constexpr std::int64_t kMaxBufferedFrames = AudioMixer::kSampleRate * 2;
constexpr DWORD kMixIntervalMs = 10;

std::int64_t qpcNowHns() {
    static const std::int64_t frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart / frequency * kHnsPerSecond + t.QuadPart % frequency * kHnsPerSecond / frequency;
}

}  // namespace

AudioMixer::AudioMixer(std::size_t sourceCount, Writer writer)
    : writer_(std::move(writer)), sources_(sourceCount), stopEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

AudioMixer::~AudioMixer() {
    SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    CloseHandle(stopEvent_);
}

void AudioMixer::setGain(std::size_t source, float gain) {
    std::lock_guard lock(mutex_);
    sources_.at(source).gain = gain;
}

std::int64_t AudioMixer::positionAt(std::int64_t qpcHns) const {
    return (qpcHns - startHns_ - pausedTotalHns_) * kSampleRate / kHnsPerSecond;
}

std::int64_t AudioMixer::nowPosition() const { return positionAt(paused_ ? pausedAtHns_ : qpcNowHns()); }

void AudioMixer::push(std::size_t index, const float* frames, std::uint32_t count, std::int64_t qpcHns) {
    std::lock_guard lock(mutex_);
    if (!started_ || paused_ || count == 0) return;
    Source& s = sources_.at(index);

    std::int64_t pos = positionAt(qpcHns);
    if (s.samples.empty()) {
        s.frontPos = pos;
    } else {
        const std::int64_t end = s.frontPos + static_cast<std::int64_t>(s.samples.size() / kChannels);
        const std::int64_t diff = pos - end;
        if (diff > kToleranceFrames) {
            s.samples.insert(s.samples.end(), static_cast<std::size_t>(diff) * kChannels, 0.0f);  // gap
        } else if (diff < -kToleranceFrames) {
            const auto skip = static_cast<std::uint32_t>(-diff);  // overlaps what we already have
            if (skip >= count) return;
            if (frames) frames += static_cast<std::size_t>(skip) * kChannels;
            count -= skip;
        }
    }
    if (frames)
        s.samples.insert(s.samples.end(), frames, frames + static_cast<std::size_t>(count) * kChannels);
    else
        s.samples.insert(s.samples.end(), static_cast<std::size_t>(count) * kChannels, 0.0f);

    // Drop what arrived too late to be mixed, and never hold more than kMaxBufferedFrames.
    const std::int64_t buffered = static_cast<std::int64_t>(s.samples.size() / kChannels);
    const std::int64_t drop = std::clamp(std::max(emitted_ - s.frontPos, buffered - kMaxBufferedFrames),
                                         std::int64_t{0}, buffered);
    if (drop > 0) {
        s.samples.erase(s.samples.begin(), s.samples.begin() + static_cast<std::ptrdiff_t>(drop * kChannels));
        s.frontPos += drop;
    }
}

void AudioMixer::begin() {
    {
        std::lock_guard lock(mutex_);
        if (started_) return;
        started_ = true;
        startHns_ = qpcNowHns();
        for (auto& s : sources_) s.samples.clear();
    }
    thread_ = std::thread([this] { run(); });
}

void AudioMixer::pause() {
    std::lock_guard lock(mutex_);
    if (!started_ || paused_) return;
    pausedAtHns_ = qpcNowHns();
    paused_ = true;
}

void AudioMixer::resume() {
    std::lock_guard lock(mutex_);
    if (!paused_) return;
    pausedTotalHns_ += qpcNowHns() - pausedAtHns_;
    paused_ = false;
}

void AudioMixer::run() {
    SetThreadDescription(GetCurrentThread(), L"ELVideo audio mixer");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    while (WaitForSingleObject(stopEvent_, kMixIntervalMs) == WAIT_TIMEOUT) {
        std::int64_t target;
        {
            std::lock_guard lock(mutex_);
            target = nowPosition() - kLatencyFrames;
        }
        try {
            emitUntil(target);
        } catch (const std::exception& e) {
            spdlog::error("audio mixing failed: {}", e.what());
        }
    }
}

void AudioMixer::finish(std::int64_t endHns) {
    SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard lock(mutex_);
        if (!started_) return;
    }
    emitUntil(endHns * kSampleRate / kHnsPerSecond);
}

void AudioMixer::emitUntil(std::int64_t target) {
    std::int64_t start;
    std::uint32_t frames;
    {
        std::lock_guard lock(mutex_);
        if (target <= emitted_) return;
        start = emitted_;
        frames = static_cast<std::uint32_t>(target - start);
        mix_.assign(static_cast<std::size_t>(frames) * kChannels, 0.0f);

        for (auto& s : sources_) {
            // Samples before `start` are too late; samples from frontPos on are mixed in place.
            if (s.frontPos < start) {
                const auto stale = std::min<std::int64_t>(start - s.frontPos,
                                                          static_cast<std::int64_t>(s.samples.size() / kChannels));
                s.samples.erase(s.samples.begin(), s.samples.begin() + static_cast<std::ptrdiff_t>(stale * kChannels));
                s.frontPos += stale;
            }
            const std::int64_t offset = s.frontPos - start;
            if (s.samples.empty() || offset >= frames) continue;

            const auto take = static_cast<std::size_t>(
                std::min<std::int64_t>(frames - offset, static_cast<std::int64_t>(s.samples.size() / kChannels)));
            float peak = 0.0f;
            float* out = mix_.data() + offset * kChannels;
            auto in = s.samples.begin();
            for (std::size_t i = 0; i < take * kChannels; ++i, ++in) {
                const float v = *in * s.gain;
                out[i] += v;
                peak = std::max(peak, std::abs(v));
            }
            s.samples.erase(s.samples.begin(), in);
            s.frontPos += static_cast<std::int64_t>(take);
            if (peak > s.peak.load(std::memory_order_relaxed)) s.peak.store(peak, std::memory_order_relaxed);
        }
        emitted_ = target;
    }

    pcm_.resize(mix_.size());
    for (std::size_t i = 0; i < mix_.size(); ++i)
        pcm_[i] = static_cast<std::int16_t>(std::lrint(std::clamp(mix_[i], -1.0f, 1.0f) * 32767.0f));
    writer_(pcm_.data(), frames, start * kHnsPerSecond / kSampleRate);
}

float AudioMixer::takePeak(std::size_t source) {
    return std::min(1.0f, sources_.at(source).peak.exchange(0.0f, std::memory_order_relaxed));
}

}  // namespace elv
