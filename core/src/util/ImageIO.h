#pragma once

#include "util/D3DDevice.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace elv {

// Copies the top-left width x height region of a BGRA texture to the CPU as
// top-down pixels with stride width * 4 and opaque alpha. Blocks on the GPU copy.
std::vector<std::uint8_t> readTexturePixels(const D3DDevice& device, ID3D11Texture2D* texture, std::uint32_t width,
                                            std::uint32_t height);

// Copies the top-left width x height region of a BGRA texture to the CPU and
// writes it as an opaque PNG. Blocks until the GPU copy completes. Requires COM.
void saveTextureAsPng(const D3DDevice& device, ID3D11Texture2D* texture, std::uint32_t width,
                      std::uint32_t height, const std::filesystem::path& path);

// Writes top-down 32-bit BGRA/BGRX pixels as a PNG. Requires COM.
void savePixelsAsPng(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t stride,
                     const std::filesystem::path& path);

}  // namespace elv
