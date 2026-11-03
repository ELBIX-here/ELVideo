#pragma once

#include "util/D3DDevice.h"

#include <cstdint>

namespace elv {

// Converts captured BGRA frames to a fixed-size NV12 texture (BT.709, limited
// range) on the GPU. Sources of a different aspect ratio are letterboxed.
class VideoConverter {
public:
    VideoConverter(const D3DDevice& device, std::uint32_t outputWidth, std::uint32_t outputHeight);

    // Converts the top-left width x height region of a BGRA texture into output().
    void convert(ID3D11Texture2D* source, std::uint32_t width, std::uint32_t height);

    ID3D11Texture2D* output() const { return output_.get(); }
    std::uint32_t outputWidth() const { return outputWidth_; }
    std::uint32_t outputHeight() const { return outputHeight_; }

private:
    void ensureProcessor(std::uint32_t inputWidth, std::uint32_t inputHeight);

    std::uint32_t outputWidth_;
    std::uint32_t outputHeight_;
    std::uint32_t inputWidth_ = 0;
    std::uint32_t inputHeight_ = 0;

    winrt::com_ptr<ID3D11VideoDevice> videoDevice_;
    winrt::com_ptr<ID3D11VideoContext> videoContext_;
    winrt::com_ptr<ID3D11VideoProcessorEnumerator> enumerator_;
    winrt::com_ptr<ID3D11VideoProcessor> processor_;
    winrt::com_ptr<ID3D11Texture2D> output_;
    winrt::com_ptr<ID3D11VideoProcessorOutputView> outputView_;
};

}  // namespace elv
