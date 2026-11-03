#pragma once

#include "core/Types.h"
#include "util/D3DDevice.h"

#include <winrt/Windows.Graphics.Capture.h>

#include <cstdint>
#include <functional>
#include <mutex>

namespace elv {

struct CapturedFrame {
    // Valid only for the duration of the frame callback. The texture may be
    // larger than width x height right after a resize; use the top-left region.
    ID3D11Texture2D* texture = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int64_t systemTimeHns = 0;  // QPC time in 100ns units
};

struct CaptureOptions {
    bool captureCursor = true;
    bool drawBorder = false;  // the yellow border can only be removed on Windows 11
};

// Windows.Graphics.Capture session for one monitor or window.
// Frames arrive on a WGC worker thread; onClosed arrives on the process-wide
// capture dispatcher thread. Handlers must not call stop() or destroy the capture.
class WgcCapture {
public:
    using FrameHandler = std::function<void(const CapturedFrame&)>;
    using ClosedHandler = std::function<void()>;

    // Throws std::runtime_error if the source cannot be captured (e.g. the window is gone).
    WgcCapture(const D3DDevice& device, const CaptureSource& source, CaptureOptions options = {});
    ~WgcCapture();

    WgcCapture(const WgcCapture&) = delete;
    WgcCapture& operator=(const WgcCapture&) = delete;

    // onClosed fires when the window is closed or the monitor is disconnected.
    void start(FrameHandler onFrame, ClosedHandler onClosed);
    void stop();

    // Current size of the source in physical pixels.
    winrt::Windows::Graphics::SizeInt32 size() const { return item_.Size(); }

private:
    void onFrameArrived(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& pool);

    const D3DDevice& device_;
    CaptureOptions options_;
    HWND window_ = nullptr;  // window sources only
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};  // shared per source, see .cpp
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::event_token frameArrivedToken_{};
    winrt::event_token closedToken_{};
    winrt::Windows::Graphics::SizeInt32 poolSize_{};

    FrameHandler onFrame_;
    ClosedHandler onClosed_;

    // Held for the whole frame callback so stop() can wait for it to finish.
    std::mutex mutex_;
    bool running_ = false;
};

// Monitor whose adapter should own the D3D device for this source.
HMONITOR monitorForSource(const CaptureSource& source);

}  // namespace elv
