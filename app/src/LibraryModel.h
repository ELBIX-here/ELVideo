#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>
#include <QTimer>

#include <vector>

class AppSettings;

// Recordings, replays and screenshots in the output and screenshot folders,
// newest first. Follows the folders; durations are filled in the background.
class LibraryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)  // all | video | image
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)  // part of the name
    Q_PROPERTY(int videoCount READ videoCount NOTIFY countsChanged)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY countsChanged)

public:
    enum Role { PathRole = Qt::UserRole + 1, NameRole, KindRole, DateRole, SizeRole, DurationRole, ThumbnailRole };

    explicit LibraryModel(AppSettings& settings, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    // Recycle Bin, so it can be undone from Explorer.
    Q_INVOKABLE bool moveToTrash(const QString& path);

    QString filter() const { return filter_; }
    void setFilter(const QString& filter);
    QString search() const { return search_; }
    void setSearch(const QString& search);
    int videoCount() const;
    int imageCount() const;

signals:
    void filterChanged();
    void searchChanged();
    void countsChanged();

private:
    struct Item {
        QString path;
        QString name;
        QString kind;  // video | replay | image
        QDateTime modified;
        qint64 size = 0;
        qint64 durationMs = -1;  // videos; -1 until probed
    };

    void rebuildVisible();
    void watchFolders();
    void probeDurations();

    AppSettings& settings_;
    std::vector<Item> all_;
    std::vector<int> visible_;
    QString filter_ = QStringLiteral("all");
    QString search_;
    QFileSystemWatcher watcher_;
    QTimer refreshTimer_;  // coalesces folder change bursts
    int generation_ = 0;
};

// image://library/<generation>/<percent-encoded path>: image files scaled, videos via
// a decoded frame (Media Foundation). Off the UI thread.
class LibraryThumbnailProvider : public QQuickAsyncImageProvider {
public:
    LibraryThumbnailProvider();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    QThreadPool pool_;
};
