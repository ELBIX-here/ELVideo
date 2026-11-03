#pragma once

#include "core/Types.h"
#include "util/D3DDevice.h"

#include <cstdint>
#include <optional>

namespace elv {

// Scales the encoder's NV12 frame down to BGRA on the GPU and reads it back, for a
// live preview of exactly what is being recorded. Not thread-safe; one caller at a time.
class PreviewRenderer {
public:
    PreviewRenderer(const D3DDevice& device, std::uint32_t sourceWidth, std::uint32_t sourceHeight);

    std::optional<Thumbnail> render(ID3D11Texture2D* nv12, std::uint32_t maxWidth, std::uint32_t maxHeight);

private:
    void ensure(std::uint32_t width, std::uint32_t height);

    const D3DDevice& device_;
    const std::uint32_t sourceWidth_;
    const std::uint32_t sourceHeight_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;

    winrt::com_ptr<ID3D11VideoDevice> videoDevice_;
    winrt::com_ptr<ID3D11VideoContext> videoContext_;
    winrt::com_ptr<ID3D11VideoProcessorEnumerator> enumerator_;
    winrt::com_ptr<ID3D11VideoProcessor> processor_;
    winrt::com_ptr<ID3D11Texture2D> target_;
    winrt::com_ptr<ID3D11Texture2D> staging_;
    winrt::com_ptr<ID3D11VideoProcessorOutputView> outputView_;
};

}  // namespace elv
