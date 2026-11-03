// probeMediaFile(): duration, size and a frame of a video file through Media Foundation.

#include "core/IRecorder.h"

#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <winrt/base.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

namespace elv {

namespace {

struct MfScope {
    MfScope()
        : com(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))),
          mf(SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {}
    ~MfScope() {
        if (mf) MFShutdown();
        if (com) CoUninitialize();
    }
    bool com;
    bool mf;
};

constexpr DWORD kVideo = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

// The visible picture inside a decoded frame: encoders pad to whole macroblocks
// (626x352 HEVC decodes as 640x352 with green padding), and the minimum display
// aperture says what to show. Falls back to the whole frame.
MFVideoArea visibleArea(IMFMediaType* type, UINT32 width, UINT32 height) {
    MFVideoArea area{};
    UINT32 size = 0;
    if (FAILED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), &size)) ||
        size != sizeof(area) || area.Area.cx <= 0 || area.Area.cy <= 0 ||
        area.OffsetX.value + area.Area.cx > static_cast<LONG>(width) || area.OffsetY.value + area.Area.cy > static_cast<LONG>(height)) {
        area = {};
        area.Area = {static_cast<LONG>(width), static_cast<LONG>(height)};
    }
    return area;
}

}  // namespace

std::optional<MediaFileInfo> probeMediaFile(const std::filesystem::path& path, bool withFrame) {
    MfScope mf;
    if (!mf.mf) return std::nullopt;
    try {
        winrt::com_ptr<IMFAttributes> attributes;
        winrt::check_hresult(MFCreateAttributes(attributes.put(), 1));
        winrt::check_hresult(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE));
        winrt::com_ptr<IMFSourceReader> reader;
        winrt::check_hresult(MFCreateSourceReaderFromURL(path.c_str(), attributes.get(), reader.put()));

        MediaFileInfo info;
        PROPVARIANT duration;
        PropVariantInit(&duration);
        if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION,
                                                       &duration)))
            info.duration = std::chrono::milliseconds(duration.uhVal.QuadPart / 10'000);
        PropVariantClear(&duration);

        winrt::check_hresult(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE));
        if (FAILED(reader->SetStreamSelection(kVideo, TRUE))) return info;  // audio only
        winrt::com_ptr<IMFMediaType> native;
        if (SUCCEEDED(reader->GetNativeMediaType(kVideo, 0, native.put()))) {
            UINT32 w = 0, h = 0;
            MFGetAttributeSize(native.get(), MF_MT_FRAME_SIZE, &w, &h);
            const MFVideoArea visible = visibleArea(native.get(), w, h);
            info.width = static_cast<std::uint32_t>(visible.Area.cx);
            info.height = static_cast<std::uint32_t>(visible.Area.cy);
        }
        if (!withFrame) return info;

        winrt::com_ptr<IMFMediaType> rgb;
        winrt::check_hresult(MFCreateMediaType(rgb.put()));
        winrt::check_hresult(rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        winrt::check_hresult(rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
        winrt::check_hresult(reader->SetCurrentMediaType(kVideo, nullptr, rgb.get()));
        winrt::com_ptr<IMFMediaType> current;
        winrt::check_hresult(reader->GetCurrentMediaType(kVideo, current.put()));
        UINT32 width = 0, height = 0;
        winrt::check_hresult(MFGetAttributeSize(current.get(), MF_MT_FRAME_SIZE, &width, &height));
        const MFVideoArea visible = visibleArea(current.get(), width, height);
        const auto left = static_cast<UINT32>(visible.OffsetX.value);
        const auto top = static_cast<UINT32>(visible.OffsetY.value);
        const auto shownWidth = static_cast<UINT32>(visible.Area.cx);
        const auto shownHeight = static_cast<UINT32>(visible.Area.cy);

        // A frame a little into the video (the first is often black), at most 1 s.
        const auto at = std::min<std::int64_t>(10'000'000, info.duration.count() * 10'000 / 10);
        PROPVARIANT position;
        PropVariantInit(&position);
        position.vt = VT_I8;
        position.hVal.QuadPart = at;
        reader->SetCurrentPosition(GUID_NULL, position);

        for (int attempts = 0; attempts < 200; ++attempts) {
            DWORD flags = 0;
            LONGLONG time = 0;
            winrt::com_ptr<IMFSample> sample;
            winrt::check_hresult(reader->ReadSample(kVideo, 0, nullptr, &flags, &time, sample.put()));
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (!sample || time < at) continue;

            winrt::com_ptr<IMFMediaBuffer> buffer;
            winrt::check_hresult(sample->GetBufferByIndex(0, buffer.put()));
            BYTE* scanline0 = nullptr;
            LONG pitch = 0;
            const auto buffer2d = buffer.try_as<IMF2DBuffer>();
            if (buffer2d) {
                winrt::check_hresult(buffer2d->Lock2D(&scanline0, &pitch));
            } else {
                BYTE* data = nullptr;
                DWORD length = 0;
                winrt::check_hresult(buffer->Lock(&data, nullptr, &length));
                // Rows can be padded to whole macroblocks; find the layout that matches.
                const auto align16 = [](std::uint32_t v) { return (v + 15) & ~15u; };
                pitch = static_cast<LONG>(length / height);
                for (const std::uint32_t w : {width, align16(width)})
                    for (const std::uint32_t h : {height, align16(height)})
                        if (static_cast<std::size_t>(w) * 4 * h == length) pitch = static_cast<LONG>(w * 4);
                scanline0 = data;
            }
            Thumbnail frame;
            frame.width = shownWidth;
            frame.height = shownHeight;
            frame.bgra.resize(static_cast<std::size_t>(shownWidth) * shownHeight * 4);
            for (UINT32 y = 0; y < shownHeight; ++y) {
                std::uint8_t* row = frame.bgra.data() + static_cast<std::size_t>(y) * shownWidth * 4;
                std::memcpy(row, scanline0 + static_cast<std::ptrdiff_t>(y + top) * pitch + left * 4,
                            static_cast<std::size_t>(shownWidth) * 4);
                for (UINT32 x = 3; x < shownWidth * 4; x += 4) row[x] = 0xFF;
            }
            if (buffer2d)
                buffer2d->Unlock2D();
            else
                buffer->Unlock();
            info.frame = std::move(frame);
            break;
        }
        return info;
    } catch (const winrt::hresult_error& e) {
        spdlog::debug("probe {} failed: {}", winrt::to_string(path.wstring()), winrt::to_string(e.message()));
    }
    return std::nullopt;
}

}  // namespace elv
