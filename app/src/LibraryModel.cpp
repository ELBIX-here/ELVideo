#include "LibraryModel.h"

#include "AppSettings.h"

#include "core/IRecorder.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLocale>
#include <QPointer>
#include <QUrl>

#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <thread>

namespace {

const QStringList kVideoFilters{QStringLiteral("*.mp4"), QStringLiteral("*.mkv")};
const QStringList kImageFilters{QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")};

QString formatSize(qint64 bytes) {
    return QLocale::system().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString formatDuration(qint64 ms) {
    if (ms < 0) return {};
    const qint64 s = ms / 1000;
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

LibraryModel::LibraryModel(AppSettings& settings, QObject* parent) : QAbstractListModel(parent), settings_(settings) {
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(500);
    connect(&refreshTimer_, &QTimer::timeout, this, &LibraryModel::refresh);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { refreshTimer_.start(); });
    connect(&settings_, &AppSettings::changed, this, [this] { watchFolders(); });
    watchFolders();
}

void LibraryModel::watchFolders() {
    const QStringList wanted{settings_.effectiveOutputDir(), settings_.effectiveScreenshotDir()};
    QStringList current = watcher_.directories();
    std::sort(current.begin(), current.end());
    QStringList existing;
    for (const auto& d : wanted)
        if (QDir(d).exists() && !existing.contains(d)) existing << d;
    std::sort(existing.begin(), existing.end());
    if (current == existing) return;
    if (!current.isEmpty()) watcher_.removePaths(current);
    if (!existing.isEmpty()) watcher_.addPaths(existing);
    refreshTimer_.start();
}

void LibraryModel::refresh() {
    std::vector<Item> items;
    const auto scan = [&](const QString& dir, const QStringList& filters, bool video) {
        for (const QFileInfo& info : QDir(dir).entryInfoList(filters, QDir::Files)) {
            Item item;
            item.path = QDir::toNativeSeparators(info.absoluteFilePath());
            if (std::any_of(items.begin(), items.end(), [&](const Item& i) { return i.path == item.path; })) continue;
            item.name = info.fileName();
            item.kind = !video ? QStringLiteral("image")
                        : info.fileName().startsWith(QLatin1String("ELVideo_Replay_")) ? QStringLiteral("replay")
                                                                                      : QStringLiteral("video");
            item.modified = info.lastModified();
            item.size = info.size();
            // Keep a duration already probed for an unchanged file.
            for (const auto& old : all_)
                if (old.path == item.path && old.size == item.size && old.modified == item.modified) item.durationMs = old.durationMs;
            items.push_back(std::move(item));
        }
    };
    scan(settings_.effectiveOutputDir(), kVideoFilters, true);
    scan(settings_.effectiveScreenshotDir(), kImageFilters, false);
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.modified > b.modified; });

    beginResetModel();
    all_ = std::move(items);
    ++generation_;
    rebuildVisible();
    endResetModel();
    emit countsChanged();
    watchFolders();  // a folder may have been created meanwhile
    probeDurations();
}

void LibraryModel::probeDurations() {
    std::vector<QString> pending;
    for (const auto& item : all_)
        if (item.kind != QLatin1String("image") && item.durationMs < 0) pending.push_back(item.path);
    if (pending.empty()) return;
    std::thread([pending = std::move(pending), guard = QPointer<LibraryModel>(this), generation = generation_] {
        for (const auto& path : pending) {
            const auto info = elv::probeMediaFile(path.toStdWString(), false);
            const qint64 ms = info ? info->duration.count() : 0;
            QMetaObject::invokeMethod(qApp, [guard, generation, path, ms] {
                if (!guard || guard->generation_ != generation) return;
                for (std::size_t i = 0; i < guard->all_.size(); ++i) {
                    if (guard->all_[i].path != path) continue;
                    guard->all_[i].durationMs = ms;
                    const auto row = std::find(guard->visible_.begin(), guard->visible_.end(), static_cast<int>(i));
                    if (row != guard->visible_.end()) {
                        const QModelIndex index = guard->index(static_cast<int>(row - guard->visible_.begin()));
                        emit guard->dataChanged(index, index, {DurationRole});
                    }
                }
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void LibraryModel::rebuildVisible() {
    visible_.clear();
    for (int i = 0; i < static_cast<int>(all_.size()); ++i) {
        const bool image = all_[i].kind == QLatin1String("image");
        if (filter_ != QLatin1String("all") && (filter_ == QLatin1String("image")) != image) continue;
        if (!search_.isEmpty() && !all_[i].name.contains(search_, Qt::CaseInsensitive)) continue;
        visible_.push_back(i);
    }
}

void LibraryModel::setFilter(const QString& filter) {
    if (filter == filter_) return;
    beginResetModel();
    filter_ = filter;
    rebuildVisible();
    endResetModel();
    emit filterChanged();
}

void LibraryModel::setSearch(const QString& search) {
    const QString trimmed = search.trimmed();
    if (trimmed == search_) return;
    beginResetModel();
    search_ = trimmed;
    rebuildVisible();
    endResetModel();
    emit searchChanged();
}

int LibraryModel::videoCount() const {
    return static_cast<int>(std::count_if(all_.begin(), all_.end(), [](const Item& i) { return i.kind != QLatin1String("image"); }));
}

int LibraryModel::imageCount() const { return static_cast<int>(all_.size()) - videoCount(); }

bool LibraryModel::moveToTrash(const QString& path) {
    std::wstring from = QDir::toNativeSeparators(path).toStdWString();
    from.push_back(L'\0');  // the list is double-null terminated
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const bool ok = SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
    refresh();
    return ok;
}

int LibraryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(visible_.size())) return {};
    const Item& item = all_[visible_[index.row()]];
    switch (role) {
    case PathRole: return item.path;
    case Qt::DisplayRole:
    case NameRole: return item.name;
    case KindRole: return item.kind;
    case DateRole: return QLocale::system().toString(item.modified, QStringLiteral("yyyy-MM-dd HH:mm"));
    case SizeRole: return formatSize(item.size);
    case DurationRole: return formatDuration(item.durationMs);
    case ThumbnailRole:
        return QStringLiteral("image://library/%1/%2")
            .arg(item.modified.toMSecsSinceEpoch())
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(item.path)));
    default: return {};
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const {
    return {{PathRole, "path"}, {NameRole, "name"},         {KindRole, "kind"},           {DateRole, "date"},
            {SizeRole, "size"}, {DurationRole, "duration"}, {ThumbnailRole, "thumbnail"}};
}

// --- thumbnails ------------------------------------------------------------------

namespace {

class LibraryResponse : public QQuickImageResponse {
public:
    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(image_); }
    QString errorString() const override { return {}; }
    void deliver(QImage image) {
        image_ = std::move(image);
        emit finished();
    }

private:
    QImage image_;
};

}  // namespace

LibraryThumbnailProvider::LibraryThumbnailProvider() { pool_.setMaxThreadCount(3); }

QQuickImageResponse* LibraryThumbnailProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    auto* response = new LibraryResponse;
    const QString path = QUrl::fromPercentEncoding(id.section(QLatin1Char('/'), 1).toUtf8());  // the id arrives partly decoded (Hangul)
    const QSize size = requestedSize.isValid() ? requestedSize : QSize(320, 180);
    pool_.start([response, path, size] {
        QImage image;
        if (path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive) || path.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive) ||
            path.endsWith(QLatin1String(".jpeg"), Qt::CaseInsensitive)) {
            QImageReader reader(path);
            const QSize full = reader.size();
            if (full.isValid()) reader.setScaledSize(full.scaled(size, Qt::KeepAspectRatio));
            image = reader.read();
        } else if (const auto info = elv::probeMediaFile(path.toStdWString(), true); info && info->frame) {
            const auto& f = *info->frame;
            image = QImage(f.bgra.data(), static_cast<int>(f.width), static_cast<int>(f.height), static_cast<int>(f.width * 4),
                           QImage::Format_RGB32)
                        .scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (image.isNull()) {  // unreadable: a 1x1 marker the card shows as "no preview"
            image = QImage(1, 1, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
        }
        response->deliver(std::move(image));
    });
    return response;
}
