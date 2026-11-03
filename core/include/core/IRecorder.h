#pragma once

#include "core/Events.h"
#include "core/Types.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace elv {

// Screen recorder engine. All methods are non-blocking and thread-safe: they
// validate the current state, queue the work on the engine thread and return
// whether the command was accepted. Results arrive through IRecorderListener.
//
//   Idle/Error --start--> Starting --> Recording <--pause/resume--> Paused
//   Recording/Paused --stop / source closed--> Stopping --> Idle
//   any failure --> Error (start() is accepted again from Error)
class IRecorder {
public:
    virtual ~IRecorder() = default;

    virtual bool start(const RecordSettings& settings) = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual bool stop() = 0;
    // Replay buffer mode only: writes the buffered seconds to `path` (absolute).
    // Result: onReplaySaved, or onWarning when nothing is buffered yet.
    virtual bool saveReplay(const std::filesystem::path& path) = 0;

    virtual RecorderState state() const = 0;

    // The frame being recorded right now (after cropping and overlays), scaled to
    // fit maxWidth x maxHeight. nullopt when not recording or before the first
    // frame. Any thread; costs a GPU scale and a small readback (~1-3 ms).
    virtual std::optional<Thumbnail> preview(std::uint32_t maxWidth, std::uint32_t maxHeight) = 0;

    // Set before start(); may be nullptr. The pointer must outlive the recorder
    // or be cleared first.
    virtual void setListener(IRecorderListener* listener) = 0;
};

// Destroying the recorder stops and finalizes an active recording.
std::unique_ptr<IRecorder> createRecorder();

// Platform checks usable before any recorder exists.
struct PlatformInfo {
    bool captureSupported = false;          // GraphicsCaptureSession::IsSupported
    bool borderlessCaptureSupported = false;  // IsBorderRequired API present (Win11+)
    std::string windowsBuild;
};

PlatformInfo queryPlatform();
const char* version();

// Monitors first (primary first), then capturable top-level windows in Z order.
// The calling process should be per-monitor DPI aware for sizes to be physical.
std::vector<CaptureSource> enumerateCaptureSources();

// Video encoders Media Foundation offers on this PC (hardware first per codec).
// A codec with no entry cannot be recorded. Hardware encoders belong to a GPU;
// the one that drives the recorded monitor is used.
std::vector<VideoEncoderInfo> enumerateVideoEncoders();
const char* codecName(VideoCodec codec);  // "H.264", "HEVC", "AV1"

// Encodes a few frames with `codec` on the GPU that would record `source` and
// reports which encoder was used, or why it failed. Takes up to a second.
EncoderProbe probeVideoEncoder(VideoCodec codec, const CaptureSource& source, bool preferHardware = true);

// Duration, size and (withFrame) one frame of a video file Media Foundation can
// read (ELVideo's own MP4s, most H.264/HEVC files). Any thread; blocks briefly.
std::optional<MediaFileInfo> probeMediaFile(const std::filesystem::path& path, bool withFrame);

// Cameras Media Foundation can open.
std::vector<WebcamDevice> enumerateWebcams();

// Active audio endpoints, default first. Requires COM on the calling thread.
std::vector<AudioDevice> enumerateAudioDevices(AudioDeviceKind kind);

// Grabs one frame of the source (without the cursor), optionally cropped to
// `region`, scaled to fit maxWidth x maxHeight. Blocks until a frame arrives or
// `timeout` passes, so call it off the UI thread; several calls may run in
// parallel. Returns nullopt when no frame came (minimized window, protected
// content) or the source is gone.
std::optional<Thumbnail> captureThumbnail(const CaptureSource& source, std::uint32_t maxWidth,
                                          std::uint32_t maxHeight,
                                          const std::optional<CaptureRegion>& region = std::nullopt,
                                          std::chrono::milliseconds timeout = std::chrono::milliseconds(800));

// Screenshot: one frame of the source at full resolution (physical pixels),
// optionally cropped to `region`, with or without the mouse cursor. Same
// threading and failure rules as captureThumbnail().
std::optional<Thumbnail> captureImage(const CaptureSource& source,
                                      const std::optional<CaptureRegion>& region = std::nullopt,
                                      bool includeCursor = false,
                                      std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

}  // namespace elv
