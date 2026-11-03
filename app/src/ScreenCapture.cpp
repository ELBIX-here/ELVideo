#include "ScreenCapture.h"

#include "AppSettings.h"
#include "OverlayGeometry.h"

#include "core/IRecorder.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QWindow>

#include <dwmapi.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <future>
#include <thread>

namespace {

QImage toQImage(const elv::Thumbnail& t) {
    return QImage(t.bgra.data(), static_cast<int>(t.width), static_cast<int>(t.height),
                  static_cast<int>(t.width * 4), QImage::Format_RGB32)
        .copy();
}

QString hex(std::uintptr_t v) { return QString::number(static_cast<qulonglong>(v), 16); }

}  // namespace

ScreenCapture::ScreenCapture(AppSettings& settings, QObject* parent) : QObject(parent), settings_(settings) {}

void ScreenCapture::setActive(bool active) {
    if (active == active_) return;
    active_ = active;
    emit activeChanged();
}

void ScreenCapture::setBusy(bool busy) {
    if (busy == busy_) return;
    busy_ = busy;
    emit busyChanged();
}

void ScreenCapture::start() {
    if (busy_ || active_) return;
    setBusy(true);
    const int delay = std::clamp(settings_.screenshotDelay, 0, 60);
    if (delay == 0) {
        freeze();
        return;
    }
    // Count down once per second, then freeze.
    auto remaining = std::make_shared<int>(delay);
    emit countdown(*remaining);
    auto* timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this, timer, remaining] {
        if (!busy_) {  // cancelled
            timer->deleteLater();
            return;
        }
        if (--*remaining > 0) {
            emit countdown(*remaining);
            return;
        }
        timer->deleteLater();
        emit countdown(0);
        freeze();
    });
    timer->start();
}

void ScreenCapture::freeze() {
    // Windows on screen, top to bottom, for "click a window" (ours excluded).
    windows_.clear();
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* list = reinterpret_cast<std::vector<WindowSnapshot>*>(param);
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == GetCurrentProcessId()) return TRUE;
            BOOL cloaked = FALSE;
            DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            if (cloaked) return TRUE;
            wchar_t cls[64]{};
            GetClassNameW(hwnd, cls, 64);
            const std::wstring_view c(cls);
            if (c == L"Progman" || c == L"WorkerW" || c == L"Shell_TrayWnd" || c == L"Shell_SecondaryTrayWnd")
                return TRUE;
            if ((GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) && GetWindowTextLengthW(hwnd) == 0) return TRUE;
            RECT r{};
            if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(hwnd, &r);
            if (r.right - r.left < 8 || r.bottom - r.top < 8) return TRUE;
            list->push_back({hwnd, r});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows_));

    const bool cursor = settings_.screenshotCursor;
    std::thread([this, cursor, guard = QPointer<ScreenCapture>(this)] {
        // All monitors at once: each capture waits for its first frame.
        std::vector<std::pair<elv::CaptureSource, std::future<std::optional<elv::Thumbnail>>>> jobs;
        for (const auto& s : elv::enumerateCaptureSources()) {
            if (s.kind != elv::SourceKind::Monitor) continue;
            jobs.emplace_back(s, std::async(std::launch::async, [s, cursor] {
                                  return elv::captureImage(s, std::nullopt, cursor);
                              }));
        }
        std::map<std::uintptr_t, Frozen> frozen;
        for (auto& [source, job] : jobs) {
            const auto image = job.get();
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (!image || !GetMonitorInfoW(reinterpret_cast<HMONITOR>(source.handle), &info)) continue;
            frozen[source.handle] = {toQImage(*image), info.rcMonitor};
        }
        QMetaObject::invokeMethod(qApp, [guard, frozen = std::move(frozen)]() mutable {
            if (!guard) return;
            guard->setBusy(false);
            if (frozen.empty()) {
                emit guard->failed(QObject::tr("화면을 캡처하지 못했습니다."));
                return;
            }
            {
                std::lock_guard lock(guard->mutex_);
                guard->frozen_ = std::move(frozen);
                ++guard->generation_;
            }
            guard->setActive(true);
        }, Qt::QueuedConnection);
    }).detach();
}

void ScreenCapture::cancel() {
    setActive(false);
    setBusy(false);
    std::lock_guard lock(mutex_);
    frozen_.clear();
}

QString ScreenCapture::frozenUrl(QWindow* overlay) const {
    const auto mapping = mapOverlay(overlay);
    if (!mapping) return {};
    return QStringLiteral("image://frozen/%1/%2").arg(generation_).arg(hex(reinterpret_cast<std::uintptr_t>(mapping->monitor)));
}

QImage ScreenCapture::frozenImage(const QString& id) const {
    bool ok = false;
    const auto monitor = static_cast<std::uintptr_t>(id.section(QLatin1Char('/'), 1, 1).toULongLong(&ok, 16));
    std::lock_guard lock(mutex_);
    const auto it = frozen_.find(monitor);
    return ok && it != frozen_.end() ? it->second.image : QImage();
}

const ScreenCapture::WindowSnapshot* ScreenCapture::windowAt(POINT p) const {
    for (const auto& w : windows_)
        if (PtInRect(&w.rect, p)) return &w;
    return nullptr;
}

QPointF ScreenCapture::windowCenter(QWindow* overlay, const QString& title) const {
    const auto mapping = mapOverlay(overlay);
    if (!mapping) return {-1, -1};
    for (const auto& w : windows_) {
        wchar_t text[256]{};
        GetWindowTextW(w.hwnd, text, 256);
        if (QString::fromWCharArray(text) != title) continue;
        const QRectF r = mapping->toOverlay(w.rect);
        if (r.center().x() < 0 || r.center().y() < 0 || r.center().x() > overlay->width() || r.center().y() > overlay->height())
            return {-1, -1};
        return r.center();
    }
    return {-1, -1};
}

