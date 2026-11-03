// `probe` and `synccheck`: read a recording back with Media Foundation to verify it.

#include "Probe.h"

#include "util/ImageIO.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

struct MfScope {
    MfScope() { winrt::check_hresult(MFStartup(MF_VERSION, MFSTARTUP_LITE)); }
    ~MfScope() { MFShutdown(); }
};

constexpr DWORD kVideo = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD kAudio = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

winrt::com_ptr<IMFSourceReader> openReader(const std::filesystem::path& path, DWORD stream, bool decodeToRgb) {
    winrt::com_ptr<IMFAttributes> attributes;
    winrt::check_hresult(MFCreateAttributes(attributes.put(), 1));
    if (decodeToRgb) winrt::check_hresult(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE));

    winrt::com_ptr<IMFSourceReader> reader;
    winrt::check_hresult(MFCreateSourceReaderFromURL(path.c_str(), attributes.get(), reader.put()));
    winrt::check_hresult(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE));
    if (FAILED(reader->SetStreamSelection(stream, TRUE))) return nullptr;  // no such stream
    return reader;
}

// Video reader that decodes to RGB32; returns the frame size.
winrt::com_ptr<IMFSourceReader> openRgbReader(const std::filesystem::path& path, UINT32& width, UINT32& height) {
    auto reader = openReader(path, kVideo, true);
    winrt::com_ptr<IMFMediaType> rgb;
    winrt::check_hresult(MFCreateMediaType(rgb.put()));
    winrt::check_hresult(rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    winrt::check_hresult(rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
    winrt::check_hresult(reader->SetCurrentMediaType(kVideo, nullptr, rgb.get()));
    winrt::com_ptr<IMFMediaType> current;
    winrt::check_hresult(reader->GetCurrentMediaType(kVideo, current.put()));
    winrt::check_hresult(MFGetAttributeSize(current.get(), MF_MT_FRAME_SIZE, &width, &height));
    return reader;
}

// A locked RGB32 frame. Rows may be padded; pitch is negative for bottom-up images.
class FrameView {
public:
    FrameView(IMFSample* sample, UINT32 width, UINT32 height) {
        winrt::check_hresult(sample->GetBufferByIndex(0, buffer_.put()));
        buffer2d_ = buffer_.try_as<IMF2DBuffer>();
        if (buffer2d_) {
            winrt::check_hresult(buffer2d_->Lock2D(&scanline0_, &pitch_));
            return;
        }
        BYTE* data = nullptr;
        DWORD length = 0;
        winrt::check_hresult(buffer_->Lock(&data, nullptr, &length));
        // The decoder pads to whole macroblocks (722 -> 736 px wide, 1080 -> 1088
        // rows) while MF_MT_DEFAULT_STRIDE still reports the unpadded width, so
        // find the padded layout that matches the buffer size.
        const auto align16 = [](std::uint32_t v) { return (v + 15) & ~15u; };
        pitch_ = static_cast<LONG>(length / height);
        for (const std::uint32_t w : {width, align16(width)})
            for (const std::uint32_t h : {height, align16(height)})
                if (static_cast<std::size_t>(w) * 4 * h == length) pitch_ = static_cast<LONG>(w * 4);
        if (static_cast<std::size_t>(std::abs(pitch_)) * height > length ||
            std::abs(pitch_) < static_cast<LONG>(width * 4)) {
            buffer_->Unlock();
            buffer_ = nullptr;
            throw std::runtime_error("unexpected decoded frame layout");
        }
        scanline0_ = pitch_ < 0 ? data + static_cast<std::ptrdiff_t>(-pitch_) * (height - 1) : data;
    }
    ~FrameView() {
        if (buffer2d_)
            buffer2d_->Unlock2D();
        else if (buffer_)
            buffer_->Unlock();
    }
    FrameView(const FrameView&) = delete;
    FrameView& operator=(const FrameView&) = delete;

    const BYTE* row(UINT32 y) const { return scanline0_ + static_cast<std::ptrdiff_t>(y) * pitch_; }

private:
    winrt::com_ptr<IMFMediaBuffer> buffer_;
    winrt::com_ptr<IMF2DBuffer> buffer2d_;
    BYTE* scanline0_ = nullptr;
    LONG pitch_ = 0;
};

// Decodes the frame nearest to `atHns` and writes it as PNG.
void saveFrame(const std::filesystem::path& path, std::int64_t atHns, const std::filesystem::path& png) {
    UINT32 width = 0, height = 0;
    const auto reader = openRgbReader(path, width, height);

    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = atHns;
    winrt::check_hresult(reader->SetCurrentPosition(GUID_NULL, position));

    for (;;) {
        DWORD flags = 0;
        LONGLONG time = 0;
        winrt::com_ptr<IMFSample> sample;
        winrt::check_hresult(reader->ReadSample(kVideo, 0, nullptr, &flags, &time, sample.put()));
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) throw std::runtime_error("no frame at requested time");
        if (!sample || time < atHns) continue;

        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
        std::vector<std::uint8_t> pixels(rowBytes * height);
        {
            const FrameView frame(sample.get(), width, height);
            for (UINT32 y = 0; y < height; ++y) std::copy_n(frame.row(y), rowBytes, pixels.data() + y * rowBytes);
        }
        // RGB32 has an undefined alpha byte; force opaque.
        for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 0xFF;
        elv::savePixelsAsPng(pixels.data(), width, height, width * 4, png);
        std::printf("frame at %.3fs -> %s\n", time / 1e7, winrt::to_string(png.wstring()).c_str());
        return;
    }
}

struct DecodedAudio {
    UINT32 rate = 0;
    UINT32 channels = 0;
    std::vector<float> mono;   // channel average
    LONGLONG firstTime = 0;    // timestamp of mono[0]
    std::uint64_t gaps = 0;    // timestamp discontinuities > 2 ms
    std::uint64_t packets = 0;
    float peak = 0.0f;
};

std::optional<DecodedAudio> decodeAudio(const std::filesystem::path& path) {
    const auto reader = openReader(path, kAudio, false);
    if (!reader) return std::nullopt;

    winrt::com_ptr<IMFMediaType> pcm;
    winrt::check_hresult(MFCreateMediaType(pcm.put()));
    winrt::check_hresult(pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    winrt::check_hresult(pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float));
    winrt::check_hresult(reader->SetCurrentMediaType(kAudio, nullptr, pcm.get()));
    winrt::com_ptr<IMFMediaType> current;
    winrt::check_hresult(reader->GetCurrentMediaType(kAudio, current.put()));

    DecodedAudio audio;
    audio.rate = MFGetAttributeUINT32(current.get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
    audio.channels = MFGetAttributeUINT32(current.get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
    if (!audio.rate || !audio.channels) throw std::runtime_error("unexpected audio format");

    LONGLONG expected = -1;
    for (;;) {
        DWORD flags = 0;
        LONGLONG time = 0;
        winrt::com_ptr<IMFSample> sample;
        winrt::check_hresult(reader->ReadSample(kAudio, 0, nullptr, &flags, &time, sample.put()));
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;

        if (audio.packets++ == 0) {
            audio.firstTime = time;
        } else if (std::abs(time - expected) > 20'000) {
            ++audio.gaps;
        }
        winrt::com_ptr<IMFMediaBuffer> buffer;
        winrt::check_hresult(sample->ConvertToContiguousBuffer(buffer.put()));
        BYTE* data = nullptr;
        DWORD length = 0;
        winrt::check_hresult(buffer->Lock(&data, nullptr, &length));
        const auto* f = reinterpret_cast<const float*>(data);
        const std::size_t frames = length / sizeof(float) / audio.channels;
        for (std::size_t i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (UINT32 c = 0; c < audio.channels; ++c) {
                const float v = f[i * audio.channels + c];
                sum += v;
                audio.peak = std::max(audio.peak, std::abs(v));
            }
            audio.mono.push_back(sum / audio.channels);
        }
        buffer->Unlock();
        expected = time + static_cast<LONGLONG>(frames) * 10'000'000 / audio.rate;
    }
    return audio;
}

double dbfs(float v) { return v > 0 ? 20.0 * std::log10(v) : -120.0; }

// Rising edges of `values` through the midpoint between their min and max, at
// least `minGap` apart. Returns their times.
std::vector<double> risingEdges(const std::vector<double>& times, const std::vector<double>& values, double minGap,
                                double minRange) {
    std::vector<double> edges;
    if (values.empty()) return edges;
    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    if (*hi - *lo < minRange) return edges;
    const double threshold = (*lo + *hi) / 2;
    for (std::size_t i = 1; i < values.size(); ++i)
        if (values[i - 1] < threshold && values[i] >= threshold && (edges.empty() || times[i] - edges.back() >= minGap))
            edges.push_back(times[i]);
    return edges;
}

}  // namespace

int runProbe(const std::filesystem::path& path, const std::filesystem::path& png) {
    MfScope mf;
    const auto reader = openReader(path, kVideo, false);
    if (!reader) throw std::runtime_error("no video stream");

    winrt::com_ptr<IMFMediaType> type;
    winrt::check_hresult(reader->GetNativeMediaType(kVideo, 0, type.put()));
    UINT32 width = 0, height = 0, fpsNum = 0, fpsDen = 1;
    MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &width, &height);
    MFGetAttributeRatio(type.get(), MF_MT_FRAME_RATE, &fpsNum, &fpsDen);
    GUID subtype{};
    type->GetGUID(MF_MT_SUBTYPE, &subtype);

    std::uint64_t samples = 0;
    std::uint64_t keyframes = 0;
    std::uint64_t bytes = 0;
    std::vector<LONGLONG> times;  // decode order; B-frames make it non-monotonic
    const LONGLONG frameHns = fpsNum ? 10'000'000LL * fpsDen / fpsNum : 0;

    for (;;) {
        DWORD flags = 0;
        LONGLONG time = 0;
        winrt::com_ptr<IMFSample> sample;
        winrt::check_hresult(reader->ReadSample(kVideo, 0, nullptr, &flags, &time, sample.put()));
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;

        ++samples;
        if (MFGetAttributeUINT32(sample.get(), MFSampleExtension_CleanPoint, FALSE)) ++keyframes;
        DWORD length = 0;
        sample->GetTotalLength(&length);
        bytes += length;
        times.push_back(time);
    }

    // Gaps: presentation-order jumps larger than 1.5 frame durations.
    std::sort(times.begin(), times.end());
    std::uint64_t gaps = 0;
    for (std::size_t i = 1; i < times.size(); ++i)
        if (frameHns && times[i] - times[i - 1] > frameHns * 3 / 2) ++gaps;
    const LONGLONG first = times.empty() ? 0 : times.front();
    const LONGLONG last = times.empty() ? 0 : times.back();

    const double duration = samples ? (last - first + frameHns) / 1e7 : 0.0;
    std::printf("codec      : %s\n", subtype == MFVideoFormat_H264 ? "H.264" : "other");
    std::printf("size       : %ux%u\n", width, height);
    std::printf("frame rate : %u/%u\n", fpsNum, fpsDen);
    std::printf("frames     : %llu (keyframes %llu, gaps %llu)\n", static_cast<unsigned long long>(samples),
                static_cast<unsigned long long>(keyframes), static_cast<unsigned long long>(gaps));
    std::printf("duration   : %.3f s (avg %.2f fps)\n", duration, duration > 0 ? samples / duration : 0.0);
    std::printf("bitrate    : %.0f kbps\n", duration > 0 ? bytes * 8 / duration / 1000 : 0.0);

    if (const auto audio = decodeAudio(path)) {
        const double audioStart = audio->firstTime / 1e7;
        const double audioDuration = static_cast<double>(audio->mono.size()) / audio->rate;
        std::size_t silent = 0;
        for (float v : audio->mono)
            if (std::abs(v) < 1e-4f) ++silent;
        std::printf("audio      : %u Hz, %u ch, starts %.3f s, %.3f s long (video %+.3f s), gaps %llu\n", audio->rate,
                    audio->channels, audioStart, audioDuration, audioStart + audioDuration - duration,
                    static_cast<unsigned long long>(audio->gaps));
        std::printf("audio level: peak %.1f dBFS, %.0f%% silent\n", dbfs(audio->peak),
                    audio->mono.empty() ? 100.0 : 100.0 * silent / audio->mono.size());
    } else {
        std::printf("audio      : none\n");
    }

    if (!png.empty() && samples) saveFrame(path, (first + last) / 2, png);
    return samples ? 0 : 1;
}

int runSyncCheck(const std::filesystem::path& path) {
    MfScope mf;

    // Video: mean brightness per frame (sampled grid).
    std::vector<double> frameTimes, brightness;
    {
        UINT32 width = 0, height = 0;
        const auto reader = openRgbReader(path, width, height);
        for (;;) {
            DWORD flags = 0;
            LONGLONG time = 0;
            winrt::com_ptr<IMFSample> sample;
            winrt::check_hresult(reader->ReadSample(kVideo, 0, nullptr, &flags, &time, sample.put()));
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (!sample) continue;
            const FrameView frame(sample.get(), width, height);
            double sum = 0;
            std::uint64_t n = 0;
            for (UINT32 y = 0; y < height; y += 8) {
                const BYTE* row = frame.row(y);
                for (UINT32 x = 0; x < width; x += 8, ++n) sum += row[x * 4 + 1];  // green
            }
            frameTimes.push_back(time / 1e7);
            brightness.push_back(sum / std::max<std::uint64_t>(1, n));
        }
    }

    // Audio: RMS in 5 ms windows.
    const auto audio = decodeAudio(path);
    if (!audio) {
        std::printf("no audio stream\n");
        return 1;
    }
    std::vector<double> windowTimes, rms;
    const std::size_t window = audio->rate / 200;
    for (std::size_t i = 0; i + window <= audio->mono.size(); i += window) {
        double sum = 0;
        for (std::size_t k = 0; k < window; ++k) sum += static_cast<double>(audio->mono[i + k]) * audio->mono[i + k];
        windowTimes.push_back(audio->firstTime / 1e7 + static_cast<double>(i) / audio->rate);
        rms.push_back(std::sqrt(sum / window));
    }

    const auto flashes = risingEdges(frameTimes, brightness, 0.3, 30.0);
    const auto beeps = risingEdges(windowTimes, rms, 0.3, 0.01);
    std::printf("flashes %zu, beeps %zu\n", flashes.size(), beeps.size());

    std::vector<double> offsets;
    for (double f : flashes) {
        const auto nearest = std::min_element(beeps.begin(), beeps.end(),
                                              [f](double a, double b) { return std::abs(a - f) < std::abs(b - f); });
        if (nearest == beeps.end() || std::abs(*nearest - f) > 0.5) {
            std::printf("  flash %7.3f s: no beep nearby\n", f);
            continue;
        }
        offsets.push_back(*nearest - f);
        std::printf("  flash %7.3f s  beep %7.3f s  audio %+6.1f ms\n", f, *nearest, (*nearest - f) * 1000);
    }
    if (offsets.empty()) return 1;
    double mean = 0;
    for (double o : offsets) mean += o;
    mean /= offsets.size();
    double var = 0;
    for (double o : offsets) var += (o - mean) * (o - mean);
    const auto [lo, hi] = std::minmax_element(offsets.begin(), offsets.end());
    std::printf("offset: mean %+.1f ms, stddev %.1f ms, range %+.1f..%+.1f ms (positive = audio later)\n",
                mean * 1000, std::sqrt(var / offsets.size()) * 1000, *lo * 1000, *hi * 1000);
    return 0;
}
