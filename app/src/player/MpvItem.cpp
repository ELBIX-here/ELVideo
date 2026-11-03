#include "player/MpvItem.h"

#include <QDir>
#include <QFileInfo>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QPointer>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QUrl>
#include <QtQuick/qquickopenglutils.h>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <utility>
#include <stdexcept>
#include <vector>

namespace {

void* getProcAddress(void*, const char* name) {
    QOpenGLContext* context = QOpenGLContext::currentContext();
    return context ? reinterpret_cast<void*>(context->getProcAddress(name)) : nullptr;
}

QVariant toVariant(const mpv_node& node) {
    switch (node.format) {
    case MPV_FORMAT_STRING: return QString::fromUtf8(node.u.string);
    case MPV_FORMAT_FLAG: return node.u.flag != 0;
    case MPV_FORMAT_INT64: return static_cast<qlonglong>(node.u.int64);
    case MPV_FORMAT_DOUBLE: return node.u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        for (int i = 0; i < node.u.list->num; ++i) list.append(toVariant(node.u.list->values[i]));
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        for (int i = 0; i < node.u.list->num; ++i)
            map.insert(QString::fromUtf8(node.u.list->keys[i]), toVariant(node.u.list->values[i]));
        return map;
    }
    default: return {};
    }
}

// Calls the item from mpv's render thread callbacks; the item may already be gone.
struct UpdateTarget {
    QPointer<MpvItem> item;
};

class MpvRenderer : public QQuickFramebufferObject::Renderer {
public:
    MpvRenderer(MpvItem* item, std::shared_ptr<mpv_handle> mpv) : mpv_(std::move(mpv)), target_{item} {}

