#include "SourceModel.h"

#include "OverlayGeometry.h"

#include "core/IRecorder.h"

#include <QImage>
#include <QRunnable>

#include <windows.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Friendly monitor name ("DELL U2720Q") for a GDI device name ("\\.\DISPLAY1").
QString monitorFriendlyName(HMONITOR monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return {};

    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return {};
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
        ERROR_SUCCESS)
        return {};

    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        if (wcscmp(source.viewGdiDeviceName, info.szDevice) != 0) continue;

        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = paths[i].targetInfo.adapterId;
        target.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS && target.monitorFriendlyDeviceName[0])
            return QString::fromWCharArray(target.monitorFriendlyDeviceName);
    }
    return {};
}

bool isPrimary(HMONITOR monitor) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    return GetMonitorInfoW(monitor, &info) && (info.dwFlags & MONITORINFOF_PRIMARY);
}

QString sizeText(const elv::CaptureSource& s) {
    return QStringLiteral("%1×%2").arg(s.width).arg(s.height);
}

}  // namespace

SourceModel::SourceModel(QObject* parent) : QAbstractListModel(parent) {}

QString SourceModel::idOf(const elv::CaptureSource& source) {
    return QStringLiteral("%1:%2")
        .arg(source.kind == elv::SourceKind::Monitor ? QLatin1Char('m') : QLatin1Char('w'))
        .arg(static_cast<qulonglong>(source.handle), 0, 16);
}

std::optional<elv::CaptureSource> SourceModel::parseId(const QString& id) {
    if (id.size() < 3 || id[1] != QLatin1Char(':')) return std::nullopt;
    bool ok = false;
    const qulonglong handle = id.mid(2).toULongLong(&ok, 16);
    if (!ok || handle == 0) return std::nullopt;
    elv::CaptureSource source;
    if (id[0] == QLatin1Char('m'))
        source.kind = elv::SourceKind::Monitor;
    else if (id[0] == QLatin1Char('w'))
        source.kind = elv::SourceKind::Window;
    else
        return std::nullopt;
    source.handle = static_cast<std::uintptr_t>(handle);
    return source;
}

void SourceModel::refresh() {
    std::vector<Entry> fresh;
    int monitorNumber = 0;
    for (auto& source : elv::enumerateCaptureSources()) {
        Entry entry;
        entry.id = idOf(source);
        if (source.kind == elv::SourceKind::Monitor) {
            const auto monitor = reinterpret_cast<HMONITOR>(source.handle);
            entry.title = tr("디스플레이 %1").arg(++monitorNumber);
            QStringList parts;
            if (const auto name = monitorFriendlyName(monitor); !name.isEmpty()) parts << name;
            parts << sizeText(source);
            if (isPrimary(monitor)) parts << tr("주 모니터");
            entry.subtitle = parts.join(QStringLiteral(" · "));
        } else {
            entry.title = QString::fromStdWString(source.title);
            entry.subtitle = QString::fromStdWString(source.processName) + QStringLiteral(" · ") + sizeText(source);
        }
        entry.source = std::move(source);
        fresh.push_back(std::move(entry));
    }
    ++generation_;

    // Same sources in the same order: update in place so the cards stay and only
    // their thumbnails reload. Otherwise rebuild the list.
    const bool sameIds = std::equal(fresh.begin(), fresh.end(), all_.begin(), all_.end(),
                                    [](const Entry& a, const Entry& b) { return a.id == b.id; });
    if (sameIds) {
        all_ = std::move(fresh);
        if (!visible_.empty()) emit dataChanged(index(0), index(static_cast<int>(visible_.size()) - 1));
    } else {
        beginResetModel();
        all_ = std::move(fresh);
        rebuildVisible();
        endResetModel();
        emit countsChanged();
    }

    // Keep the choice across refreshes; fall back to the primary monitor.
    if (!find(selectedId_)) selectedId_ = all_.empty() ? QString() : all_.front().id;
    emit selectionChanged();  // the title may have changed too
    emit regionChanged();     // new thumbnail generation; the region's monitor may be gone
}

void SourceModel::rebuildVisible() {
    visible_.clear();
    const auto wanted = showWindows_ ? elv::SourceKind::Window : elv::SourceKind::Monitor;
    for (int i = 0; i < static_cast<int>(all_.size()); ++i)
        if (all_[i].source.kind == wanted) visible_.push_back(i);
}

const SourceModel::Entry* SourceModel::find(const QString& id) const {
    if (id.isEmpty()) return nullptr;
    for (const auto& entry : all_)
        if (entry.id == id) return &entry;
    return nullptr;
}

void SourceModel::setShowWindows(bool show) {
    if (show == showWindows_) return;
    beginResetModel();
    showWindows_ = show;
    rebuildVisible();
    endResetModel();
    emit showWindowsChanged();
}

void SourceModel::setSelectedId(const QString& id) {
    if (id == selectedId_) return;
    selectedId_ = id;
    if (!visible_.empty()) emit dataChanged(index(0), index(static_cast<int>(visible_.size()) - 1), {SelectedRole});
    emit selectionChanged();
}

QString SourceModel::selectedTitle() const {
    const auto* entry = find(selectedId_);
    return entry ? entry->title : QString();
}

