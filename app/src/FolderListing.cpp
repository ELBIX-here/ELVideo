#include "FolderListing.h"

#include "GoogleDrive.h"
#include "WatchProgress.h"
#include "core/IRecorder.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QThreadPool>

#include <algorithm>
#include <functional>
#include <thread>

namespace {

// Directory names and file names compared as people number them: "2강" < "10강".
void naturalSort(QFileInfoList& list) {
    QCollator collator{QLocale(QLocale::Korean)};
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(),
              [&](const QFileInfo& a, const QFileInfo& b) { return collator.compare(a.fileName(), b.fileName()) < 0; });
}

}  // namespace

FolderListing::FolderListing(QObject* parent) : QObject(parent) {}

bool FolderListing::isVideo(const QString& fileName) {
    static const QStringList kExtensions = {
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("mov"),
        QStringLiteral("wmv"), QStringLiteral("webm"), QStringLiteral("m4v"), QStringLiteral("ts"),
        QStringLiteral("m2ts"), QStringLiteral("flv"), QStringLiteral("mpg"), QStringLiteral("mpeg")};
    return kExtensions.contains(QFileInfo(fileName).suffix().toLower());
}

QObject* FolderListing::progress() const { return progress_.data(); }

void FolderListing::setProgress(QObject* progress) {
    auto* p = qobject_cast<WatchProgress*>(progress);
    if (p == progress_) return;
    if (progress_) disconnect(progress_, nullptr, this, nullptr);
    progress_ = p;
    if (progress_) connect(progress_, &WatchProgress::changed, this, &FolderListing::countsChanged);
    emit progressChanged();
    emit countsChanged();
}

void FolderListing::setFolder(const QString& folder) {
    if (folder == folder_) return;
    folder_ = folder;
    emit folderChanged();
    subs_.clear();
    videos_.clear();
    emit listed();
    emit countsChanged();
    refresh();
}

void FolderListing::setFolderName(const QString& name) {
    if (name == folderName_) return;
    folderName_ = name;
    emit folderChanged();
}

void FolderListing::refresh() {
    const int generation = ++generation_;
    live_->store(generation);
    if (folder_.isEmpty()) return;
    loading_ = true;
    emit loadingChanged();
    if (gdrive::isFolder(folder_)) {
        refreshDrive(generation);
        return;
    }

    std::thread([folder = folder_, generation, guard = QPointer<FolderListing>(this)] {
        const QDir dir(folder);
        const bool exists = dir.exists();
        QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
        QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable);
        naturalSort(dirs);
        naturalSort(files);

        std::vector<Sub> subs;
        for (const QFileInfo& d : dirs) {
            Sub sub{d.fileName(), d.absoluteFilePath(), {}};
            QDirIterator it(d.absoluteFilePath(), QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString p = it.next();
                if (isVideo(p)) sub.videos.push_back(p);
            }
            if (!sub.videos.isEmpty()) subs.push_back(std::move(sub));  // folders without videos are noise
        }
        QVariantList videos;
        QStringList paths;
        for (const QFileInfo& f : files) {
            if (!isVideo(f.fileName())) continue;
            paths.push_back(f.absoluteFilePath());
            videos.push_back(QVariantMap{{QStringLiteral("name"), f.fileName()},
                                         {QStringLiteral("title"), f.completeBaseName()},
                                         {QStringLiteral("path"), f.absoluteFilePath()},
                                         {QStringLiteral("size"), f.size()}});
        }

        QMetaObject::invokeMethod(qApp, [guard, generation, exists, subs = std::move(subs), videos, paths]() mutable {
            if (!guard || guard->generation_ != generation) return;
            guard->exists_ = exists;
            guard->errorText_.clear();
            guard->subs_ = std::move(subs);
            guard->videos_ = videos;
            guard->loading_ = false;
            emit guard->loadingChanged();
            emit guard->listed();
            emit guard->countsChanged();
            guard->probeDurations(generation, paths);
        }, Qt::QueuedConnection);
    }).detach();
}

