#include "core/IRecorder.h"

#include <windows.h>

#include <dwmapi.h>

#include <algorithm>
#include <filesystem>
#include <format>

namespace elv {

namespace {

std::wstring processNameOf(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};

    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    std::wstring name;
    if (QueryFullProcessImageNameW(process, 0, path, &size))
        name = std::filesystem::path(path).filename().wstring();
    CloseHandle(process);
    return name;
}

bool isCapturableWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) return false;

    const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const auto exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((style & (WS_CHILD | WS_DISABLED)) || (exStyle & WS_EX_TOOLWINDOW)) return false;

    // UWP frames for suspended apps and windows on other virtual desktops are cloaked.
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return false;

    if (GetWindowTextLengthW(hwnd) == 0) return false;

    wchar_t className[64]{};
    GetClassNameW(hwnd, className, ARRAYSIZE(className));
    if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"Shell_TrayWnd") == 0) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid != GetCurrentProcessId();
}

BOOL CALLBACK onMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    auto& sources = *reinterpret_cast<std::vector<CaptureSource>*>(param);

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;

    const bool primary = info.dwFlags & MONITORINFOF_PRIMARY;
    CaptureSource source;
    source.kind = SourceKind::Monitor;
    source.handle = reinterpret_cast<std::uintptr_t>(monitor);
    source.width = info.rcMonitor.right - info.rcMonitor.left;
    source.height = info.rcMonitor.bottom - info.rcMonitor.top;
    source.title = std::format(L"{}{}", info.szDevice, primary ? L" (primary)" : L"");
    sources.insert(primary ? sources.begin() : sources.end(), std::move(source));
    return TRUE;
}

BOOL CALLBACK onWindow(HWND hwnd, LPARAM param) {
    if (!isCapturableWindow(hwnd)) return TRUE;
    auto& sources = *reinterpret_cast<std::vector<CaptureSource>*>(param);

    std::wstring title(GetWindowTextLengthW(hwnd) + 1, L'\0');
    title.resize(GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size())));

    RECT bounds{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))))
        GetWindowRect(hwnd, &bounds);

    CaptureSource source;
    source.kind = SourceKind::Window;
    source.handle = reinterpret_cast<std::uintptr_t>(hwnd);
    source.title = std::move(title);
    source.processName = processNameOf(hwnd);
    source.width = bounds.right - bounds.left;
    source.height = bounds.bottom - bounds.top;
    sources.push_back(std::move(source));
    return TRUE;
}

}  // namespace

std::vector<CaptureSource> enumerateCaptureSources() {
    std::vector<CaptureSource> monitors;
    EnumDisplayMonitors(nullptr, nullptr, onMonitor, reinterpret_cast<LPARAM>(&monitors));

    std::vector<CaptureSource> windows;
    EnumWindows(onWindow, reinterpret_cast<LPARAM>(&windows));

    monitors.insert(monitors.end(), std::make_move_iterator(windows.begin()),
                    std::make_move_iterator(windows.end()));
    return monitors;
}

}  // namespace elv
