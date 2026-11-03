#pragma once

#include "core/Types.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace elv {

struct RecorderStats {
    std::chrono::milliseconds elapsed{0};  // recorded media time; excludes paused time
    std::uint64_t framesCaptured = 0;      // frames delivered by the capture source
    std::uint64_t framesEncoded = 0;
    std::uint64_t framesDuplicated = 0;    // CFR ticks that repeated the previous frame
    std::uint64_t framesDropped = 0;
    // Peak level (0..1) of each audio source since the previous stats; -1 when the
    // source is not recorded.
    float systemPeak = -1.0f;
    float microphonePeak = -1.0f;
};

struct RecordingInfo {
    std::filesystem::path path;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps = 0;
    std::wstring encoderName;
    bool hardwareEncoder = false;
    std::wstring adapterName;
    bool hasAudio = false;
};

enum class StopReason {
    User,          // stop() was called
    SourceClosed,  // the captured window was closed or the monitor disconnected
    Error,         // encoding or GPU failure; onError carries the message
};

// Callbacks run on the engine thread, one at a time. Implementations must not
// block and should marshal to their own thread (e.g. QMetaObject::invokeMethod).
// Calling IRecorder methods from a callback is allowed.
class IRecorderListener {
public:
    virtual ~IRecorderListener() = default;

    virtual void onStateChanged(RecorderState) {}
    // Recording has begun; describes the output.
    virtual void onStarted(const RecordingInfo&) {}
    // Periodic progress while recording or paused (about 4 times per second).
    virtual void onStats(const RecorderStats&) {}
    // The file is finalized (also after SourceClosed and Error). Not called when the
    // recording stopped before its first frame; that empty file is deleted.
    virtual void onFinished(const RecordingInfo&, const RecorderStats&, StopReason) {}
    // A start failed or a recording failed. UTF-8 message.
    virtual void onError(const std::string& /*message*/) {}
    // Something degraded but the recording goes on (e.g. a microphone could not be
    // opened or was unplugged; its part of the track is silent). UTF-8 message.
    virtual void onWarning(const std::string& /*message*/) {}
};

}  // namespace elv
