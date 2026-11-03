#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace elv {

// Mixes several 48 kHz stereo sources onto the recording timeline and emits
// 16-bit PCM in order. The timeline starts at begin() (the first video frame)
// and excludes paused time, like the video pacer, so audio stays in sync.
//
// Sources push timestamped packets from their own threads. Each packet is placed
// at its QPC time: gaps (a loopback source sends nothing while nothing plays)
// become silence, overlaps are trimmed, and small jitter is absorbed so steady
// audio stays continuous. Output lags real time by kLatency to let late packets in.
class AudioMixer {
public:
    static constexpr std::uint32_t kSampleRate = 48'000;
    static constexpr std::uint32_t kChannels = 2;

    // pcm: interleaved stereo int16; timeHns: timeline position of the first frame.
    using Writer = std::function<void(const std::int16_t* pcm, std::uint32_t frames, std::int64_t timeHns)>;

    AudioMixer(std::size_t sourceCount, Writer writer);
    ~AudioMixer();

    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    void setGain(std::size_t source, float gain);

    // Any thread. frames == nullptr means silence. Ignored before begin() and while paused.
    void push(std::size_t source, const float* frames, std::uint32_t count, std::int64_t qpcHns);

    // Starts the timeline now and the mixing thread.
    void begin();
    void pause();
    void resume();
    // Emits everything up to endHns (padding with silence) and stops. Call after
    // the sources have stopped. Safe if begin() never ran (emits nothing).
    void finish(std::int64_t endHns);

    // Highest absolute sample value of the source since the last call (0..1).
    float takePeak(std::size_t source);

private:
    struct Source {
        std::deque<float> samples;  // interleaved stereo
        std::int64_t frontPos = 0;  // timeline frame of samples.front()
        float gain = 1.0f;
        std::atomic<float> peak = 0.0f;
    };

    void run();
    // Under mutex_: timeline frame for a QPC time.
    std::int64_t positionAt(std::int64_t qpcHns) const;
    std::int64_t nowPosition() const;
    // Mixes [emitted_, target) and writes it. Takes the lock itself.
    void emitUntil(std::int64_t target);

    Writer writer_;
    std::vector<Source> sources_;

    mutable std::mutex mutex_;
    bool started_ = false;
    bool paused_ = false;
    std::int64_t startHns_ = 0;
    std::int64_t pausedAtHns_ = 0;
    std::int64_t pausedTotalHns_ = 0;
    std::int64_t emitted_ = 0;  // timeline frames written so far

    HANDLE stopEvent_ = nullptr;
    std::thread thread_;
    std::vector<float> mix_;          // mixing thread scratch
    std::vector<std::int16_t> pcm_;
};

}  // namespace elv
