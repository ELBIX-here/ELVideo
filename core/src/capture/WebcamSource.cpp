// Webcam capture and enumerateWebcams().

#include "capture/WebcamSource.h"

#include "core/IRecorder.h"

#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <winrt/base.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>

namespace elv {

namespace {

constexpr wchar_t kTestPatternId[] = L"test";

struct MfCom {
    MfCom() : com(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))), mf(SUCCEEDED(MFStartup(MF_VERSION))) {}
    ~MfCom() {
        if (mf) MFShutdown();
        if (com) CoUninitialize();
    }
    bool com;
    bool mf;
};

std::wstring stringAttribute(IMFActivate* activate, const GUID& key) {
    wchar_t* value = nullptr;
    UINT32 length = 0;
    if (FAILED(activate->GetAllocatedString(key, &value, &length))) return {};
    std::wstring result(value, length);
    CoTaskMemFree(value);
    return result;
}

std::vector<winrt::com_ptr<IMFActivate>> cameraActivates() {
    winrt::com_ptr<IMFAttributes> attributes;
    winrt::check_hresult(MFCreateAttributes(attributes.put(), 1));
    winrt::check_hresult(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    winrt::check_hresult(MFEnumDeviceSources(attributes.get(), &devices, &count));
    std::vector<winrt::com_ptr<IMFActivate>> result;
    for (UINT32 i = 0; i < count; ++i) {
        winrt::com_ptr<IMFActivate> a;
        a.attach(devices[i]);
        result.push_back(std::move(a));
    }
    CoTaskMemFree(devices);
    return result;
}

}  // namespace

std::vector<WebcamDevice> enumerateWebcams() {
    MfCom mf;
    std::vector<WebcamDevice> cameras;
    try {
        for (const auto& a : cameraActivates())
            cameras.push_back({stringAttribute(a.get(), MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK),
                               stringAttribute(a.get(), MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME)});
    } catch (const winrt::hresult_error& e) {
        spdlog::warn("cannot list cameras: {}", winrt::to_string(e.message()));
    }
    return cameras;
}

WebcamSource::WebcamSource(std::wstring deviceId) : deviceId_(std::move(deviceId)) {}

WebcamSource::~WebcamSource() { stop(); }