QRectF ScreenCapture::windowRectAt(QWindow* overlay, const QPointF& pos) const {
    const auto mapping = mapOverlay(overlay);
    if (!mapping) return {};
    const auto* w = windowAt(mapping->toDesktop(pos));
    return w ? mapping->toOverlay(w->rect) : QRectF();
}

void ScreenCapture::captureRegion(QWindow* overlay, const QRectF& rect) {
    const auto mapping = mapOverlay(overlay);
    QImage image;
    if (mapping) {
        std::lock_guard lock(mutex_);
        const QRect r = mapping->toMonitor(rect);
        spdlog::info("screenshot region {},{} {}x{} (overlay {},{} {}x{})", r.x(), r.y(), r.width(), r.height(), rect.x(),
                     rect.y(), rect.width(), rect.height());
        if (const auto it = frozen_.find(reinterpret_cast<std::uintptr_t>(mapping->monitor)); it != frozen_.end())
            image = it->second.image.copy(r);
    }
    finish(std::move(image));
}

void ScreenCapture::captureMonitor(QWindow* overlay) {
    const auto mapping = mapOverlay(overlay);
    QImage image;
    if (mapping) {
        std::lock_guard lock(mutex_);
        if (const auto it = frozen_.find(reinterpret_cast<std::uintptr_t>(mapping->monitor)); it != frozen_.end())
            image = it->second.image;
    }
    finish(std::move(image));
}

void ScreenCapture::captureAll() {
    QImage image;
    {
        std::lock_guard lock(mutex_);
        if (!frozen_.empty()) {
            RECT bounds = frozen_.begin()->second.rect;
            for (const auto& [m, f] : frozen_) UnionRect(&bounds, &bounds, &f.rect);
            image = QImage(bounds.right - bounds.left, bounds.bottom - bounds.top, QImage::Format_RGB32);
            image.fill(Qt::black);  // gaps between monitors of different sizes
            QPainter painter(&image);
            for (const auto& [m, f] : frozen_)
                painter.drawImage(f.rect.left - bounds.left, f.rect.top - bounds.top, f.image);
        }
    }
    finish(std::move(image));
}

void ScreenCapture::captureWindowAt(QWindow* overlay, const QPointF& pos) {
    const auto mapping = mapOverlay(overlay);
    const POINT point = mapping ? mapping->toDesktop(pos) : POINT{};
    const auto* w = mapping ? windowAt(point) : nullptr;
    spdlog::info("screenshot click at {},{}: {}", point.x, point.y, w ? "window" : "desktop");
    if (!w) {
        captureMonitor(overlay);  // clicked the desktop
        return;
    }
    // The window itself through WGC: complete even where other windows covered it.
    const HWND hwnd = w->hwnd;
    const bool cursor = settings_.screenshotCursor;
    setActive(false);
    setBusy(true);
    std::thread([hwnd, cursor, guard = QPointer<ScreenCapture>(this)] {
        elv::CaptureSource source;
        source.kind = elv::SourceKind::Window;
        source.handle = reinterpret_cast<std::uintptr_t>(hwnd);
        const auto image = elv::captureImage(source, std::nullopt, cursor);
        QImage result = image ? toQImage(*image) : QImage();
        QMetaObject::invokeMethod(qApp, [guard, result = std::move(result)]() mutable {
            if (!guard) return;
            guard->setBusy(false);
            guard->finish(std::move(result));
        }, Qt::QueuedConnection);
    }).detach();
}

void ScreenCapture::finish(QImage image) {
    setActive(false);
    {
        std::lock_guard lock(mutex_);
        frozen_.clear();  // a few full-screen images; free them now
    }
    if (image.isNull() || image.width() < 2 || image.height() < 2) {
        emit failed(tr("캡처할 영역이 비어 있습니다."));
        return;
    }

    bool copied = false;
    if (settings_.screenshotCopy || !settings_.screenshotSave) {
        QGuiApplication::clipboard()->setImage(image);
        copied = true;
    }
    QString path;
    if (settings_.screenshotSave) {
        const bool jpg = settings_.screenshotFormat == QLatin1String("jpg");
        const QDir dir(settings_.effectiveScreenshotDir());
        QDir().mkpath(dir.absolutePath());
        const QString name =
            QStringLiteral("ELVideo_Capture_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
        const QString extension = jpg ? QStringLiteral(".jpg") : QStringLiteral(".png");
        path = dir.filePath(name + extension);
        for (int n = 2; QFileInfo::exists(path); ++n) path = dir.filePath(QStringLiteral("%1_%2%3").arg(name).arg(n).arg(extension));
        if (!image.save(path, jpg ? "JPG" : "PNG", jpg ? 92 : -1)) {
            spdlog::error("cannot save screenshot {}", path.toStdString());
            emit failed(tr("캡처를 저장하지 못했습니다: %1").arg(QDir::toNativeSeparators(path)));
            return;
        }
        path = QDir::toNativeSeparators(path);
        spdlog::info("screenshot saved: {} ({}x{})", path.toStdString(), image.width(), image.height());
    }
    emit captured(path, image.width(), image.height(), copied);
}

FrozenImageProvider::FrozenImageProvider(ScreenCapture& capture)
    : QQuickImageProvider(QQuickImageProvider::Image), capture_(capture) {}

QImage FrozenImageProvider::requestImage(const QString& id, QSize* size, const QSize&) {
    QImage image = capture_.frozenImage(id);
    if (size) *size = image.size();
    return image;
}
