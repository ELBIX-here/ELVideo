#include "util/ImageIO.h"

#include <wincodec.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace elv {

std::vector<std::uint8_t> readTexturePixels(const D3DDevice& device, ID3D11Texture2D* texture, std::uint32_t width,
                                   std::uint32_t height) {
    D3D11_TEXTURE2D_DESC srcDesc{};
    texture->GetDesc(&srcDesc);
    if (srcDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) throw std::runtime_error("expected a BGRA texture");

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = srcDesc.Format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    winrt::com_ptr<ID3D11Texture2D> staging;
    winrt::check_hresult(device.device()->CreateTexture2D(&desc, nullptr, staging.put()));

    auto* context = device.context();
    const D3D11_BOX box{0, 0, 0, width, height, 1};
    context->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, texture, 0, &box);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    winrt::check_hresult(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));

    const std::uint32_t stride = width * 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint8_t* row = pixels.data() + static_cast<std::size_t>(y) * stride;
        std::memcpy(row, static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                    stride);
        // Captured alpha is not meaningful; force opaque.
        for (std::uint32_t x = 3; x < stride; x += 4) row[x] = 0xFF;
    }
    context->Unmap(staging.get(), 0);
    return pixels;
}

void saveTextureAsPng(const D3DDevice& device, ID3D11Texture2D* texture, std::uint32_t width,
                      std::uint32_t height, const std::filesystem::path& path) {
    const auto pixels = readTexturePixels(device, texture, width, height);
    savePixelsAsPng(pixels.data(), width, height, width * 4, path);
}

void savePixelsAsPng(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t stride,
                     const std::filesystem::path& path) {
    const auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);

    winrt::com_ptr<IWICStream> stream;
    winrt::check_hresult(factory->CreateStream(stream.put()));
    winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));

    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));

    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
    winrt::check_hresult(frame->Initialize(nullptr));
    winrt::check_hresult(frame->SetSize(width, height));

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    winrt::check_hresult(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA) throw std::runtime_error("PNG encoder rejected BGRA");

    winrt::check_hresult(frame->WritePixels(height, stride, stride * height, const_cast<BYTE*>(pixels)));
    winrt::check_hresult(frame->Commit());
    winrt::check_hresult(encoder->Commit());
}

}  // namespace elv