// A Google Drive folder: the same listing from the Drive API. Durations come with
// it (Drive reads them itself), and each video is labelled with its name and
// folder so "이어서 보기" can show it without the file. Network calls block, so
// this runs on the thread pool, subfolders side by side.
void FolderListing::refreshDrive(int generation) {
    QThreadPool::globalInstance()->start([folderId = gdrive::folderId(folder_), generation, live = live_,
                                          guard = QPointer<FolderListing>(this)] {
        QString error;
        const auto kids = gdrive::children(folderId, &error);
        std::vector<Sub> subs;
        QVariantList videos;
        struct Known {
            QString path;
            QString name;
            double duration;
        };
        std::vector<Known> known;
        if (kids) {
            for (const auto& k : *kids) {
                if (k.isFolder()) {
                    subs.push_back(Sub{k.name, gdrive::folderPath(k.id), {}});
                } else if (k.mime.startsWith(QLatin1String("video/")) || isVideo(k.name)) {
                    const QString path = gdrive::videoPath(k.id);
                    known.push_back({path, k.name, k.durationSec});
                    videos.push_back(QVariantMap{{QStringLiteral("name"), k.name},
                                                 {QStringLiteral("title"), QFileInfo(k.name).completeBaseName()},
                                                 {QStringLiteral("path"), path},
                                                 {QStringLiteral("size"), k.size}});
                }
            }
            // Every video under each subfolder (three levels), for the counts.
            std::function<QStringList(const QString&, int)> below = [&](const QString& id, int depth) {
                QStringList out;
                if (depth == 0 || live->load() != generation) return out;
                if (const auto items = gdrive::children(id))
                    for (const auto& i : *items) {
                        if (i.isFolder()) out += below(i.id, depth - 1);
                        else if (i.mime.startsWith(QLatin1String("video/")) || isVideo(i.name)) out.push_back(gdrive::videoPath(i.id));
                    }
                return out;
            };
            for (Sub& sub : subs) sub.videos = below(gdrive::folderId(sub.path), 3);
            std::erase_if(subs, [](const Sub& s) { return s.videos.isEmpty(); });
        }
        QMetaObject::invokeMethod(qApp, [guard, generation, ok = kids.has_value(), error, subs = std::move(subs), videos,
                                         known = std::move(known)]() mutable {
            if (!guard || guard->generation_ != generation) return;
            guard->exists_ = ok;
            guard->errorText_ = ok ? QString() : error;
            guard->subs_ = std::move(subs);
            guard->videos_ = videos;
            guard->loading_ = false;
            if (guard->progress_) {
                const QString folderName = guard->folderName_.isEmpty() ? gdrive::knownName(gdrive::folderId(guard->folder_)) : guard->folderName_;
                for (const Known& k : known) {
                    if (k.duration > 0) guard->progress_->setDuration(k.path, k.duration);
                    guard->progress_->setLabel(k.path, k.name, folderName, guard->folder_);
                }
            }
            emit guard->loadingChanged();
            emit guard->listed();
            emit guard->countsChanged();
        }, Qt::QueuedConnection);
    });
}

// Durations nobody has recorded yet, one file at a time, stopping when the folder changes.
void FolderListing::probeDurations(int generation, const QStringList& paths) {
    if (!progress_) return;
    QStringList missing;
    for (const QString& p : paths) {
        const QVariantMap entry = progress_->get(p);
        if (entry.value(QStringLiteral("duration"), -1).toDouble() <= 0) missing.push_back(p);
    }
    if (missing.isEmpty()) return;
    std::thread([missing, generation, live = live_, guard = QPointer<FolderListing>(this)] {
        for (const QString& p : missing) {
            if (live->load() != generation) return;  // another folder now
            const auto info = elv::probeMediaFile(p.toStdWString(), false);
            if (!info || info->duration.count() <= 0) continue;
            const double seconds = info->duration.count() / 1000.0;
            QMetaObject::invokeMethod(qApp, [guard, p, seconds] {
                if (guard && guard->progress_) guard->progress_->setDuration(p, seconds);
            }, Qt::QueuedConnection);
        }
    }).detach();
}

QVariantList FolderListing::subfolders() const {
    QVariantList out;
    for (const Sub& s : subs_) {
        int watched = 0;
        if (progress_)
            for (const QString& p : s.videos)
                if (progress_->get(p).value(QStringLiteral("watched")).toBool()) ++watched;
        out.push_back(QVariantMap{{QStringLiteral("name"), s.name},
                                  {QStringLiteral("path"), s.path},
                                  {QStringLiteral("videoCount"), static_cast<int>(s.videos.size())},
                                  {QStringLiteral("watchedCount"), watched}});
    }
    return out;
}

int FolderListing::watchedCount() const {
    if (!progress_) return 0;
    int watched = 0;
    for (const QVariant& v : videos_)
        if (progress_->get(v.toMap().value(QStringLiteral("path")).toString()).value(QStringLiteral("watched")).toBool())
            ++watched;
    return watched;
}

QString FolderListing::nextToWatch() const {
    // A video stopped partway comes first; otherwise the first one not watched.
    QString firstUnwatched;
    qint64 latest = -1;
    QString latestPartial;
    for (const QVariant& v : videos_) {
        const QString p = v.toMap().value(QStringLiteral("path")).toString();
        const QVariantMap e = progress_ ? progress_->get(p) : QVariantMap{};
        if (e.value(QStringLiteral("watched")).toBool()) continue;
        if (firstUnwatched.isEmpty()) firstUnwatched = p;
        const qint64 updated = e.value(QStringLiteral("updated"), 0).toLongLong();
        if (e.value(QStringLiteral("position"), 0).toDouble() >= 5 && updated > latest) {
            latest = updated;
            latestPartial = p;
        }
    }
    return latestPartial.isEmpty() ? firstUnwatched : latestPartial;
}