void WebcamSource::start(FrameHandler onFrame, WarningHandler onWarning) {
    if (thread_.joinable()) return;
    onFrame_ = std::move(onFrame);
    onWarning_ = std::move(onWarning);
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

void WebcamSource::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void WebcamSource::run() {
    SetThreadDescription(GetCurrentThread(), L"ELVideo webcam");
    MfCom mf;
    try {
        if (deviceId_ == kTestPatternId)
            runTestPattern();
        else
            runCamera();
    } catch (const winrt::hresult_error& e) {
        const std::string message = std::format("webcam stopped: {} (0x{:08X})", winrt::to_string(e.message()),
                                                static_cast<unsigned>(e.code().value));
        spdlog::warn("{}", message);
        if (onWarning_) onWarning_(message);
    }
}

void WebcamSource::runCamera() {
    winrt::com_ptr<IMFActivate> chosen;
    for (const auto& a : cameraActivates()) {
        if (deviceId_.empty() || stringAttribute(a.get(), MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK) == deviceId_) {
            chosen = a;
            break;
        }
    }
    if (!chosen) winrt::throw_hresult(MF_E_NO_CAPTURE_DEVICES_AVAILABLE);

    winrt::com_ptr<IMFMediaSource> source;
    winrt::check_hresult(chosen->ActivateObject(IID_PPV_ARGS(source.put())));
    struct Shutdown {
        IMFActivate* a;
        ~Shutdown() { a->ShutdownObject(); }
    } shutdown{chosen.get()};

    winrt::com_ptr<IMFAttributes> attributes;
    winrt::check_hresult(MFCreateAttributes(attributes.put(), 1));
    // Lets the reader decode (MJPEG) and convert to RGB32.
    winrt::check_hresult(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE));
    winrt::com_ptr<IMFSourceReader> reader;
    winrt::check_hresult(MFCreateSourceReaderFromMediaSource(source.get(), attributes.get(), reader.put()));
    constexpr DWORD kStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

    // Native format: the largest up to 1280x720 that runs at 24 fps or more.
    winrt::com_ptr<IMFMediaType> best;
    UINT64 bestScore = 0;
    for (DWORD i = 0;; ++i) {
        winrt::com_ptr<IMFMediaType> type;
        if (FAILED(reader->GetNativeMediaType(kStream, i, type.put()))) break;
        UINT32 w = 0, h = 0, num = 0, den = 1;
        MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio(type.get(), MF_MT_FRAME_RATE, &num, &den);
        if (w > 1280 || h > 720 || den == 0 || num / den < 24) continue;
        const UINT64 score = static_cast<UINT64>(w) * h * 100 + std::min(num / den, 60u);
        if (score > bestScore) {
            bestScore = score;
            best = type;
        }
    }
    if (best) winrt::check_hresult(reader->SetCurrentMediaType(kStream, nullptr, best.get()));

    winrt::com_ptr<IMFMediaType> rgb;
    winrt::check_hresult(MFCreateMediaType(rgb.put()));
    winrt::check_hresult(rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    winrt::check_hresult(rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
    winrt::check_hresult(reader->SetCurrentMediaType(kStream, nullptr, rgb.get()));
    winrt::com_ptr<IMFMediaType> current;
    winrt::check_hresult(reader->GetCurrentMediaType(kStream, current.put()));
    UINT32 width = 0, height = 0;
    winrt::check_hresult(MFGetAttributeSize(current.get(), MF_MT_FRAME_SIZE, &width, &height));
    // RGB32 from MF is bottom-up unless the stride says otherwise.
    const auto stride = static_cast<INT32>(MFGetAttributeUINT32(current.get(), MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(-static_cast<INT32>(width * 4))));
    spdlog::info("webcam opened: {} {}x{}", winrt::to_string(stringAttribute(chosen.get(), MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME)), width, height);
    buffer_.resize(static_cast<std::size_t>(width) * height * 4);

    while (running_) {
        DWORD flags = 0;
        LONGLONG time = 0;
        winrt::com_ptr<IMFSample> sample;
        winrt::check_hresult(reader->ReadSample(kStream, 0, nullptr, &flags, &time, sample.put()));
        if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) winrt::throw_hresult(MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED);
        if (!sample) continue;

        winrt::com_ptr<IMFMediaBuffer> buffer;
        winrt::check_hresult(sample->GetBufferByIndex(0, buffer.put()));
        BYTE* scanline0 = nullptr;
        LONG pitch = 0;
        const auto buffer2d = buffer.try_as<IMF2DBuffer>();
        BYTE* data = nullptr;
        if (buffer2d) {
            winrt::check_hresult(buffer2d->Lock2D(&scanline0, &pitch));
        } else {
            DWORD length = 0;
            winrt::check_hresult(buffer->Lock(&data, nullptr, &length));
            pitch = stride;
            scanline0 = pitch < 0 ? data + static_cast<std::ptrdiff_t>(-pitch) * (height - 1) : data;
        }
        for (UINT32 y = 0; y < height; ++y) {
            std::uint8_t* row = buffer_.data() + static_cast<std::size_t>(y) * width * 4;
            std::memcpy(row, scanline0 + static_cast<std::ptrdiff_t>(y) * pitch, static_cast<std::size_t>(width) * 4);
            for (UINT32 x = 3; x < width * 4; x += 4) row[x] = 0xFF;
        }
        if (buffer2d)
            buffer2d->Unlock2D();
        else
            buffer->Unlock();

        ++frames_;
        if (onFrame_) onFrame_({buffer_.data(), width, height});
    }
}

void WebcamSource::runTestPattern() {
    // 640x360 at 30 fps: colour bars that move one bar per second, plus a white
    // square that circles, so overlay placement and motion can be checked.
    constexpr std::uint32_t width = 640, height = 360;
    buffer_.resize(static_cast<std::size_t>(width) * height * 4);
    const std::uint8_t bars[6][3] = {{0, 0, 255}, {0, 255, 0}, {255, 0, 0}, {0, 255, 255}, {255, 0, 255}, {255, 255, 0}};
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t n = 0; running_; ++n) {
        const double t = n / 30.0;
        const int shift = static_cast<int>(t);
        const int sx = static_cast<int>(width / 2 + std::cos(t * 2) * 200) - 30;
        const int sy = static_cast<int>(height / 2 + std::sin(t * 2) * 100) - 30;
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x) {
                std::uint8_t* p = buffer_.data() + (static_cast<std::size_t>(y) * width + x) * 4;
                const bool square = static_cast<int>(x) >= sx && static_cast<int>(x) < sx + 60 &&
                                    static_cast<int>(y) >= sy && static_cast<int>(y) < sy + 60;
                const auto& c = bars[(x * 6 / width + shift) % 6];
                p[0] = square ? 255 : c[0];
                p[1] = square ? 255 : c[1];
                p[2] = square ? 255 : c[2];
                p[3] = 0xFF;
            }
        ++frames_;
        if (onFrame_) onFrame_({buffer_.data(), width, height});
        std::this_thread::sleep_until(start + std::chrono::microseconds(static_cast<long long>((n + 1) * 1e6 / 30)));
    }
}

}  // namespace elv
