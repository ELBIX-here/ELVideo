#pragma once

#include "core/Types.h"
#include "util/D3DDevice.h"

#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

namespace elv {

struct MediaWriterConfig {
    std::filesystem::path path;
    std::uint32_t width = 0;   // must be even
    std::uint32_t height = 0;  // must be even
    std::uint32_t fpsNumerator = 60;
    std::uint32_t fpsDenominator = 1;
    std::uint32_t bitrateKbps = 12'000;
    Container container = Container::FragmentedMp4;
    bool preferHardware = true;
    bool audio = false;  // adds an AAC track fed by writeAudio()
    std::uint32_t audioBitrateKbps = 192;
};

// Media Foundation SinkWriter producing H.264 in MP4 / fragmented MP4.
// Takes NV12 textures from the shared D3D device. Hardware encoders get GPU
// surfaces directly; the software fallback gets a CPU copy.
class MediaWriter {
public:
    MediaWriter(const D3DDevice& device, const MediaWriterConfig& config);
    ~MediaWriter();

    MediaWriter(const MediaWriter&) = delete;
    MediaWriter& operator=(const MediaWriter&) = delete;

    // Copies the NV12 texture into an encoder-owned surface and queues it.
    // Returns false (frame dropped) if every surface is still in use by the encoder.
    bool writeVideo(ID3D11Texture2D* nv12, std::int64_t timeHns, std::int64_t durationHns);

    // Interleaved 48 kHz stereo 16-bit PCM. May be called from another thread than
    // writeVideo(); calls must be in timestamp order.
    void writeAudio(const std::int16_t* pcm, std::uint32_t frames, std::int64_t timeHns);
    static constexpr std::uint32_t kAudioSampleRate = 48'000;
    static constexpr std::uint32_t kAudioChannels = 2;

    // Flushes the encoder and closes the file. Safe to call more than once.
    // With no frames written the file is left unfinalized for the caller to delete.
    void finalize();
    std::uint64_t samplesWritten() const { return samplesWritten_; }

    bool hardwareEncoder() const { return hardware_; }
    const std::wstring& encoderName() const { return encoderName_; }

private:
    struct MfPlatform {
        MfPlatform();
        ~MfPlatform();
    };

    void createWriter(bool hardware);
    void addAudioStream();
    void queryEncoder();
    void releaseWriter();
    void writeSystemMemory(ID3D11Texture2D* nv12, std::int64_t timeHns, std::int64_t durationHns);

    MfPlatform platform_;
    const D3DDevice& device_;
    MediaWriterConfig config_;
    UINT resetToken_ = 0;
    winrt::com_ptr<IMFDXGIDeviceManager> deviceManager_;
    winrt::com_ptr<IMFSinkWriter> writer_;
    winrt::com_ptr<IMFVideoSampleAllocatorEx> allocator_;  // hardware: GPU surfaces for the encoder
    winrt::com_ptr<ID3D11Texture2D> staging_;              // software: CPU readback of NV12
    DWORD videoStream_ = 0;
    DWORD audioStream_ = 0;
    std::mutex writeMutex_;  // WriteSample from the pacer and the audio mixer
    std::uint64_t samplesWritten_ = 0;  // pacer thread only
    bool hardware_ = false;
    std::wstring encoderName_;
};

}  // namespace elv
