#pragma once

#include <QRect>
#include <QRectF>

#include <windows.h>

#include <optional>

class QWindow;

// Maps between a full-monitor overlay window's Qt (logical) coordinates and the
// physical pixels of the monitor it covers. Used by the region and screenshot overlays.
struct OverlayMapping {
    HMONITOR monitor = nullptr;
    RECT monitorRect{};  // physical, virtual-desktop coordinates
    RECT windowRect{};   // the overlay, physical
    double scale = 1.0;  // physical pixels per logical unit

    // A logical overlay rectangle in physical pixels relative to the monitor's top-left, clipped to it.
    QRect toMonitor(const QRectF& logical) const;
    // A physical virtual-desktop rectangle in overlay coordinates.
    QRectF toOverlay(const RECT& physical) const;
    // A logical overlay point in physical virtual-desktop coordinates.
    POINT toDesktop(const QPointF& logical) const;
};

std::optional<OverlayMapping> mapOverlay(QWindow* overlay);
