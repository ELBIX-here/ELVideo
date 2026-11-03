#include "RecorderModel.h"

#include "AppSettings.h"
#include "SourceModel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QVariantMap>
#include <QCoreApplication>

#include <windows.h>
#include <QPointer>

#include <optional>
#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

RecorderModel::RecorderModel(AppSettings& settings, SourceModel& sources, QObject* parent)
    : QObject(parent), settings_(settings), sources_(sources), recorder_(elv::createRecorder()) {
    recorder_->setListener(this);
}

RecorderModel::~RecorderModel() {
    recorder_->setListener(nullptr);
    recorder_.reset();  // stops and finalizes an active recording
}

bool RecorderModel::start() { return startWith(false); }
bool RecorderModel::startReplay() { return startWith(true); }

bool RecorderModel::saveReplay() {
    if (!replayMode()) return false;
    const QString name =
        QStringLiteral("ELVideo_Replay_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    const QDir dir(settings_.effectiveOutputDir());
    QString path = dir.filePath(name + QStringLiteral(".mp4"));
    for (int n = 2; QFileInfo::exists(path); ++n) path = dir.filePath(QStringLiteral("%1_%2.mp4").arg(name).arg(n));
    return recorder_->saveReplay(QDir::toNativeSeparators(path).toStdWString());
}

void RecorderModel::replayHotkey() {
    if (replayMode())
        saveReplay();
    else if (!active() && !busy())
        startReplay();
}

bool RecorderModel::startWith(bool replay) {
    const auto region = sources_.activeRegion();
    if (sources_.regionMode() && !region) {
        emit errorOccurred(tr("녹화할 영역을 먼저 선택하세요."));
        return false;
    }
    const auto source = region ? std::optional(region->first) : sources_.selectedSource();
    if (!source) {
        emit errorOccurred(tr("녹화할 화면이 없습니다."));
        return false;
    }

    const QString dir = settings_.effectiveOutputDir();
    const QString extension = QStringLiteral(".mp4");
    const QString name =
        QStringLiteral("ELVideo_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    QString path = QDir(dir).filePath(name + extension);
    for (int n = 2; QFileInfo::exists(path); ++n)  // two recordings within one second
        path = QDir(dir).filePath(QStringLiteral("%1_%2%3").arg(name).arg(n).arg(extension));

    elv::RecordSettings rs;
    rs.source = *source;
    rs.video.fps = static_cast<std::uint32_t>(settings_.fps);
    rs.video.bitrateKbps = static_cast<std::uint32_t>(settings_.bitrateKbps);
    rs.video.preferHardware = settings_.preferHardware;
    rs.video.codec = settings_.codec == QLatin1String("hevc")  ? elv::VideoCodec::HEVC
                     : settings_.codec == QLatin1String("av1") ? elv::VideoCodec::AV1
                                                               : elv::VideoCodec::H264;
    rs.container = settings_.container == QLatin1String("mp4") ? elv::Container::Mp4 : elv::Container::FragmentedMp4;
    rs.outputPath = QDir::toNativeSeparators(path).toStdWString();
    if (replay) rs.replaySeconds = settings_.replaySeconds;
    rs.captureCursor = settings_.captureCursor;
    if (region) rs.region = region->second;
    rs.audio.captureSystem = settings_.captureSystemAudio;
    rs.audio.systemDeviceId = settings_.systemAudioDevice.toStdWString();
    rs.audio.systemVolume = static_cast<float>(settings_.systemAudioVolume) / 100.0f;
    if (settings_.appAudioOnly && rs.source.kind == elv::SourceKind::Window) {
        DWORD pid = 0;
        GetWindowThreadProcessId(reinterpret_cast<HWND>(rs.source.handle), &pid);
        rs.audio.systemProcessId = pid;
    }
    rs.audio.captureMicrophone = settings_.captureMicrophone;
    rs.audio.microphoneDeviceId = settings_.microphoneDevice.toStdWString();
    rs.audio.microphoneVolume = static_cast<float>(settings_.microphoneVolume) / 100.0f;
    rs.audio.bitrateKbps = static_cast<std::uint32_t>(settings_.audioBitrateKbps);
    if (settings_.webcamEnabled) {
        elv::WebcamOverlay webcam;
        webcam.deviceId = settings_.webcamDevice.toStdWString();
        webcam.corner = static_cast<elv::Corner>(settings_.webcamCorner);
        webcam.widthFraction = static_cast<float>(settings_.webcamSize) / 100.0f;
        webcam.mirror = settings_.webcamMirror;
        rs.webcam = webcam;
    }

    pendingTitle_ = sources_.recordingTitle();
    pendingReplay_ = replay;
    settings_.lastSource = sources_.selectedId();
    emit settings_.changed();
    return recorder_->start(rs);
}

bool RecorderModel::stop() { return recorder_->stop(); }
bool RecorderModel::pause() { return recorder_->pause(); }
bool RecorderModel::resume() { return recorder_->resume(); }

void RecorderModel::toggle() {
    if (active()) {
        stop();
    } else if (!busy()) {
        start();
    }
}

void RecorderModel::togglePause() {
    if (state_ == elv::RecorderState::Recording)
        pause();
    else if (state_ == elv::RecorderState::Paused)
        resume();
}

void RecorderModel::probeCodecs() {
    if (probing_) return;
    std::optional<elv::CaptureSource> primary;
    for (const auto& s : elv::enumerateCaptureSources())
        if (s.kind == elv::SourceKind::Monitor) {
            primary = s;  // monitors come first, primary first
            break;
        }
    if (!primary) return;
    probing_ = true;
    const bool preferHardware = settings_.preferHardware;
    std::thread([this, source = *primary, preferHardware, guard = QPointer<RecorderModel>(this)] {
        QVariantMap support;
        const std::pair<const char*, elv::VideoCodec> codecs[] = {
            {"h264", elv::VideoCodec::H264}, {"hevc", elv::VideoCodec::HEVC}, {"av1", elv::VideoCodec::AV1}};
        for (const auto& [key, codec] : codecs) {
            const auto probe = elv::probeVideoEncoder(codec, source, preferHardware);
            QString label = QString::fromLatin1(elv::codecName(codec));
            label += probe.supported ? QStringLiteral(" — ") + QString::fromStdWString(probe.encoderName) +
                                           (probe.hardware ? QString() : tr(" (소프트웨어)"))
                                     : tr(" — 이 PC에서 사용할 수 없음");
            support.insert(QString::fromLatin1(key),
                           QVariantMap{{QStringLiteral("supported"), probe.supported}, {QStringLiteral("label"), label}});
        }
        QMetaObject::invokeMethod(qApp, [guard, support] {
            if (!guard) return;
            guard->codecSupport_ = support;
            guard->probing_ = false;
            emit guard->codecSupportChanged();
        }, Qt::QueuedConnection);
    }).detach();
}

QVariantList RecorderModel::audioDevices(bool input) const {
    QVariantList list;
    for (const auto& d : elv::enumerateAudioDevices(input ? elv::AudioDeviceKind::Input : elv::AudioDeviceKind::Output)) {
        list.append(QVariantMap{{QStringLiteral("id"), QString::fromStdWString(d.id)},
                                {QStringLiteral("name"), QString::fromStdWString(d.name)},
                                {QStringLiteral("isDefault"), d.isDefault}});
    }
    return list;
}

QVariantList RecorderModel::webcams() const {
    QVariantList list;
    for (const auto& w : elv::enumerateWebcams())
        list.append(QVariantMap{{QStringLiteral("id"), QString::fromStdWString(w.id)},
                                {QStringLiteral("name"), QString::fromStdWString(w.name)}});
    return list;
}

QString RecorderModel::state() const {
    switch (state_) {
    case elv::RecorderState::Idle: return QStringLiteral("idle");
    case elv::RecorderState::Starting: return QStringLiteral("starting");
    case elv::RecorderState::Recording: return QStringLiteral("recording");
    case elv::RecorderState::Paused: return QStringLiteral("paused");
    case elv::RecorderState::Stopping: return QStringLiteral("stopping");
    case elv::RecorderState::Error: return QStringLiteral("error");
    }
    return {};
}

// --- engine thread -> UI thread ---------------------------------------------

void RecorderModel::onStateChanged(elv::RecorderState state) {
    QMetaObject::invokeMethod(this, [this, state] {
        if (state == state_) return;
        state_ = state;
        if (replayMode_ && !active()) {
            replayMode_ = false;
            emit startedChanged();
        }
        if (state == elv::RecorderState::Starting) {
            elapsedMs_ = 0;
            framesDropped_ = 0;
            systemLevel_ = microphoneLevel_ = -1;
            emit statsChanged();
        }
        emit stateChanged();
    }, Qt::QueuedConnection);
}

void RecorderModel::onStarted(const elv::RecordingInfo& info) {
    QString text = QStringLiteral("%1×%2 · %3fps").arg(info.width).arg(info.height).arg(info.fps);
    if (!info.encoderName.empty()) text += QStringLiteral(" · ") + QString::fromStdWString(info.encoderName);
    if (!info.hardwareEncoder) text += tr(" (소프트웨어)");
    const bool replay = info.replay;
    QMetaObject::invokeMethod(this, [this, text, replay] {
        outputInfo_ = text;
        recordingTitle_ = pendingTitle_;
        replayMode_ = replay;
        emit startedChanged();
    }, Qt::QueuedConnection);
}

void RecorderModel::onStats(const elv::RecorderStats& stats) {
    const qint64 elapsed = stats.elapsed.count();
    const auto dropped = static_cast<qint64>(stats.framesDropped);
    const double system = stats.systemPeak;
    const double microphone = stats.microphonePeak;
    const double buffered = stats.replayBufferedSeconds;
    QMetaObject::invokeMethod(this, [this, elapsed, dropped, system, microphone, buffered] {
        replayBuffered_ = buffered;
        elapsedMs_ = elapsed;
        framesDropped_ = dropped;
        systemLevel_ = system;
        microphoneLevel_ = microphone;
        emit statsChanged();
    }, Qt::QueuedConnection);
}

void RecorderModel::onFinished(const elv::RecordingInfo& info, const elv::RecorderStats& stats,
                               elv::StopReason reason) {
    const QString path = QString::fromStdWString(info.path.wstring());
    const qint64 duration = stats.elapsed.count();
    const QString why = reason == elv::StopReason::User           ? QStringLiteral("user")
                        : reason == elv::StopReason::SourceClosed ? QStringLiteral("sourceClosed")
                                                                  : QStringLiteral("error");
    spdlog::info("recording saved: {} ({} ms, {})", path.toStdString(), duration, why.toStdString());
    QMetaObject::invokeMethod(this, [this, path, why, duration] {
        elapsedMs_ = duration;
        emit statsChanged();
        emit finished(path, why, duration);
    }, Qt::QueuedConnection);
}

void RecorderModel::onError(const std::string& message) {
    const QString text = QString::fromStdString(message);
    QMetaObject::invokeMethod(this, [this, text] { emit errorOccurred(text); }, Qt::QueuedConnection);
}

void RecorderModel::onReplaySaved(const std::filesystem::path& path, std::chrono::milliseconds length) {
    const QString file = QString::fromStdWString(path.wstring());
    const qint64 ms = length.count();
    QMetaObject::invokeMethod(this, [this, file, ms] { emit replaySaved(file, ms); }, Qt::QueuedConnection);
}

void RecorderModel::onWarning(const std::string& message) {
    const QString text = QString::fromStdString(message);
    QMetaObject::invokeMethod(this, [this, text] { emit warningOccurred(text); }, Qt::QueuedConnection);
}

namespace {

class PreviewResponse : public QQuickImageResponse {
public:
    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image_);
    }
    QString errorString() const override { return image_.isNull() ? QStringLiteral("not recording") : QString(); }
    void deliver(QImage image) {
        image_ = std::move(image);
        emit finished();
    }

private:
    QImage image_;
};

}  // namespace

PreviewProvider::PreviewProvider(elv::IRecorder& recorder) : recorder_(recorder) { pool_.setMaxThreadCount(1); }

QQuickImageResponse* PreviewProvider::requestImageResponse(const QString&, const QSize& requestedSize) {
    auto* response = new PreviewResponse;
    const QSize size = requestedSize.isValid() ? requestedSize : QSize(960, 540);
    pool_.start([this, response, size] {
        QImage image;
        if (const auto frame = recorder_.preview(static_cast<std::uint32_t>(size.width()),
                                                 static_cast<std::uint32_t>(size.height()))) {
            image = QImage(frame->bgra.data(), static_cast<int>(frame->width), static_cast<int>(frame->height),
                           static_cast<int>(frame->width * 4), QImage::Format_RGB32)
                        .copy();
        }
        response->deliver(std::move(image));
    });
    return response;
}
