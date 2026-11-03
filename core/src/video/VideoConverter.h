#pragma once

#include "util/D3DDevice.h"

#include <cstdint>
#include <optional>

namespace elv {

// Converts captured BGRA frames to a fixed-size NV12 texture (BT.709, limited
// range) on the GPU. Sources of a different aspect ratio are letterboxed.
// With a crop rectangle only that part of each frame is used.
class VideoConverter {
public:
    VideoConverter(const D3DDevice& device, std::uint32_t outputWidth, std::uint32_t outputHeight,
                   std::optional<RECT> crop = std::nullopt);

    // Converts the top-left width x height region of a BGRA texture into output().
    void convert(ID3D11Texture2D* source, std::uint32_t width, std::uint32_t height);

    // Picture-in-picture: an opaque BGRA image drawn over a corner of the output by
    // the same video-processor pass (second input stream).
    struct OverlayPlacement {
        int corner = 3;  // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
        float widthFraction = 0.22f;
        bool mirror = false;
    };
    void setOverlayPlacement(const OverlayPlacement& placement) { placement_ = placement; }
    // Replaces the overlay image (top-down BGRA, stride width * 4).
    void updateOverlay(const std::uint8_t* bgra, std::uint32_t width, std::uint32_t height);
    // False when the GPU's video processor has a single input stream (no overlay).
    bool overlaySupported() const { return multiStream_; }

    ID3D11Texture2D* output() const { return output_.get(); }
    std::uint32_t outputWidth() const { return outputWidth_; }
    std::uint32_t outputHeight() const { return outputHeight_; }

private:
    void ensureProcessor(std::uint32_t inputWidth, std::uint32_t inputHeight);
    RECT overlayRect() const;

    std::uint32_t outputWidth_;
    std::uint32_t outputHeight_;
    std::uint32_t inputWidth_ = 0;
    std::uint32_t inputHeight_ = 0;
    std::optional<RECT> crop_;

    winrt::com_ptr<ID3D11VideoDevice> videoDevice_;
    winrt::com_ptr<ID3D11VideoContext> videoContext_;
    winrt::com_ptr<ID3D11VideoProcessorEnumerator> enumerator_;
    winrt::com_ptr<ID3D11VideoProcessor> processor_;
    winrt::com_ptr<ID3D11Texture2D> output_;
    winrt::com_ptr<ID3D11VideoProcessorOutputView> outputView_;

    std::optional<OverlayPlacement> placement_;
    winrt::com_ptr<ID3D11Texture2D> overlay_;
    winrt::com_ptr<ID3D11VideoProcessorInputView> overlayView_;  // tied to enumerator_
    std::uint32_t overlayWidth_ = 0;
    std::uint32_t overlayHeight_ = 0;
    bool multiStream_ = true;
};

}  // namespace elv
