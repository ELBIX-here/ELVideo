#pragma once

#include "core/Types.h"

#include <QAbstractListModel>
#include <QRectF>
#include <QWindow>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>

#include <optional>
#include <utility>
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
    // Region tab: record a rectangle of one monitor instead of the selected source.
    Q_PROPERTY(bool regionMode READ regionMode WRITE setRegionMode NOTIFY regionChanged)
    Q_PROPERTY(bool hasRegion READ hasRegion NOTIFY regionChanged)
    Q_PROPERTY(QString regionTitle READ regionTitle NOTIFY regionChanged)
    Q_PROPERTY(QString regionThumbnail READ regionThumbnail NOTIFY regionChanged)
    // The region on the desktop in Qt (logical, virtual-desktop) coordinates, for the frame.
    Q_PROPERTY(QRectF regionGeometry READ regionGeometry NOTIFY regionChanged)

public:
    enum Role { IdRole = Qt::UserRole + 1, KindRole, TitleRole, SubtitleRole, ThumbnailRole, SelectedRole };

    explicit SourceModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Re-enumerates sources and reloads every thumbnail.
    Q_INVOKABLE void refresh();

    // Finishes a region selection: `overlay` covers one monitor and `rect` is the
    // dragged rectangle in its coordinates. Switches to region mode. False if too small.
    Q_INVOKABLE bool setRegionFromOverlay(QWindow* overlay, const QRectF& rect);

    bool showWindows() const { return showWindows_; }
    void setShowWindows(bool show);
    QString selectedId() const { return selectedId_; }
    void setSelectedId(const QString& id);
    QString selectedTitle() const;
    int monitorCount() const;
    int windowCount() const;

    // The selected source, or the primary monitor when nothing valid is selected.
    std::optional<elv::CaptureSource> selectedSource() const;
    // In region mode with a valid region: the monitor and the region on it.
    std::optional<std::pair<elv::CaptureSource, elv::CaptureRegion>> activeRegion() const;
    // Title for the recording bar: the region or the selected source.
    QString recordingTitle() const;

    bool regionMode() const { return regionMode_; }
    void setRegionMode(bool on);
    bool hasRegion() const;
    QString regionTitle() const;
    QString regionThumbnail() const;
    QRectF regionGeometry() const { return regionGeometry_; }

    static QString idOf(const elv::CaptureSource& source);
    // Kind and handle only; enough for captureThumbnail().
    static std::optional<elv::CaptureSource> parseId(const QString& id);

signals:
    void showWindowsChanged();
    void selectionChanged();
    void countsChanged();
    void regionChanged();

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

    bool regionMode_ = false;
    QString regionMonitorId_;
    elv::CaptureRegion region_;
    QRectF regionGeometry_;
};

// image://thumbnail/<generation>/<source id>[/x,y,w,h], captured off the UI thread.
class ThumbnailProvider : public QQuickAsyncImageProvider {
public:
    ThumbnailProvider();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    QThreadPool pool_;
};