bool SourceModel::setRegionFromOverlay(QWindow* overlay, const QRectF& rect) {
    const auto mapping = mapOverlay(overlay);
    if (!mapping) return false;
    const QRect r = mapping->toMonitor(rect);
    // Even sizes: NV12 and H.264 need them.
    const int width = r.width() & ~1;
    const int height = r.height() & ~1;
    if (width < 16 || height < 16) return false;

    elv::CaptureSource source;
    source.kind = elv::SourceKind::Monitor;
    source.handle = reinterpret_cast<std::uintptr_t>(mapping->monitor);
    regionMonitorId_ = idOf(source);
    region_ = {r.x(), r.y(), width, height};
    const RECT physical{mapping->monitorRect.left + r.x(), mapping->monitorRect.top + r.y(),
                        mapping->monitorRect.left + r.x() + width, mapping->monitorRect.top + r.y() + height};
    regionGeometry_ = mapping->toOverlay(physical).translated(overlay->x(), overlay->y());
    regionMode_ = true;
    ++generation_;
    emit regionChanged();
    emit selectionChanged();
    return true;
}

void SourceModel::setRegionMode(bool on) {
    if (on == regionMode_) return;
    regionMode_ = on;
    emit regionChanged();
    emit selectionChanged();
}

bool SourceModel::hasRegion() const { return find(regionMonitorId_) != nullptr; }

QString SourceModel::regionTitle() const {
    const auto* monitor = find(regionMonitorId_);
    if (!monitor) return {};
    return tr("영역 %1×%2 · %3").arg(region_.width).arg(region_.height).arg(monitor->title);
}

QString SourceModel::regionThumbnail() const {
    if (!hasRegion()) return {};
    return QStringLiteral("image://thumbnail/%1/%2/%3,%4,%5,%6")
        .arg(generation_)
        .arg(regionMonitorId_)
        .arg(region_.x)
        .arg(region_.y)
        .arg(region_.width)
        .arg(region_.height);
}

std::optional<std::pair<elv::CaptureSource, elv::CaptureRegion>> SourceModel::activeRegion() const {
    if (!regionMode_) return std::nullopt;
    const auto* monitor = find(regionMonitorId_);
    if (!monitor) return std::nullopt;
    return std::make_pair(monitor->source, region_);
}

QString SourceModel::recordingTitle() const {
    if (regionMode_) return regionTitle();
    return selectedTitle();
}

int SourceModel::monitorCount() const {
    return static_cast<int>(std::count_if(all_.begin(), all_.end(), [](const Entry& e) {
        return e.source.kind == elv::SourceKind::Monitor;
    }));
}

int SourceModel::windowCount() const { return static_cast<int>(all_.size()) - monitorCount(); }

std::optional<elv::CaptureSource> SourceModel::selectedSource() const {
    if (const auto* entry = find(selectedId_)) return entry->source;
    for (const auto& entry : all_)
        if (entry.source.kind == elv::SourceKind::Monitor) return entry.source;
    return std::nullopt;
}

int SourceModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

QVariant SourceModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(visible_.size())) return {};
    const Entry& entry = all_[visible_[index.row()]];
    switch (role) {
    case IdRole: return entry.id;
    case KindRole:
        return entry.source.kind == elv::SourceKind::Monitor ? QStringLiteral("monitor") : QStringLiteral("window");
    case Qt::DisplayRole:
    case TitleRole: return entry.title;
    case SubtitleRole: return entry.subtitle;
    case ThumbnailRole: return QStringLiteral("image://thumbnail/%1/%2").arg(generation_).arg(entry.id);
    case SelectedRole: return entry.id == selectedId_;
    default: return {};
    }
}

QHash<int, QByteArray> SourceModel::roleNames() const {
    return {{IdRole, "sourceId"},       {KindRole, "kind"},           {TitleRole, "title"},
            {SubtitleRole, "subtitle"}, {ThumbnailRole, "thumbnail"}, {SelectedRole, "selected"}};
}

// --- thumbnails -------------------------------------------------------------

namespace {

class ThumbnailResponse : public QQuickImageResponse {
public:
    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image_);
    }
    // No frame (minimized window) is not an error: deliver a 1x1 image that the
    // card treats as "no preview", instead of a QML warning on every refresh.
    QString errorString() const override { return {}; }

    void deliver(QImage image) {
        image_ = std::move(image);
        emit finished();
    }

private:
    QImage image_;
};

}  // namespace

ThumbnailProvider::ThumbnailProvider() {
    // Each capture briefly creates a D3D device; a few at a time is plenty.
    pool_.setMaxThreadCount(4);
}

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    auto* response = new ThumbnailResponse;
    const QString sourceId = id.section(QLatin1Char('/'), 1, 1);
    const auto source = SourceModel::parseId(sourceId);
    std::optional<elv::CaptureRegion> region;
    if (const QStringList r = id.section(QLatin1Char('/'), 2, 2).split(QLatin1Char(',')); r.size() == 4)
        region = elv::CaptureRegion{r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt()};
    const QSize size = requestedSize.isValid() ? requestedSize : QSize(400, 225);

    pool_.start([response, source, size, region] {
        QImage image;
        if (source) {
            if (const auto thumb = elv::captureThumbnail(*source, size.width(), size.height(), region)) {
                image = QImage(thumb->bgra.data(), static_cast<int>(thumb->width), static_cast<int>(thumb->height),
                               static_cast<int>(thumb->width * 4), QImage::Format_RGB32)
                            .copy();
            }
        }
        // finished() may be connected across threads; Qt queues it to the requester.
        if (image.isNull()) {
            image = QImage(1, 1, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
        }
        response->deliver(std::move(image));
    });
    return response;
}
