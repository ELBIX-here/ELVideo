#pragma once

#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QRectF>
#include <QString>

#include <windows.h>

#include <map>
#include <mutex>
#include <vector>

class AppSettings;
class QWindow;

// Screenshots. start() captures every monitor first ("freezes" the screen), then
// the overlay (ScreenshotOverlay.qml) shows those images and the user drags a
// region, clicks a window, or picks a whole monitor / all monitors. The result
// is copied to the clipboard and/or saved, per the settings.
class ScreenCapture : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)  // the overlay is up
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)        // waiting for the delay or the capture

public:
    explicit ScreenCapture(AppSettings& settings, QObject* parent = nullptr);

    bool active() const { return active_; }
    bool busy() const { return busy_; }

    Q_INVOKABLE void start();  // after the configured delay
    Q_INVOKABLE void cancel();

    // For overlays: the frozen image of the monitor under `overlay`.
    Q_INVOKABLE QString frozenUrl(QWindow* overlay) const;
    // The topmost window at `pos` (overlay coordinates) when the screen was frozen,
    // in overlay coordinates; an empty rect if none.
    Q_INVOKABLE QRectF windowRectAt(QWindow* overlay, const QPointF& pos) const;

    Q_INVOKABLE void captureRegion(QWindow* overlay, const QRectF& rect);
    Q_INVOKABLE void captureWindowAt(QWindow* overlay, const QPointF& pos);
    Q_INVOKABLE void captureMonitor(QWindow* overlay);
    Q_INVOKABLE void captureAll();

    QImage frozenImage(const QString& id) const;  // for the image provider

    // Test hook (scripts/test/shot_test.ps1): the centre of the frozen-time window
    // titled `title`, in overlay coordinates; (-1, -1) if it is not on this overlay.
    Q_INVOKABLE QPointF windowCenter(QWindow* overlay, const QString& title) const;

signals:
    void activeChanged();
    void busyChanged();
    // path is empty when the image was only copied to the clipboard.
    void captured(const QString& path, int width, int height, bool copied);
    void failed(const QString& message);
    void countdown(int seconds);

private:
    struct Frozen {
        QImage image;
        RECT rect{};  // physical, virtual desktop
    };
    struct WindowSnapshot {
        HWND hwnd = nullptr;
        RECT rect{};  // physical, visible frame bounds
    };

    void freeze();
    void finish(QImage image);
    void setActive(bool active);
    void setBusy(bool busy);
    const WindowSnapshot* windowAt(POINT p) const;

    AppSettings& settings_;
    bool active_ = false;
    bool busy_ = false;
    int generation_ = 0;
    mutable std::mutex mutex_;  // frozen_ is read by the image provider on the render thread
    std::map<std::uintptr_t, Frozen> frozen_;  // by HMONITOR
    std::vector<WindowSnapshot> windows_;      // top to bottom
};

// image://frozen/<generation>/<hmonitor hex>
class FrozenImageProvider : public QQuickImageProvider {
public:
    explicit FrozenImageProvider(ScreenCapture& capture);
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    ScreenCapture& capture_;
};
