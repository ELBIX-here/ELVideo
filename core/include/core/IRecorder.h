#pragma once

#include "core/Events.h"
#include "core/Types.h"

#include <chrono>
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

    virtual RecorderState state() const = 0;

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

// Active audio endpoints, default first. Requires COM on the calling thread.
std::vector<AudioDevice> enumerateAudioDevices(AudioDeviceKind kind);

// Grabs one frame of the source (without the cursor) scaled to fit
// maxWidth x maxHeight. Blocks until a frame arrives or `timeout` passes, so call
// it off the UI thread; several calls may run in parallel. Returns nullopt when
// no frame came (minimized window, protected content) or the source is gone.
std::optional<Thumbnail> captureThumbnail(const CaptureSource& source, std::uint32_t maxWidth,
                                          std::uint32_t maxHeight,
                                          std::chrono::milliseconds timeout = std::chrono::milliseconds(800));

}  // namespace elv
