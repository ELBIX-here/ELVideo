#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariant>

#include <functional>

// How far each video has been watched, shared by the recorder window (library
// pages) and the player process through %LOCALAPPDATA%\ELVideo\progress.json.
// Each write re-reads the file first, so both processes' changes survive; a
// change by the other process arrives through the file watcher as changed().
// (Resuming at the position is mpv's watch-later; this is for showing progress.)
class WatchProgress : public QObject {
    Q_OBJECT
    // Bumped on every change; QML bindings read it to re-evaluate get() and inProgress().
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit WatchProgress(QObject* parent = nullptr);
    ~WatchProgress() override;

    // { position, duration (seconds, -1 unknown), watched, updated (ms since epoch) };
    // an empty map for a video never opened.
    Q_INVOKABLE QVariantMap get(const QString& path) const;
    // Seconds watched so far; marks the video watched near its end.
    Q_INVOKABLE void update(const QString& path, double position, double duration);
    Q_INVOKABLE void setWatched(const QString& path, bool watched);
    // Remembers a duration probed elsewhere (library pages) without touching progress.
    Q_INVOKABLE void setDuration(const QString& path, double duration);
    // Started but not finished, most recent first, existing files only:
    // [{ path, name, folder, position, duration, updated }].
    Q_INVOKABLE QVariantList inProgress(int limit = 24) const;

    // Writes pending changes now (the player calls this before quitting).
    Q_INVOKABLE void flush();

    int revision() const { return revision_; }

signals:
    void changed();

private:
    struct Entry {
        QString path;  // as given, for display and opening
        double position = 0;
        double duration = -1;
        bool watched = false;
        qint64 updated = 0;
    };

    static QString key(const QString& path);
    static QString filePath();
    void load();
    void save();
    void reloadIfChanged();
    void touch(const QString& path, const std::function<void(Entry&)>& change);

    QHash<QString, Entry> entries_;
    QHash<QString, Entry> pending_;  // changed here, not written yet
    QFileSystemWatcher watcher_;
    QTimer saveTimer_;
    QTimer pollTimer_;
    QDateTime loadedAt_;  // the file's time when last read or written here
    bool saving_ = false;
    int revision_ = 0;
};
