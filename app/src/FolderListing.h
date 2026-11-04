#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>
#include <vector>

class WatchProgress;

// One folder of the video library: its subfolders (each with how many videos it
// holds underneath and how many are watched) and its own videos, in natural
// order ("2강" before "10강"). Listing runs off the UI thread — on a Google
// Drive or network folder it can take a moment — and durations are probed in
// the background into WatchProgress, which also says what has been watched.
class FolderListing : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(QObject* progress READ progress WRITE setProgress NOTIFY progressChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool exists READ exists NOTIFY listed)
    // [{ name, path, videoCount, watchedCount }]
    Q_PROPERTY(QVariantList subfolders READ subfolders NOTIFY countsChanged)
    // [{ name, title, path, size }]
    Q_PROPERTY(QVariantList videos READ videos NOTIFY listed)
    Q_PROPERTY(int watchedCount READ watchedCount NOTIFY countsChanged)  // of this folder's own videos
    Q_PROPERTY(QString errorText READ errorText NOTIFY listed)          // why a Drive folder could not be read
    // The folder's own name: the last path part, or for a Drive folder the name Drive gives it.
    Q_PROPERTY(QString folderName READ folderName WRITE setFolderName NOTIFY folderChanged)

public:
    explicit FolderListing(QObject* parent = nullptr);

    QString folder() const { return folder_; }
    void setFolder(const QString& folder);
    QObject* progress() const;
    void setProgress(QObject* progress);
    bool loading() const { return loading_; }
    bool exists() const { return exists_; }
    QVariantList subfolders() const;
    QVariantList videos() const { return videos_; }
    int watchedCount() const;
    QString errorText() const { return errorText_; }
    QString folderName() const { return folderName_; }
    void setFolderName(const QString& name);

    Q_INVOKABLE void refresh();
    // The first video not watched to the end: the one to resume (or start) next.
    Q_INVOKABLE QString nextToWatch() const;

    static bool isVideo(const QString& fileName);

signals:
    void folderChanged();
    void progressChanged();
    void loadingChanged();
    void listed();
    void countsChanged();

private:
    struct Sub {
        QString name;
        QString path;
        QStringList videos;  // everything underneath, for the counts
    };

    void probeDurations(int generation, const QStringList& paths);
    void refreshDrive(int generation);

    QString folder_;
    QPointer<WatchProgress> progress_;
    bool loading_ = false;
    bool exists_ = true;
    QString errorText_;
    QString folderName_;
    int generation_ = 0;
    // The current generation, readable by the background probe without the UI thread.
    std::shared_ptr<std::atomic_int> live_ = std::make_shared<std::atomic_int>(0);
    std::vector<Sub> subs_;
    QVariantList videos_;
};
