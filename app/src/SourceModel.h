#pragma once

#include "core/Types.h"

#include <QAbstractListModel>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>

#include <optional>
#include <vector>

// Capture sources for the picker: monitors or windows, depending on showWindows.
// Ids ("m:<hex>" / "w:<hex>") stay stable while the monitor or window exists.
class SourceModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool showWindows READ showWindows WRITE setShowWindows NOTIFY showWindowsChanged)
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY selectionChanged)
    Q_PROPERTY(int monitorCount READ monitorCount NOTIFY countsChanged)
    Q_PROPERTY(int windowCount READ windowCount NOTIFY countsChanged)

public:
    enum Role { IdRole = Qt::UserRole + 1, KindRole, TitleRole, SubtitleRole, ThumbnailRole, SelectedRole };

    explicit SourceModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Re-enumerates sources and reloads every thumbnail.
    Q_INVOKABLE void refresh();

    bool showWindows() const { return showWindows_; }
    void setShowWindows(bool show);
    QString selectedId() const { return selectedId_; }
    void setSelectedId(const QString& id);
    QString selectedTitle() const;
    int monitorCount() const;
    int windowCount() const;

    // The selected source, or the primary monitor when nothing valid is selected.
    std::optional<elv::CaptureSource> selectedSource() const;

    static QString idOf(const elv::CaptureSource& source);
    // Kind and handle only; enough for captureThumbnail().
    static std::optional<elv::CaptureSource> parseId(const QString& id);

signals:
    void showWindowsChanged();
    void selectionChanged();
    void countsChanged();

private:
    struct Entry {
        elv::CaptureSource source;
        QString id;
        QString title;
        QString subtitle;
    };

    void rebuildVisible();
    const Entry* find(const QString& id) const;

    std::vector<Entry> all_;
    std::vector<int> visible_;  // indexes into all_
    bool showWindows_ = false;
    QString selectedId_;
    int generation_ = 0;  // part of thumbnail URLs so QML reloads them on refresh
};

// image://thumbnail/<generation>/<source id>, captured off the UI thread.
class ThumbnailProvider : public QQuickAsyncImageProvider {
public:
    ThumbnailProvider();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    QThreadPool pool_;
};
