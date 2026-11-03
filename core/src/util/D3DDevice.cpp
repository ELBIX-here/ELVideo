#include "util/D3DDevice.h"

#include <windows.graphics.directx.direct3d11.interop.h>

#include <spdlog/spdlog.h>

namespace elv {

namespace {

winrt::com_ptr<IDXGIAdapter1> findAdapterForMonitor(HMONITOR monitor) {
    if (!monitor) return nullptr;

    winrt::com_ptr<IDXGIFactory1> factory;
    winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(factory.put())));

    winrt::com_ptr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, adapter.put()) != DXGI_ERROR_NOT_FOUND; ++a) {
        winrt::com_ptr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, output.put()) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc{};
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) return adapter;
            output = nullptr;
        }
        adapter = nullptr;
    }
    return nullptr;
}

HRESULT createDevice(IDXGIAdapter1* adapter, UINT flags, ID3D11Device** device,
                     ID3D11DeviceContext** context) {
    static constexpr D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    return D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                             nullptr, flags, kLevels, ARRAYSIZE(kLevels), D3D11_SDK_VERSION, device,
                             nullptr, context);
}

}  // namespace

D3DDevice::D3DDevice(HMONITOR monitor) {
    const auto adapter = findAdapterForMonitor(monitor);

    // VIDEO_SUPPORT is needed later for ID3D11VideoProcessor; retry without it
    // so capture still works on adapters that lack it.
    constexpr UINT kBaseFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = createDevice(adapter.get(), kBaseFlags | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                              device_.put(), context_.put());
    if (FAILED(hr)) {
        spdlog::warn("D3D11 device without video support (0x{:08X})", static_cast<unsigned>(hr));
        hr = createDevice(adapter.get(), kBaseFlags, device_.put(), context_.put());
    }
    winrt::check_hresult(hr);

    device_.as<ID3D11Multithread>()->SetMultithreadProtected(TRUE);

    const auto dxgiDevice = device_.as<IDXGIDevice>();
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    winrtDevice_ = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

    winrt::com_ptr<IDXGIAdapter> usedAdapter;
    DXGI_ADAPTER_DESC desc{};
    if (SUCCEEDED(dxgiDevice->GetAdapter(usedAdapter.put())) && SUCCEEDED(usedAdapter->GetDesc(&desc)))
        adapterName_ = desc.Description;
}

}  // namespace elv
