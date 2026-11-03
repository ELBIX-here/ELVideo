#include "capture/WgcCapture.h"

#include <DispatcherQueue.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.System.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace elv {

using namespace winrt::Windows::Graphics::Capture;
using winrt::Windows::Graphics::SizeInt32;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::System::DispatcherQueue;
using winrt::Windows::System::DispatcherQueueController;

namespace {

constexpr auto kPixelFormat = DirectXPixelFormat::B8G8R8A8UIntNormalized;
constexpr int kBufferCount = 2;

DispatcherQueueController createDedicatedQueue() {
    const DispatcherQueueOptions options{sizeof(DispatcherQueueOptions), DQTYPE_THREAD_DEDICATED, DQTAT_COM_STA};
    DispatcherQueueController controller{nullptr};
    winrt::check_hresult(CreateDispatcherQueueController(
        options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))));
    return controller;
}

template <typename F>
auto runOnQueue(const DispatcherQueue& queue, F&& fn) {
    std::packaged_task<decltype(fn())()> task(std::forward<F>(fn));
    auto result = task.get_future();
    if (!queue.TryEnqueue([&task] { task(); })) throw std::runtime_error("capture dispatcher is not running");
    return result.get();
}

GraphicsCaptureItem createItem(const CaptureSource& source) {
    const auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    const auto iid = winrt::guid_of<IGraphicsCaptureItem>();
    const HRESULT hr =
        source.kind == SourceKind::Monitor
            ? interop->CreateForMonitor(reinterpret_cast<HMONITOR>(source.handle), iid, winrt::put_abi(item))
            : interop->CreateForWindow(reinterpret_cast<HWND>(source.handle), iid, winrt::put_abi(item));
    if (FAILED(hr))
        throw std::runtime_error(std::format("cannot create capture item (0x{:08X})", static_cast<unsigned>(hr)));
    return item;
}

// GraphicsCaptureItem raises Closed only through a DispatcherQueue on the thread
// that created it, and every creation opens an ALPC port the OS keeps for the
// life of the process. So items are created on one long-lived dispatcher thread
// and reused per source until the window closes or the monitor goes away.
class CaptureItemCache {
public:
    static CaptureItemCache& instance() {
        // Never destroyed: the dispatcher thread lives as long as the process, and
        // tearing it down during static destruction would race COM shutdown.
        static auto* cache = new CaptureItemCache();
        return *cache;
    }

    GraphicsCaptureItem get(const CaptureSource& source) {
        const Key key{source.kind, source.handle};
        {
            std::lock_guard lock(mutex_);
            if (const auto it = items_.find(key); it != items_.end()) {
                // A destroyed window's HWND value can be reused by a new window.
                if (source.kind == SourceKind::Monitor || IsWindow(reinterpret_cast<HWND>(source.handle)))
                    return it->second;
                items_.erase(it);
            }
        }

        GraphicsCaptureItem item = runOnQueue(controller_.DispatcherQueue(), [&] { return createItem(source); });
        item.Closed([this, key](const auto&, const auto&) {
            std::lock_guard lock(mutex_);
            items_.erase(key);
        });

        std::lock_guard lock(mutex_);
        return items_.try_emplace(key, item).first->second;
    }

private:
    using Key = std::pair<SourceKind, std::uintptr_t>;

    CaptureItemCache() : controller_(createDedicatedQueue()) {}

    DispatcherQueueController controller_;
    std::mutex mutex_;
    std::map<Key, GraphicsCaptureItem> items_;
};

}  // namespace

HMONITOR monitorForSource(const CaptureSource& source) {
    if (source.kind == SourceKind::Monitor) return reinterpret_cast<HMONITOR>(source.handle);
    return MonitorFromWindow(reinterpret_cast<HWND>(source.handle), MONITOR_DEFAULTTONEAREST);
}