    ~MpvRenderer() override {
        if (context_) mpv_render_context_free(context_);  // before mpv_ (and so mpv) goes away
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        if (!context_) {
            mpv_opengl_init_params gl{getProcAddress, nullptr};
            mpv_render_param params[]{
                {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
                {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
                {MPV_RENDER_PARAM_INVALID, nullptr},
            };
            if (mpv_render_context_create(&context_, mpv_.get(), params) < 0) {
                spdlog::error("mpv: cannot create the OpenGL render context");
                context_ = nullptr;
            } else {
                mpv_render_context_set_update_callback(context_, &MpvRenderer::onUpdate, &target_);
                if (const QPointer<MpvItem> item = target_.item)
                    QMetaObject::invokeMethod(item.data(), &MpvItem::onRendererReady, Qt::QueuedConnection);
            }
        }
        return QQuickFramebufferObject::Renderer::createFramebufferObject(size);
    }

    void render() override {
        if (!context_) return;
        QOpenGLFramebufferObject* fbo = framebufferObject();
        mpv_opengl_fbo target{static_cast<int>(fbo->handle()), fbo->width(), fbo->height(), 0};
        int flip = 0;  // Qt 6's FBO texture is already the right way up for mpv's output
        mpv_render_param params[]{
            {MPV_RENDER_PARAM_OPENGL_FBO, &target},
            {MPV_RENDER_PARAM_FLIP_Y, &flip},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(context_, params);
        QQuickOpenGLUtils::resetOpenGLState();
    }

private:
    static void onUpdate(void* data) {
        const QPointer<MpvItem> item = static_cast<UpdateTarget*>(data)->item;
        if (item) QMetaObject::invokeMethod(item.data(), &QQuickItem::update, Qt::QueuedConnection);
    }

    std::shared_ptr<mpv_handle> mpv_;
    UpdateTarget target_;
    mpv_render_context* context_ = nullptr;
};

void setOption(mpv_handle* mpv, const char* name, const QString& value) {
    if (mpv_set_option_string(mpv, name, value.toUtf8().constData()) < 0)
        spdlog::warn("mpv: option {}={} not accepted", name, value.toStdString());
}

}  // namespace

MpvItem::MpvItem(QQuickItem* parent) : QQuickFramebufferObject(parent) {
    mpv_ = std::shared_ptr<mpv_handle>(mpv_create(), [](mpv_handle* h) {
        if (h) mpv_terminate_destroy(h);
    });
    if (!mpv_) throw std::runtime_error("mpv_create failed");
    mpv_handle* mpv = mpv_.get();

    const QString data = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + QStringLiteral("/ELVideo");
    setOption(mpv, "vo", QStringLiteral("libmpv"));
    // GPU decoding straight into the OpenGL renderer when the driver has the interop,
    // otherwise GPU decoding with a copy back (still far cheaper than software).
    setOption(mpv, "hwdec", QStringLiteral("auto-safe,auto-copy"));
    setOption(mpv, "keep-open", QStringLiteral("yes"));  // stay on the last frame at the end
    setOption(mpv, "idle", QStringLiteral("yes"));
    setOption(mpv, "input-default-bindings", QStringLiteral("no"));  // keys are handled in QML
    setOption(mpv, "input-vo-keyboard", QStringLiteral("no"));
    setOption(mpv, "osc", QStringLiteral("no"));
    setOption(mpv, "osd-level", QStringLiteral("0"));  // our own OSD
    setOption(mpv, "terminal", QStringLiteral("no"));
    setOption(mpv, "volume-max", QStringLiteral("150"));
    setOption(mpv, "sub-auto", QStringLiteral("fuzzy"));  // movie.srt / movie.ko.smi next to the file
    setOption(mpv, "autocreate-playlist", QStringLiteral("filter"));  // the folder's videos
    setOption(mpv, "watch-later-dir", data + QStringLiteral("/watch_later"));
    setOption(mpv, "resume-playback", QStringLiteral("yes"));
    // Remember the position and sync offsets only. mpv's default list also restores
    // tracks and filters, so a file closed with video/audio off would reopen blank.
    setOption(mpv, "watch-later-options", QStringLiteral("start,sub-delay,audio-delay"));
    setOption(mpv, "screenshot-format", QStringLiteral("png"));
#ifndef NDEBUG
    // Test hook: ELV_PLAYER_IPC=<pipe name> opens mpv's JSON IPC for scripts/test (debug only:
    // IPC can run arbitrary commands).
    if (const QString pipe = qEnvironmentVariable("ELV_PLAYER_IPC"); !pipe.isEmpty())
        setOption(mpv, "input-ipc-server", QStringLiteral("\\\\.\\pipe\\") + pipe);
#endif
    if (mpv_initialize(mpv) < 0) throw std::runtime_error("mpv_initialize failed");

    const std::pair<const char*, mpv_format> observed[] = {
        {"time-pos", MPV_FORMAT_DOUBLE},       {"duration", MPV_FORMAT_DOUBLE},     {"pause", MPV_FORMAT_FLAG},
        {"eof-reached", MPV_FORMAT_FLAG},      {"volume", MPV_FORMAT_DOUBLE},       {"mute", MPV_FORMAT_FLAG},
        {"speed", MPV_FORMAT_DOUBLE},          {"ab-loop-a", MPV_FORMAT_NODE},      {"ab-loop-b", MPV_FORMAT_NODE},
        {"sub-delay", MPV_FORMAT_DOUBLE},      {"audio-delay", MPV_FORMAT_DOUBLE},  {"track-list", MPV_FORMAT_NODE},
        {"chapter-list", MPV_FORMAT_NODE},     {"playlist-pos", MPV_FORMAT_INT64},  {"playlist-count", MPV_FORMAT_INT64},
        {"path", MPV_FORMAT_STRING},           {"media-title", MPV_FORMAT_STRING},  {"video-params/w", MPV_FORMAT_INT64},
        {"video-params/h", MPV_FORMAT_INT64},  {"hwdec-current", MPV_FORMAT_STRING}, {"idle-active", MPV_FORMAT_FLAG},
    };
    for (const auto& [name, format] : observed) mpv_observe_property(mpv, 0, name, format);

    mpv_set_wakeup_callback(
        mpv,
        [](void* data) {
            auto* item = static_cast<MpvItem*>(data);
            QMetaObject::invokeMethod(item, [item] { item->processEvents(); }, Qt::QueuedConnection);
        },
        this);
}

MpvItem::~MpvItem() {
    mpv_set_wakeup_callback(mpv_.get(), nullptr, nullptr);
    saveResumePosition();
}

QQuickFramebufferObject::Renderer* MpvItem::createRenderer() const {
    return new MpvRenderer(const_cast<MpvItem*>(this), mpv_);
}

void MpvItem::processEvents() {
    for (;;) {
        const mpv_event* event = mpv_wait_event(mpv_.get(), 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        switch (event->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: onProperty(*static_cast<mpv_event_property*>(event->data)); break;
        case MPV_EVENT_FILE_LOADED: emit fileLoaded(); break;
        case MPV_EVENT_END_FILE: {
            const auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR) {
                const QString message = QString::fromUtf8(mpv_error_string(end->error));
                spdlog::warn("mpv: cannot play: {}", message.toStdString());
                emit error(message);
            }
            break;
        }
        default: break;
        }
    }
}

void MpvItem::onProperty(const mpv_event_property& p) {
    const std::string_view name(p.name);
    const bool has = p.format != MPV_FORMAT_NONE && p.data;
    const auto number = [&](double fallback) { return has ? *static_cast<double*>(p.data) : fallback; };
    const auto flag = [&] { return has && *static_cast<int*>(p.data) != 0; };
    const auto integer = [&](qint64 fallback) { return has ? *static_cast<int64_t*>(p.data) : fallback; };
    const auto text = [&] { return has ? QString::fromUtf8(*static_cast<char**>(p.data)) : QString(); };
    const auto node = [&] { return has ? toVariant(*static_cast<mpv_node*>(p.data)) : QVariant(); };
    const auto loopPoint = [&] {
        const QVariant v = node();  // a time, or "no"
        return v.typeId() == QMetaType::Double ? v.toDouble() : -1.0;
    };

    if (name == "time-pos") {
        position_ = number(0);
        emit positionChanged();
    } else if (name == "duration") {
        duration_ = number(0);
        emit durationChanged();
    } else if (name == "pause") {
        paused_ = flag();
        emit pausedChanged();
    } else if (name == "eof-reached") {
        eof_ = flag();
        emit eofChanged();
        if (eof_) emit endOfFile();
    } else if (name == "volume") {
        volume_ = number(100);
        emit volumeChanged();
    } else if (name == "mute") {
        muted_ = flag();
        emit mutedChanged();
    } else if (name == "speed") {
        speed_ = number(1);
        emit speedChanged();
    } else if (name == "ab-loop-a") {
        loopA_ = loopPoint();
        emit loopChanged();
    } else if (name == "ab-loop-b") {
        loopB_ = loopPoint();
        emit loopChanged();
    } else if (name == "sub-delay") {
        subDelay_ = number(0);
        emit subDelayChanged();
    } else if (name == "audio-delay") {
        audioDelay_ = number(0);
        emit audioDelayChanged();
    } else if (name == "track-list") {
        tracks_ = node().toList();
        emit tracksChanged();
    } else if (name == "chapter-list") {
        chapters_ = node().toList();
        emit chaptersChanged();
    } else if (name == "playlist-pos") {
        playlistPos_ = static_cast<int>(integer(-1));
        emit playlistChanged();
    } else if (name == "playlist-count") {
        playlistCount_ = static_cast<int>(integer(0));
        emit playlistChanged();
    } else if (name == "path") {
        path_ = text();
        emit fileChanged();
    } else if (name == "media-title") {
        title_ = text();
        emit fileChanged();
    } else if (name == "video-params/w") {
        videoWidth_ = static_cast<int>(integer(0));
        emit videoSizeChanged();
    } else if (name == "video-params/h") {
        videoHeight_ = static_cast<int>(integer(0));
        emit videoSizeChanged();
    } else if (name == "hwdec-current") {
        hwdec_ = text();
        emit hwdecChanged();
    } else if (name == "idle-active") {
        idle_ = flag();
        emit idleChanged();
    }
}

void MpvItem::command(const QVariantList& args) {
    std::vector<QByteArray> utf8;
    utf8.reserve(args.size());
    for (const auto& a : args) utf8.push_back(a.toString().toUtf8());
    std::vector<const char*> argv;
    for (const auto& s : utf8) argv.push_back(s.constData());
    argv.push_back(nullptr);
    if (const int result = mpv_command(mpv_.get(), argv.data()); result < 0)
        spdlog::warn("mpv: {} failed: {}", argv.front(), mpv_error_string(result));
}

void MpvItem::setMpvProperty(const QString& name, const QVariant& value) {
    const QByteArray key = name.toUtf8();
    int result = 0;
    switch (value.typeId()) {
    case QMetaType::Bool: {
        int flag = value.toBool() ? 1 : 0;
        result = mpv_set_property(mpv_.get(), key.constData(), MPV_FORMAT_FLAG, &flag);
        break;
    }
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::Double: {
        // As text: mpv parses it for the property's own type (integer properties such
        // as sub-margin-y reject MPV_FORMAT_DOUBLE, and QML numbers are doubles).
        const QByteArray text = QByteArray::number(value.toDouble(), 'g', 12);
        result = mpv_set_property_string(mpv_.get(), key.constData(), text.constData());
        break;
    }
    default: {
        const QByteArray text = value.toString().toUtf8();
        result = mpv_set_property_string(mpv_.get(), key.constData(), text.constData());
    }
    }
    if (result < 0) spdlog::warn("mpv: set {} failed: {}", key.toStdString(), mpv_error_string(result));
}

QVariant MpvItem::getMpvProperty(const QString& name) const {
    mpv_node node;
    if (mpv_get_property(mpv_.get(), name.toUtf8().constData(), MPV_FORMAT_NODE, &node) < 0) return {};
    QVariant value = toVariant(node);
    mpv_free_node_contents(&node);
    return value;
}

void MpvItem::open(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    const QString target = url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : pathOrUrl;
    if (!rendererReady_) {
        pendingOpen_ = target;
        return;
    }
    saveResumePosition();
    command({QStringLiteral("loadfile"), target});
}

void MpvItem::onRendererReady() {
    rendererReady_ = true;
    if (!pendingOpen_.isEmpty()) command({QStringLiteral("loadfile"), std::exchange(pendingOpen_, QString())});
}

void MpvItem::seek(double seconds, bool relative) {
    command({QStringLiteral("seek"), QString::number(seconds, 'f', 3),
             relative ? QStringLiteral("relative+exact") : QStringLiteral("absolute+exact")});
}

void MpvItem::frameStep(bool backward) {
    // "seek" mode: an exact seek to the next frame. The default "play" mode waits for the
    // frame to be presented, which never completes under the render API while paused.
    if (backward)
        command({QStringLiteral("frame-back-step")});
    else
        command({QStringLiteral("frame-step"), QStringLiteral("1"), QStringLiteral("seek")});
}

void MpvItem::cycleAbLoop() { command({QStringLiteral("ab-loop")}); }

void MpvItem::clearAbLoop() {
    setMpvProperty(QStringLiteral("ab-loop-a"), QStringLiteral("no"));
    setMpvProperty(QStringLiteral("ab-loop-b"), QStringLiteral("no"));
}

void MpvItem::addSubDelay(double seconds) {
    command({QStringLiteral("add"), QStringLiteral("sub-delay"), QString::number(seconds)});
}

void MpvItem::addAudioDelay(double seconds) {
    command({QStringLiteral("add"), QStringLiteral("audio-delay"), QString::number(seconds)});
}

void MpvItem::selectTrack(const QString& type, int id) {
    const QString property = type == QLatin1String("sub")     ? QStringLiteral("sid")
                             : type == QLatin1String("audio") ? QStringLiteral("aid")
                                                              : QStringLiteral("vid");
    setMpvProperty(property, id > 0 ? QVariant(QString::number(id)) : QVariant(QStringLiteral("no")));
}

void MpvItem::addSubtitle(const QString& path) {
    const QUrl url(path);
    command({QStringLiteral("sub-add"), QDir::toNativeSeparators(url.isLocalFile() ? url.toLocalFile() : path),
             QStringLiteral("select")});
}

void MpvItem::playlistStep(int delta) {
    command({delta > 0 ? QStringLiteral("playlist-next") : QStringLiteral("playlist-prev")});
}

QString MpvItem::screenshot(const QString& directory) {
    if (idle_) return {};
    QDir().mkpath(directory);
    const int ms = static_cast<int>(position_ * 1000);
    const QString name = QStringLiteral("%1_%2-%3-%4.%5.png")
                             .arg(QFileInfo(path_).completeBaseName())
                             .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
                             .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
                             .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
                             .arg(ms % 1000, 3, 10, QLatin1Char('0'));
    const QString file = QDir::toNativeSeparators(QDir(directory).filePath(name));
    const QByteArray utf8 = file.toUtf8();
    const char* argv[] = {"screenshot-to-file", utf8.constData(), "video", nullptr};
    if (mpv_command(mpv_.get(), argv) < 0) return {};
    return file;
}

void MpvItem::saveResumePosition() {
    if (!idle_ && duration_ > 0) command({QStringLiteral("write-watch-later-config")});
}

void MpvItem::setPaused(bool paused) { setMpvProperty(QStringLiteral("pause"), paused); }
void MpvItem::setVolume(double volume) { setMpvProperty(QStringLiteral("volume"), std::clamp(volume, 0.0, 150.0)); }
void MpvItem::setMuted(bool muted) { setMpvProperty(QStringLiteral("mute"), muted); }
void MpvItem::setSpeed(double speed) { setMpvProperty(QStringLiteral("speed"), std::clamp(speed, 0.1, 8.0)); }
