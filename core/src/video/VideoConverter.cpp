#include "video/VideoConverter.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace elv {

VideoConverter::VideoConverter(const D3DDevice& device, std::uint32_t outputWidth, std::uint32_t outputHeight)
    : outputWidth_(outputWidth), outputHeight_(outputHeight) {
    winrt::com_ptr<ID3D11Device> d3d;
    d3d.copy_from(device.device());
    winrt::com_ptr<ID3D11DeviceContext> context;
    context.copy_from(device.context());

    videoDevice_ = d3d.try_as<ID3D11VideoDevice>();
    videoContext_ = context.try_as<ID3D11VideoContext>();
    if (!videoDevice_ || !videoContext_) throw std::runtime_error("GPU has no D3D11 video processor");

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = outputWidth;
    desc.Height = outputHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    winrt::check_hresult(d3d->CreateTexture2D(&desc, nullptr, output_.put()));
}

void VideoConverter::ensureProcessor(std::uint32_t inputWidth, std::uint32_t inputHeight) {
    if (processor_ && inputWidth == inputWidth_ && inputHeight == inputHeight_) return;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate = {60, 1};
    content.InputWidth = inputWidth;
    content.InputHeight = inputHeight;
    content.OutputFrameRate = {60, 1};
    content.OutputWidth = outputWidth_;
    content.OutputHeight = outputHeight_;
    content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;

    enumerator_ = nullptr;
    processor_ = nullptr;
    outputView_ = nullptr;
    winrt::check_hresult(videoDevice_->CreateVideoProcessorEnumerator(&content, enumerator_.put()));

    UINT inputSupport = 0;
    UINT outputSupport = 0;
    winrt::check_hresult(enumerator_->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &inputSupport));
    winrt::check_hresult(enumerator_->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &outputSupport));
    if (!(inputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) ||
        !(outputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT))
        throw std::runtime_error("video processor cannot convert BGRA to NV12");

    winrt::check_hresult(videoDevice_->CreateVideoProcessor(enumerator_.get(), 0, processor_.put()));

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC viewDesc{};
    viewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    winrt::check_hresult(
        videoDevice_->CreateVideoProcessorOutputView(output_.get(), enumerator_.get(), &viewDesc, outputView_.put()));

    auto* vp = processor_.get();
    if (const auto context1 = videoContext_.try_as<ID3D11VideoContext1>()) {
        context1->VideoProcessorSetStreamColorSpace1(vp, 0, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        context1->VideoProcessorSetOutputColorSpace1(vp, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
    } else {
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE input{};
        input.YCbCr_Matrix = 1;  // BT.709
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE output{};
        output.YCbCr_Matrix = 1;
        output.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        videoContext_->VideoProcessorSetStreamColorSpace(vp, 0, &input);
        videoContext_->VideoProcessorSetOutputColorSpace(vp, &output);
    }
    videoContext_->VideoProcessorSetStreamFrameFormat(vp, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    videoContext_->VideoProcessorSetStreamAutoProcessingMode(vp, 0, FALSE);

    // Aspect-fit into the output; the background fills the bars.
    const double scale = std::min(static_cast<double>(outputWidth_) / inputWidth,
                                  static_cast<double>(outputHeight_) / inputHeight);
    const LONG width = std::max<LONG>(2, std::lround(inputWidth * scale) & ~1L);
    const LONG height = std::max<LONG>(2, std::lround(inputHeight * scale) & ~1L);
    const LONG x = ((static_cast<LONG>(outputWidth_) - width) / 2) & ~1L;
    const LONG y = ((static_cast<LONG>(outputHeight_) - height) / 2) & ~1L;
    const RECT sourceRect{0, 0, static_cast<LONG>(inputWidth), static_cast<LONG>(inputHeight)};
    const RECT destRect{x, y, x + width, y + height};
    videoContext_->VideoProcessorSetStreamSourceRect(vp, 0, TRUE, &sourceRect);
    videoContext_->VideoProcessorSetStreamDestRect(vp, 0, TRUE, &destRect);

    D3D11_VIDEO_COLOR black{};
    black.RGBA = {0.0f, 0.0f, 0.0f, 1.0f};
    videoContext_->VideoProcessorSetOutputBackgroundColor(vp, FALSE, &black);

    if (inputWidth_ != 0)
        spdlog::info("converter input {}x{} -> {}x{} (placed at {},{} {}x{})", inputWidth, inputHeight, outputWidth_,
                     outputHeight_, x, y, width, height);
    inputWidth_ = inputWidth;
    inputHeight_ = inputHeight;
}

void VideoConverter::convert(ID3D11Texture2D* source, std::uint32_t width, std::uint32_t height) {
    ensureProcessor(width, height);

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC viewDesc{};
    viewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    winrt::com_ptr<ID3D11VideoProcessorInputView> inputView;
    winrt::check_hresult(
        videoDevice_->CreateVideoProcessorInputView(source, enumerator_.get(), &viewDesc, inputView.put()));

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.get();
    winrt::check_hresult(videoContext_->VideoProcessorBlt(processor_.get(), outputView_.get(), 0, 1, &stream));
}

}  // namespace elv
