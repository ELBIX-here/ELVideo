#pragma once

#include <unknwn.h>  // must precede winrt/base.h for classic COM interop

#include <d3d11_4.h>
#include <dxgi1_6.h>

#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <string>

namespace elv {

// D3D11 device shared by capture, conversion and encoding. Multithread
// protection is on, so the immediate context may be used from several threads.
class D3DDevice {
public:
    // Creates the device on the adapter that drives `monitor` so captured
    // textures never cross GPUs. nullptr selects the default adapter.
    explicit D3DDevice(HMONITOR monitor = nullptr);

    ID3D11Device* device() const { return device_.get(); }
    ID3D11DeviceContext* context() const { return context_.get(); }
    const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice& winrtDevice() const {
        return winrtDevice_;
    }
    const std::wstring& adapterName() const { return adapterName_; }

private:
    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice_{nullptr};
    std::wstring adapterName_;
};

}  // namespace elv
