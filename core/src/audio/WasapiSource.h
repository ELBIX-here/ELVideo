#pragma once

#include "core/Types.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace elv {

// Shared-mode WASAPI capture of one endpoint: loopback of an output device
// ("system sound") or a microphone. Windows converts to 48 kHz stereo float.
//
// Runs its own thread. If the device cannot be opened or disappears, the source
// reports a warning and keeps retrying, so plugging it back in resumes audio.
// With an empty device id it follows the Windows default device.
class WasapiSource {
public:
    static constexpr std::uint32_t kSampleRate = 48'000;
    static constexpr std::uint32_t kChannels = 2;

    // frames: interleaved stereo float, or nullptr for `count` frames of silence.
    // qpcHns: QPC time of the first frame in 100ns units.
    using PacketHandler = std::function<void(const float* frames, std::uint32_t count, std::int64_t qpcHns)>;
    using WarningHandler = std::function<void(const std::string&)>;

    WasapiSource(AudioDeviceKind kind, std::wstring deviceId);
    ~WasapiSource();

    WasapiSource(const WasapiSource&) = delete;
    WasapiSource& operator=(const WasapiSource&) = delete;

    void start(PacketHandler onPacket, WarningHandler onWarning);
    void stop();

    // Friendly name of the device currently open (empty while none is).
    std::wstring deviceName() const;

private:
    void run();
    // Captures until stopped or the device must be reopened. Throws on device errors.
    void captureFrom(const std::wstring& deviceId);

    const AudioDeviceKind kind_;
    const std::wstring requestedId_;
    PacketHandler onPacket_;
    WarningHandler onWarning_;
    HANDLE stopEvent_ = nullptr;
    std::thread thread_;
    mutable std::mutex nameMutex_;
    std::wstring deviceName_;
    bool warned_ = false;  // capture thread only
};

// Id of the current default endpoint, empty if there is none.
std::wstring defaultAudioDeviceId(AudioDeviceKind kind);

}  // namespace elv
