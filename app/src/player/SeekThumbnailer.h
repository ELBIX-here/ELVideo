#pragma once

#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// Seek-bar hover previews. A second, hidden mpv runs in encoding mode: each seek
// writes the frame as raw BGRA (scaled down) to a temp file, which is read back.
// Requests are coalesced: only the latest hovered time is decoded.
class SeekThumbnailer : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source NOTIFY ready)  // image://seekthumb/<n>
    Q_PROPERTY(double time READ time NOTIFY ready)       // what `source` shows
    Q_PROPERTY(int thumbWidth READ thumbWidth CONSTANT)
    Q_PROPERTY(int thumbHeight READ thumbHeight NOTIFY fileChanged)

public:
    explicit SeekThumbnailer(QObject* parent = nullptr);
    ~SeekThumbnailer() override;

    // A new file (path) with its video size; empty path stops previews.
    Q_INVOKABLE void setFile(const QString& path, int videoWidth, int videoHeight);
    Q_INVOKABLE void request(double seconds);

    QString source() const { return source_; }
    double time() const { return time_; }
    int thumbWidth() const { return kWidth; }
    int thumbHeight() const { return height_; }
    QImage image() const;

    static constexpr int kWidth = 240;

signals:
    void ready();
    void fileChanged();

private:
    void run();

    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool quit_ = false;
    QString file_;            // guarded
    bool fileDirty_ = false;  // guarded
    double wanted_ = -1;      // guarded; latest requested time
    QImage image_;            // guarded
    int height_ = 135;
    QString source_;
    double time_ = -1;
    int generation_ = 0;
};

class SeekThumbnailProvider : public QQuickImageProvider {
public:
    explicit SeekThumbnailProvider(SeekThumbnailer& thumbnailer)
        : QQuickImageProvider(QQuickImageProvider::Image), thumbnailer_(thumbnailer) {}
    QImage requestImage(const QString&, QSize* size, const QSize&) override {
        QImage image = thumbnailer_.image();
        if (size) *size = image.size();
        return image;
    }

private:
    SeekThumbnailer& thumbnailer_;
};
