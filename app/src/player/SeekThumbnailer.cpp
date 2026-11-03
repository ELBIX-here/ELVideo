#include "player/SeekThumbnailer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <mpv/client.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <cmath>
#include <utility>

namespace {

void option(mpv_handle* mpv, const char* name, const QString& value) {
    if (mpv_set_option_string(mpv, name, value.toUtf8().constData()) < 0)
        spdlog::warn("thumbnailer: option {}={} not accepted", name, value.toStdString());
}

// Waits for `id` (or an error/end) up to `timeout`; false on timeout.
bool waitFor(mpv_handle* mpv, mpv_event_id id, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const mpv_event* e = mpv_wait_event(mpv, 0.05);
        if (e->event_id == id) return true;
        if (e->event_id == MPV_EVENT_END_FILE || e->event_id == MPV_EVENT_SHUTDOWN) return false;
    }
    return false;
}

}  // namespace

SeekThumbnailer::SeekThumbnailer(QObject* parent) : QObject(parent), worker_([this] { run(); }) {}

SeekThumbnailer::~SeekThumbnailer() {
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    worker_.join();
}

void SeekThumbnailer::setFile(const QString& path, int videoWidth, int videoHeight) {
    const int height = videoWidth > 0 && videoHeight > 0
                           ? std::max(2, static_cast<int>(std::lround(kWidth * static_cast<double>(videoHeight) / videoWidth)) & ~1)
                           : 136;
    {
        std::lock_guard lock(mutex_);
        // Width and height arrive as separate property changes; reopen only when it matters.
        if (path == file_ && height == height_) return;
        file_ = path;
        height_ = height;
        fileDirty_ = true;
        wanted_ = -1;
        image_ = QImage();
    }
    time_ = -1;
    source_.clear();
    wake_.notify_all();
    emit fileChanged();  // after the reset, so a request made in response is kept
    emit ready();
}

void SeekThumbnailer::request(double seconds) {
    {
        std::lock_guard lock(mutex_);
        wanted_ = std::max(0.0, seconds);
    }
    wake_.notify_all();
}

QImage SeekThumbnailer::image() const {
    std::lock_guard lock(mutex_);
    return image_;
}

void SeekThumbnailer::run() {
    const QString output = QDir::toNativeSeparators(QDir::tempPath() + QStringLiteral("/elvideo-seekthumb-%1.bgra")
                                                                           .arg(QCoreApplication::applicationPid()));
    mpv_handle* mpv = nullptr;
    int height = 0;
    double done = -1;

    for (;;) {
        QString file;
        bool reopen = false;
        double wanted = -1;
        {
            std::unique_lock lock(mutex_);
            // Requests wait until a file is open (no mpv yet: nothing to do with them).
            wake_.wait(lock, [&] { return quit_ || fileDirty_ || (mpv && wanted_ >= 0 && wanted_ != done); });
            if (quit_) break;
            reopen = std::exchange(fileDirty_, false);
            file = file_;
            wanted = wanted_;
            height = height_;
        }

        if (reopen) {
            if (mpv) mpv_terminate_destroy(std::exchange(mpv, nullptr));
            done = -1;
            if (file.isEmpty()) continue;
            mpv = mpv_create();
            if (!mpv) continue;
            // Encoding mode: every presented frame is written, scaled, as raw BGRA to `output`.
            option(mpv, "config", QStringLiteral("no"));
            option(mpv, "terminal", QStringLiteral("no"));
            option(mpv, "o", output);
            option(mpv, "ovc", QStringLiteral("rawvideo"));
            option(mpv, "of", QStringLiteral("image2"));
            option(mpv, "ofopts", QStringLiteral("update=1"));
            option(mpv, "vf", QStringLiteral("scale=w=%1:h=%2,format=bgra").arg(kWidth).arg(height));
            option(mpv, "aid", QStringLiteral("no"));
            option(mpv, "sid", QStringLiteral("no"));
            option(mpv, "sub-auto", QStringLiteral("no"));
            option(mpv, "audio-file-auto", QStringLiteral("no"));
            option(mpv, "hwdec", QStringLiteral("no"));
            option(mpv, "pause", QStringLiteral("yes"));
            option(mpv, "keep-open", QStringLiteral("always"));
            option(mpv, "resume-playback", QStringLiteral("no"));
            if (mpv_initialize(mpv) < 0) {
                spdlog::warn("thumbnailer: mpv_initialize failed");
                mpv_terminate_destroy(std::exchange(mpv, nullptr));
                continue;
            }
            QFile::remove(output);  // so "exists" below means this file's first frame
            const QByteArray path = QDir::toNativeSeparators(file).toUtf8();
            const char* load[] = {"loadfile", path.constData(), nullptr};
            mpv_command(mpv, load);
            if (!waitFor(mpv, MPV_EVENT_FILE_LOADED, std::chrono::seconds(5))) {
                spdlog::warn("thumbnailer: cannot open {}", file.toStdString());
                mpv_terminate_destroy(std::exchange(mpv, nullptr));
                continue;
            }
            // The paused start presents (and so writes) the first frame. Wait for that write,
            // or it could land after a request's timestamp check and pass for that frame.
            for (int i = 0; i < 100 && !QFileInfo::exists(output); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!mpv || wanted < 0 || wanted == done) continue;

        // Seek to the keyframe at or before `wanted` (fast), then play exactly one frame:
        // a paused seek alone presents nothing in encoding mode, so nothing is written.
        const QFileInfo before(output);
        const QDateTime stamp = before.exists() ? before.lastModified() : QDateTime();
        const QByteArray time = QByteArray::number(wanted, 'f', 3);
        const char* seek[] = {"seek", time.constData(), "absolute+keyframes", nullptr};
        mpv_command(mpv, seek);
        waitFor(mpv, MPV_EVENT_PLAYBACK_RESTART, std::chrono::seconds(2));
        const char* step[] = {"frame-step", nullptr};
        mpv_command(mpv, step);
        const qint64 expected = static_cast<qint64>(kWidth) * height * 4;
        QImage image;
        for (int i = 0; i < 40 && image.isNull(); ++i) {
            const QFileInfo info(output);
            if (info.exists() && info.size() == expected && info.lastModified() != stamp) {
                QFile f(output);
                if (f.open(QIODevice::ReadOnly)) {
                    const QByteArray data = f.readAll();
                    if (data.size() == expected)
                        image = QImage(reinterpret_cast<const uchar*>(data.constData()), kWidth, height, kWidth * 4,
                                       QImage::Format_RGB32)
                                    .copy();
                }
            }
            if (image.isNull()) std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        done = wanted;
        if (image.isNull()) continue;
        {
            std::lock_guard lock(mutex_);
            image_ = image;
        }
        QMetaObject::invokeMethod(this, [this, wanted] {
            time_ = wanted;
            source_ = QStringLiteral("image://seekthumb/%1").arg(++generation_);
            emit ready();
        }, Qt::QueuedConnection);
    }
    if (mpv) mpv_terminate_destroy(mpv);
    QFile::remove(output);
}
