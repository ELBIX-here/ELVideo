// captureThumbnail() and captureImage(): one frame of a source, cropped and
// (for thumbnails) downscaled on the CPU.

#include "core/IRecorder.h"

#include "capture/WgcCapture.h"
#include "util/D3DDevice.h"
#include "util/ImageIO.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>

namespace elv {

namespace {

// Box filter: each output pixel averages the source pixels it covers.
Thumbnail downscale(const std::vector<std::uint8_t>& src, std::uint32_t srcW, std::uint32_t srcH,
                    std::uint32_t maxW, std::uint32_t maxH) {
    const double scale = std::min({1.0, static_cast<double>(maxW) / srcW, static_cast<double>(maxH) / srcH});
    Thumbnail out;
    out.width = std::max(1u, static_cast<std::uint32_t>(srcW * scale + 0.5));
    out.height = std::max(1u, static_cast<std::uint32_t>(srcH * scale + 0.5));
    out.bgra.resize(static_cast<std::size_t>(out.width) * out.height * 4);

    for (std::uint32_t y = 0; y < out.height; ++y) {
        const std::uint32_t y0 = static_cast<std::uint32_t>(static_cast<std::uint64_t>(y) * srcH / out.height);
        const std::uint32_t y1 =
            std::max(y0 + 1, static_cast<std::uint32_t>(static_cast<std::uint64_t>(y + 1) * srcH / out.height));
        for (std::uint32_t x = 0; x < out.width; ++x) {
            const std::uint32_t x0 = static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * srcW / out.width);
            const std::uint32_t x1 =
                std::max(x0 + 1, static_cast<std::uint32_t>(static_cast<std::uint64_t>(x + 1) * srcW / out.width));
            std::uint32_t sum[3]{};
            for (std::uint32_t sy = y0; sy < y1; ++sy) {
                const std::uint8_t* p = src.data() + (static_cast<std::size_t>(sy) * srcW + x0) * 4;
                for (std::uint32_t sx = x0; sx < x1; ++sx, p += 4) {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                }
            }
            const std::uint32_t n = (y1 - y0) * (x1 - x0);
            std::uint8_t* d = out.bgra.data() + (static_cast<std::size_t>(y) * out.width + x) * 4;
            d[0] = static_cast<std::uint8_t>(sum[0] / n);
            d[1] = static_cast<std::uint8_t>(sum[1] / n);
            d[2] = static_cast<std::uint8_t>(sum[2] / n);
            d[3] = 0xFF;
        }
    }
    return out;
}

// COM for the calling thread (Qt thread-pool threads have none), MTA like the engine.
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

namespace {

// One frame of the source at full resolution, or nullopt when none arrives in time.
std::optional<Thumbnail> grab(const CaptureSource& source, bool cursor, std::chrono::milliseconds timeout) {
    if (source.kind == SourceKind::Window && IsIconic(reinterpret_cast<HWND>(source.handle))) return std::nullopt;

    ComScope com;
    try {
        D3DDevice device(monitorForSource(source));
        CaptureOptions options;
        options.captureCursor = cursor;
        WgcCapture capture(device, source, options);

        std::mutex mutex;
        std::condition_variable ready;
        Thumbnail image;
        bool done = false;

        capture.start(
            [&](const CapturedFrame& frame) {
                {
                    std::lock_guard lock(mutex);
                    if (done) return;
                }
                auto copy = readTexturePixels(device, frame.texture, frame.width, frame.height);
                std::lock_guard lock(mutex);
                image.bgra = std::move(copy);
                image.width = frame.width;
                image.height = frame.height;
                done = true;
                ready.notify_all();
            },
            [&] {
                std::lock_guard lock(mutex);
                done = true;
                ready.notify_all();
            });

        {
            std::unique_lock lock(mutex);
            ready.wait_for(lock, timeout, [&] { return done; });
        }
        capture.stop();
        if (image.bgra.empty()) return std::nullopt;
        return image;
    } catch (const winrt::hresult_error& e) {
        spdlog::warn("capture failed: {}", winrt::to_string(e.message()));
    } catch (const std::exception& e) {
        spdlog::warn("capture failed: {}", e.what());
    }
    return std::nullopt;
}

// The region clipped to the image; nullopt when nothing is left.
std::optional<Thumbnail> crop(const Thumbnail& image, const CaptureRegion& region) {
    const auto width = static_cast<std::int32_t>(image.width);
    const auto height = static_cast<std::int32_t>(image.height);
    const auto x0 = static_cast<std::uint32_t>(std::clamp(region.x, 0, width));
    const auto y0 = static_cast<std::uint32_t>(std::clamp(region.y, 0, height));
    const auto x1 = static_cast<std::uint32_t>(std::clamp(region.x + region.width, static_cast<std::int32_t>(x0), width));
    const auto y1 = static_cast<std::uint32_t>(std::clamp(region.y + region.height, static_cast<std::int32_t>(y0), height));
    if (x1 <= x0 || y1 <= y0) return std::nullopt;
    Thumbnail out;
    out.width = x1 - x0;
    out.height = y1 - y0;
    out.bgra.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    for (std::uint32_t y = y0; y < y1; ++y)
        std::copy_n(image.bgra.data() + (static_cast<std::size_t>(y) * image.width + x0) * 4, out.width * 4,
                    out.bgra.data() + static_cast<std::size_t>(y - y0) * out.width * 4);
    return out;
}

}  // namespace

std::optional<Thumbnail> captureThumbnail(const CaptureSource& source, std::uint32_t maxWidth,
                                          std::uint32_t maxHeight, const std::optional<CaptureRegion>& region,
                                          std::chrono::milliseconds timeout) {
    if (maxWidth == 0 || maxHeight == 0) return std::nullopt;
    auto image = grab(source, false, timeout);
    if (image && region) image = crop(*image, *region);
    if (!image) return std::nullopt;
    return downscale(image->bgra, image->width, image->height, maxWidth, maxHeight);
}

std::optional<Thumbnail> captureImage(const CaptureSource& source, const std::optional<CaptureRegion>& region,
                                      bool includeCursor, std::chrono::milliseconds timeout) {
    auto image = grab(source, includeCursor, timeout);
    if (image && region) image = crop(*image, *region);
    return image;
}

}  // namespace elv
