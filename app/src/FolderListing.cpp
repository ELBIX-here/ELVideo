#include "FolderListing.h"

#include "WatchProgress.h"
#include "core/IRecorder.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>

#include <algorithm>
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

void FolderListing::refresh() {
    const int generation = ++generation_;
    live_->store(generation);
    if (folder_.isEmpty()) return;
    loading_ = true;
    emit loadingChanged();

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
