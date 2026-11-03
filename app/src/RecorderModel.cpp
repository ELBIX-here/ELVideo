#include "RecorderModel.h"

#include "AppSettings.h"
#include "SourceModel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QVariantMap>

#include <spdlog/spdlog.h>

RecorderModel::RecorderModel(AppSettings& settings, SourceModel& sources, QObject* parent)
    : QObject(parent), settings_(settings), sources_(sources), recorder_(elv::createRecorder()) {
    recorder_->setListener(this);
}

RecorderModel::~RecorderModel() {
    recorder_->setListener(nullptr);
    recorder_.reset();  // stops and finalizes an active recording
}

bool RecorderModel::start() {
    const auto source = sources_.selectedSource();
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
    rs.container = settings_.container == QLatin1String("mp4") ? elv::Container::Mp4 : elv::Container::FragmentedMp4;
    rs.outputPath = QDir::toNativeSeparators(path).toStdWString();
    rs.captureCursor = settings_.captureCursor;
    rs.audio.captureSystem = settings_.captureSystemAudio;
    rs.audio.systemDeviceId = settings_.systemAudioDevice.toStdWString();
    rs.audio.systemVolume = static_cast<float>(settings_.systemAudioVolume) / 100.0f;
    rs.audio.captureMicrophone = settings_.captureMicrophone;
    rs.audio.microphoneDeviceId = settings_.microphoneDevice.toStdWString();
    rs.audio.microphoneVolume = static_cast<float>(settings_.microphoneVolume) / 100.0f;
    rs.audio.bitrateKbps = static_cast<std::uint32_t>(settings_.audioBitrateKbps);

    pendingTitle_ = sources_.selectedTitle();
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

QVariantList RecorderModel::audioDevices(bool input) const {
    QVariantList list;
    for (const auto& d : elv::enumerateAudioDevices(input ? elv::AudioDeviceKind::Input : elv::AudioDeviceKind::Output)) {
        list.append(QVariantMap{{QStringLiteral("id"), QString::fromStdWString(d.id)},
                                {QStringLiteral("name"), QString::fromStdWString(d.name)},
                                {QStringLiteral("isDefault"), d.isDefault}});
    }
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
    QMetaObject::invokeMethod(this, [this, text] {
        outputInfo_ = text;
        recordingTitle_ = pendingTitle_;
        emit startedChanged();
    }, Qt::QueuedConnection);
}

void RecorderModel::onStats(const elv::RecorderStats& stats) {
    const qint64 elapsed = stats.elapsed.count();
    const auto dropped = static_cast<qint64>(stats.framesDropped);
    const double system = stats.systemPeak;
    const double microphone = stats.microphonePeak;
    QMetaObject::invokeMethod(this, [this, elapsed, dropped, system, microphone] {
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

void RecorderModel::onWarning(const std::string& message) {
    const QString text = QString::fromStdString(message);
    QMetaObject::invokeMethod(this, [this, text] { emit warningOccurred(text); }, Qt::QueuedConnection);
}