WgcCapture::WgcCapture(const D3DDevice& device, const CaptureSource& source, CaptureOptions options)
    : device_(device),
      options_(options),
      window_(source.kind == SourceKind::Window ? reinterpret_cast<HWND>(source.handle) : nullptr),
      item_(CaptureItemCache::instance().get(source)) {}

WgcCapture::~WgcCapture() { stop(); }

void WgcCapture::start(FrameHandler onFrame, ClosedHandler onClosed) {
    std::lock_guard lock(mutex_);
    if (running_) return;

    onFrame_ = std::move(onFrame);
    onClosed_ = std::move(onClosed);

    poolSize_ = item_.Size();
    framePool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(device_.winrtDevice(), kPixelFormat,
                                                                kBufferCount, poolSize_);
    frameArrivedToken_ = framePool_.FrameArrived(
        [this](const Direct3D11CaptureFramePool& pool, const auto&) { onFrameArrived(pool); });
    closedToken_ = item_.Closed([this](const auto&, const auto&) {
        spdlog::info("capture item closed");
        if (onClosed_) onClosed_();
    });

    session_ = framePool_.CreateCaptureSession(item_);
    session_.IsCursorCaptureEnabled(options_.captureCursor);
    if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
            L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
        try {
            session_.IsBorderRequired(options_.drawBorder);
        } catch (const winrt::hresult_error& e) {
            spdlog::warn("cannot change capture border: {}", winrt::to_string(e.message()));
        }
    }

    running_ = true;
    session_.StartCapture();
    spdlog::info("capture started: {}x{}", poolSize_.Width, poolSize_.Height);
}

void WgcCapture::stop() {
    {
        std::lock_guard lock(mutex_);  // waits for an in-flight frame callback
        if (!running_) return;
        running_ = false;
    }
    item_.Closed(closedToken_);
    framePool_.FrameArrived(frameArrivedToken_);
    session_.Close();
    framePool_.Close();
    session_ = nullptr;
    framePool_ = nullptr;
    spdlog::info("capture stopped");
}

void WgcCapture::onFrameArrived(const Direct3D11CaptureFramePool& pool) {
    std::lock_guard lock(mutex_);
    if (!running_) return;

    try {
        const auto frame = pool.TryGetNextFrame();
        if (!frame) return;

        // Minimized windows report only their title bar (or an empty size).
        if (window_ && IsIconic(window_)) return;
        const SizeInt32 contentSize = frame.ContentSize();
        if (contentSize.Width <= 0 || contentSize.Height <= 0) return;

        // A frame whose size differs from the pool was drawn into a surface of the
        // old size (cropped or padded), and restoring windows briefly report a stale
        // title-bar size. Recreate the pool and wait for a frame that fits, so
        // consumers keep the last good frame in the meantime.
        if (contentSize.Width != poolSize_.Width || contentSize.Height != poolSize_.Height) {
            spdlog::info("capture size changed: {}x{} -> {}x{}", poolSize_.Width, poolSize_.Height,
                         contentSize.Width, contentSize.Height);
            poolSize_ = contentSize;
            pool.Recreate(device_.winrtDevice(), kPixelFormat, kBufferCount, poolSize_);
            // A repaint that raced the recreate is lost with the old buffers, and a
            // static window would never send another. Ask for one; asynchronous so a
            // hung target cannot block capture.
            if (window_) RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
            return;
        }

        const auto access =
            frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(texture.put())));

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);

        CapturedFrame captured;
        captured.texture = texture.get();
        captured.width = std::min(desc.Width, static_cast<UINT>(contentSize.Width));
        captured.height = std::min(desc.Height, static_cast<UINT>(contentSize.Height));
        captured.systemTimeHns = frame.SystemRelativeTime().count();
        if (onFrame_) onFrame_(captured);
    } catch (const winrt::hresult_error& e) {
        spdlog::error("frame handling failed: {}", winrt::to_string(e.message()));
    } catch (const std::exception& e) {
        spdlog::error("frame handling failed: {}", e.what());
    }
}

}  // namespace elv
