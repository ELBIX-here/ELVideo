// probeVideoEncoder(): a short real encode, because a registered encoder may still
// fail on this GPU (e.g. an AV1 MFT installed by a driver for GPUs that have AV1).

#include "core/IRecorder.h"

#include "capture/WgcCapture.h"
#include "output/MediaWriter.h"
#include "util/D3DDevice.h"
#include "video/VideoConverter.h"

#include <windows.h>

#include <spdlog/spdlog.h>

#include <filesystem>
#include <format>

namespace elv {

namespace {

class ComScope {
public:
    ComScope() : initialized_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~ComScope() {
        if (initialized_) CoUninitialize();
    }

private:
    bool initialized_;
};

}  // namespace

EncoderProbe probeVideoEncoder(VideoCodec codec, const CaptureSource& source, bool preferHardware) {
    ComScope com;
    EncoderProbe probe;
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    const std::filesystem::path path =
        std::filesystem::path(tempDir) / std::format(L"elvideo-probe-{}-{}.mp4", GetCurrentProcessId(), static_cast<int>(codec));
    try {
        D3DDevice device(monitorForSource(source));
        VideoConverter converter(device, 640, 360);
        MediaWriterConfig config;
        config.path = path;
        config.width = 640;
        config.height = 360;
        config.fpsNumerator = 30;
        config.bitrateKbps = 2'000;
        config.codec = codec;
        config.container = Container::Mp4;
        config.preferHardware = preferHardware;
        {
            MediaWriter writer(device, config);
            for (int i = 0; i < 5; ++i) writer.writeVideo(converter.output(), i * 333'333LL, 333'333LL);
            writer.finalize();
            probe.encoderName = writer.encoderName();
            probe.hardware = writer.hardwareEncoder();
        }
        probe.supported = true;
    } catch (const winrt::hresult_error& e) {
        probe.error = std::format("{} (0x{:08X})", winrt::to_string(e.message()), static_cast<unsigned>(e.code().value));
    } catch (const std::exception& e) {
        probe.error = e.what();
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    spdlog::info("encoder probe {}: {}", codecName(codec),
                 probe.supported ? winrt::to_string(probe.encoderName) : "unsupported: " + probe.error);
    return probe;
}

}  // namespace elv
