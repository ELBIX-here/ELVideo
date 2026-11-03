#include "video/PreviewRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace elv {

PreviewRenderer::PreviewRenderer(const D3DDevice& device, std::uint32_t sourceWidth, std::uint32_t sourceHeight)
    : device_(device), sourceWidth_(sourceWidth), sourceHeight_(sourceHeight) {
    winrt::com_ptr<ID3D11Device> d3d;
    d3d.copy_from(device.device());
    winrt::com_ptr<ID3D11DeviceContext> context;
    context.copy_from(device.context());
    videoDevice_ = d3d.try_as<ID3D11VideoDevice>();
    videoContext_ = context.try_as<ID3D11VideoContext>();
    if (!videoDevice_ || !videoContext_) throw std::runtime_error("GPU has no D3D11 video processor");
}

void PreviewRenderer::ensure(std::uint32_t width, std::uint32_t height) {
    if (processor_ && width == width_ && height == height_) return;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate = {30, 1};
    content.InputWidth = sourceWidth_;
    content.InputHeight = sourceHeight_;
    content.OutputFrameRate = {30, 1};
    content.OutputWidth = width;
    content.OutputHeight = height;
    content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;

    enumerator_ = nullptr;
    processor_ = nullptr;
    outputView_ = nullptr;
    target_ = nullptr;
    staging_ = nullptr;
    winrt::check_hresult(videoDevice_->CreateVideoProcessorEnumerator(&content, enumerator_.put()));
    winrt::check_hresult(videoDevice_->CreateVideoProcessor(enumerator_.get(), 0, processor_.put()));

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    winrt::check_hresult(device_.device()->CreateTexture2D(&desc, nullptr, target_.put()));
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    winrt::check_hresult(device_.device()->CreateTexture2D(&desc, nullptr, staging_.put()));

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC viewDesc{};
    viewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    winrt::check_hresult(
        videoDevice_->CreateVideoProcessorOutputView(target_.get(), enumerator_.get(), &viewDesc, outputView_.put()));

    auto* vp = processor_.get();
    if (const auto context1 = videoContext_.try_as<ID3D11VideoContext1>()) {
        context1->VideoProcessorSetStreamColorSpace1(vp, 0, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
        context1->VideoProcessorSetOutputColorSpace1(vp, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    }
    videoContext_->VideoProcessorSetStreamFrameFormat(vp, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    videoContext_->VideoProcessorSetStreamAutoProcessingMode(vp, 0, FALSE);
    width_ = width;
    height_ = height;
}

std::optional<Thumbnail> PreviewRenderer::render(ID3D11Texture2D* nv12, std::uint32_t maxWidth,
                                                 std::uint32_t maxHeight) {
    if (!nv12 || maxWidth < 2 || maxHeight < 2) return std::nullopt;
    const double scale = std::min({1.0, static_cast<double>(maxWidth) / sourceWidth_,
                                   static_cast<double>(maxHeight) / sourceHeight_});
    const auto width = std::max(2u, static_cast<std::uint32_t>(std::lround(sourceWidth_ * scale)) & ~1u);
    const auto height = std::max(2u, static_cast<std::uint32_t>(std::lround(sourceHeight_ * scale)) & ~1u);
    ensure(width, height);

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
    inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    winrt::com_ptr<ID3D11VideoProcessorInputView> inputView;
    winrt::check_hresult(videoDevice_->CreateVideoProcessorInputView(nv12, enumerator_.get(), &inputDesc, inputView.put()));
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.get();
    winrt::check_hresult(videoContext_->VideoProcessorBlt(processor_.get(), outputView_.get(), 0, 1, &stream));

    auto* context = device_.context();
    context->CopyResource(staging_.get(), target_.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    winrt::check_hresult(context->Map(staging_.get(), 0, D3D11_MAP_READ, 0, &mapped));
    Thumbnail out;
    out.width = width;
    out.height = height;
    out.bgra.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y)
        std::memcpy(out.bgra.data() + static_cast<std::size_t>(y) * width * 4,
                    static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                    static_cast<std::size_t>(width) * 4);
    context->Unmap(staging_.get(), 0);
    for (std::size_t i = 3; i < out.bgra.size(); i += 4) out.bgra[i] = 0xFF;
    return out;
}

}  // namespace elv
