#include "core/IRecorder.h"

#include "elv_version.h"

#include <windows.h>

#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>

#include <format>

namespace elv {

const char* version() { return ELV_VERSION_STRING; }

PlatformInfo queryPlatform() {
    using namespace winrt::Windows::Foundation::Metadata;
    using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;

    PlatformInfo info;
    try {
        info.captureSupported = GraphicsCaptureSession::IsSupported();
        info.borderlessCaptureSupported = ApiInformation::IsPropertyPresent(
            L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired");
    } catch (const winrt::hresult_error&) {
        info.captureSupported = false;
    }

    // RtlGetVersion is not affected by the manifest-based version lie of GetVersionEx.
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (auto fn = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) {
        RTL_OSVERSIONINFOW v{sizeof(v)};
        if (fn(&v) == 0)
            info.windowsBuild = std::format("{}.{}.{}", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    }
    return info;
}

}  // namespace elv
