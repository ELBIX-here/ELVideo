#pragma once

#include "core/Types.h"
#include "util/D3DDevice.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace elv {

class D3DDevice;
class WgcCapture;
class VideoConverter;
class MediaWriter;
class FramePacer;
class WasapiSource;
class AudioMixer;
class PreviewRenderer;
class WebcamSource;
class ReplayStream;

struct SessionStats {
    std::uint64_t captured = 0;    // frames delivered by WGC
    std::uint64_t encoded = 0;     // frames written to the encoder
    std::uint64_t duplicated = 0;  // CFR ticks that repeated the previous frame
    std::uint64_t droppedLate = 0;  // CFR ticks skipped because the pacer ran late
    std::uint64_t droppedBusy = 0;  // ticks with no free encoder surface (back-pressure)
    float systemPeak = -1.0f;      // audio peak since the last stats() call; -1 if not recorded
    float microphonePeak = -1.0f;
    double replayBufferedSeconds = 0;

    std::uint64_t dropped() const { return droppedLate + droppedBusy; }
};

// One recording: capture -> NV12 conversion -> CFR pacing -> encoder -> file,
// plus WASAPI sources -> mixer -> AAC on the same timeline.
// The output size is fixed to the source size at construction time.
class RecordingSession {
public:
    // Throws if the source, GPU or encoder cannot be set up.
    explicit RecordingSession(const RecordSettings& settings);
    ~RecordingSession();

    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    // Handlers run on internal threads and must not call stop(). onError fires at
    // most once, when encoding fails (e.g. the GPU was removed); onWarning when an
    // audio device cannot be used (the recording continues with silence for it).
    void start(std::function<void()> onSourceClosed, std::function<void(const std::string&)> onError,
               std::function<void(const std::string&)> onWarning);
    // Stops capture and finalizes the file.
    void stop();
    void pause();
    void resume();

    bool replay() const { return replay_ != nullptr; }
    // Replay mode: writes the buffered seconds to `path`; nullopt if nothing is buffered.
    std::optional<std::chrono::milliseconds> saveReplay(const std::filesystem::path& path);

    // The current output frame scaled down; nullopt before the first frame. Any thread.
    std::optional<Thumbnail> preview(std::uint32_t maxWidth, std::uint32_t maxHeight);

    // Not const: reading the audio peaks resets them.
    SessionStats stats();
    // Recorded media length in frames; paused time is excluded.
    std::uint64_t recordedFrames() const;
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    bool hardwareEncoder() const;
    std::wstring encoderName() const;
    std::wstring adapterName() const;

private:
    void onTick(std::uint64_t frameIndex);

    RecordSettings settings_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;

    std::unique_ptr<D3DDevice> device_;
    std::unique_ptr<WgcCapture> capture_;
    std::unique_ptr<VideoConverter> converter_;
    std::unique_ptr<MediaWriter> writer_;
    std::unique_ptr<FramePacer> pacer_;
    std::vector<std::unique_ptr<WasapiSource>> audioSources_;
    std::unique_ptr<AudioMixer> mixer_;  // null without audio
    int systemSource_ = -1;              // index into audioSources_, -1 when not recorded
    int microphoneSource_ = -1;
    std::atomic<bool> audioBegun_ = false;
    std::atomic<bool> audioWriteFailed_ = false;

    // Webcam overlay: the last screen frame is kept so camera frames can be composited
    // while the screen is static. composeMutex_ serializes the converter.
    std::unique_ptr<WebcamSource> webcam_;
    winrt::com_ptr<ID3D11Texture2D> screenCopy_;
    std::uint32_t screenWidth_ = 0;
    std::uint32_t screenHeight_ = 0;
    std::mutex composeMutex_;
    void composeScreen(ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height);

    winrt::com_ptr<ReplayStream> replay_;  // replay buffer mode

    std::mutex previewMutex_;
    std::unique_ptr<PreviewRenderer> previewRenderer_;

    std::atomic<std::uint64_t> captured_ = 0;
    std::atomic<std::uint64_t> encoded_ = 0;
    std::atomic<std::uint64_t> writerDropped_ = 0;
    std::function<void(const std::string&)> onError_;
    std::atomic<bool> failed_ = false;
    bool stopped_ = false;
    long testFailAfter_ = -1;  // debug builds: simulated failure frame, see constructor
};

}  // namespace elv
