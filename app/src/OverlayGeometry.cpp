#include "OverlayGeometry.h"

#include <QWindow>

#include <algorithm>
#include <cmath>

std::optional<OverlayMapping> mapOverlay(QWindow* overlay) {
    if (!overlay || overlay->width() <= 0) return std::nullopt;
    const auto hwnd = reinterpret_cast<HWND>(overlay->winId());
    OverlayMapping m;
    m.monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(m.monitor, &info) || !GetWindowRect(hwnd, &m.windowRect)) return std::nullopt;
    m.monitorRect = info.rcMonitor;
    m.scale = static_cast<double>(m.windowRect.right - m.windowRect.left) / overlay->width();
    return m;
}

QRect OverlayMapping::toMonitor(const QRectF& logical) const {
    const QRectF r = logical.normalized();
    const int width = monitorRect.right - monitorRect.left;
    const int height = monitorRect.bottom - monitorRect.top;
    const int offsetX = windowRect.left - monitorRect.left;
    const int offsetY = windowRect.top - monitorRect.top;
    const int left = std::clamp(static_cast<int>(std::lround(r.left() * scale)) + offsetX, 0, width);
    const int top = std::clamp(static_cast<int>(std::lround(r.top() * scale)) + offsetY, 0, height);
    const int right = std::clamp(static_cast<int>(std::lround(r.right() * scale)) + offsetX, left, width);
    const int bottom = std::clamp(static_cast<int>(std::lround(r.bottom() * scale)) + offsetY, top, height);
    return QRect(left, top, right - left, bottom - top);
}

QRectF OverlayMapping::toOverlay(const RECT& physical) const {
    return QRectF((physical.left - windowRect.left) / scale, (physical.top - windowRect.top) / scale,
                  (physical.right - physical.left) / scale, (physical.bottom - physical.top) / scale);
}

POINT OverlayMapping::toDesktop(const QPointF& logical) const {
    return {windowRect.left + static_cast<LONG>(std::lround(logical.x() * scale)),
            windowRect.top + static_cast<LONG>(std::lround(logical.y() * scale))};
}
