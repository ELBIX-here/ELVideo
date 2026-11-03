// captureThumbnail(): one frame of a source, downscaled on the CPU.

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

std::optional<Thumbnail> captureThumbnail(const CaptureSource& source, std::uint32_t maxWidth,
                                          std::uint32_t maxHeight, const std::optional<CaptureRegion>& region,
                                          std::chrono::milliseconds timeout) {
    if (maxWidth == 0 || maxHeight == 0) return std::nullopt;
    if (source.kind == SourceKind::Window && IsIconic(reinterpret_cast<HWND>(source.handle))) return std::nullopt;

    ComScope com;
    try {
        D3DDevice device(monitorForSource(source));
        CaptureOptions options;
        options.captureCursor = false;
        WgcCapture capture(device, source, options);

        std::mutex mutex;
        std::condition_variable ready;
        std::vector<std::uint8_t> pixels;
        std::uint32_t width = 0, height = 0;
        bool done = false;

        capture.start(
            [&](const CapturedFrame& frame) {
                {
                    std::lock_guard lock(mutex);
                    if (done) return;
                }
                auto copy = readTexturePixels(device, frame.texture, frame.width, frame.height);
                std::lock_guard lock(mutex);
                pixels = std::move(copy);
                width = frame.width;
                height = frame.height;
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

        if (pixels.empty()) return std::nullopt;
        if (region) {
            const auto x0 = static_cast<std::uint32_t>(std::clamp(region->x, 0, static_cast<std::int32_t>(width)));
            const auto y0 = static_cast<std::uint32_t>(std::clamp(region->y, 0, static_cast<std::int32_t>(height)));
            const auto x1 = static_cast<std::uint32_t>(
                std::clamp(region->x + region->width, static_cast<std::int32_t>(x0), static_cast<std::int32_t>(width)));
            const auto y1 = static_cast<std::uint32_t>(
                std::clamp(region->y + region->height, static_cast<std::int32_t>(y0), static_cast<std::int32_t>(height)));
            if (x1 <= x0 || y1 <= y0) return std::nullopt;
            std::vector<std::uint8_t> cropped(static_cast<std::size_t>(x1 - x0) * (y1 - y0) * 4);
            for (std::uint32_t y = y0; y < y1; ++y)
                std::copy_n(pixels.data() + (static_cast<std::size_t>(y) * width + x0) * 4, (x1 - x0) * 4,
                            cropped.data() + static_cast<std::size_t>(y - y0) * (x1 - x0) * 4);
            return downscale(cropped, x1 - x0, y1 - y0, maxWidth, maxHeight);
        }
        return downscale(pixels, width, height, maxWidth, maxHeight);
    } catch (const winrt::hresult_error& e) {
        spdlog::warn("thumbnail failed: {}", winrt::to_string(e.message()));
    } catch (const std::exception& e) {
        spdlog::warn("thumbnail failed: {}", e.what());
    }
    return std::nullopt;
}

}  // namespace elv
