#pragma once

#include "core/IRecorder.h"

#include <QObject>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class AppSettings;
class SourceModel;

// QML face of the engine. Engine callbacks arrive on the engine thread and are
// queued to this object's (the UI) thread before touching any property.
class RecorderModel : public QObject, private elv::IRecorderListener {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)  // idle starting recording paused stopping error
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)    // recording or paused
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)        // starting or stopping
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY statsChanged)
    Q_PROPERTY(qint64 framesDropped READ framesDropped NOTIFY statsChanged)
    Q_PROPERTY(QString outputInfo READ outputInfo NOTIFY startedChanged)  // "1920×1080 · 60fps · Intel QSV"
    Q_PROPERTY(QString recordingTitle READ recordingTitle NOTIFY startedChanged)
    // Replay buffer running (instead of a normal recording) and how much it holds.
    Q_PROPERTY(bool replayMode READ replayMode NOTIFY startedChanged)
    Q_PROPERTY(double replayBufferedSeconds READ replayBufferedSeconds NOTIFY statsChanged)
    // Audio peak (0..1) of the last stats interval; -1 when the source is not recorded.
    Q_PROPERTY(double systemLevel READ systemLevel NOTIFY statsChanged)
    Q_PROPERTY(double microphoneLevel READ microphoneLevel NOTIFY statsChanged)
    // {h264|hevc|av1: {supported, label}} from a test encode on the primary monitor's
    // GPU; empty until probeCodecs() finishes.
    Q_PROPERTY(QVariantMap codecSupport READ codecSupport NOTIFY codecSupportChanged)

public:
    RecorderModel(AppSettings& settings, SourceModel& sources, QObject* parent = nullptr);
    ~RecorderModel() override;

    Q_INVOKABLE bool start();
    Q_INVOKABLE bool stop();
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool resume();
    Q_INVOKABLE void toggle();       // start or stop
    Q_INVOKABLE void togglePause();  // pause or resume
    // Replay buffer: start keeping the last N seconds of the selected source; save
    // writes them to the output folder.
    Q_INVOKABLE bool startReplay();
    Q_INVOKABLE bool saveReplay();
    Q_INVOKABLE void replayHotkey();  // save while buffering, otherwise start buffering

    // Test-encodes each codec in the background (about a second per codec).
    Q_INVOKABLE void probeCodecs();
    QVariantMap codecSupport() const { return codecSupport_; }

    // [{id, name, isDefault}], default device first. input: microphones; else outputs.
    Q_INVOKABLE QVariantList audioDevices(bool input) const;
    // [{id, name}] of the cameras Media Foundation can open.
    Q_INVOKABLE QVariantList webcams() const;

    elv::IRecorder& engine() { return *recorder_; }

    QString state() const;
    bool active() const { return state_ == elv::RecorderState::Recording || state_ == elv::RecorderState::Paused; }
    bool busy() const { return state_ == elv::RecorderState::Starting || state_ == elv::RecorderState::Stopping; }
    bool paused() const { return state_ == elv::RecorderState::Paused; }
    qint64 elapsedMs() const { return elapsedMs_; }
    qint64 framesDropped() const { return framesDropped_; }
    QString outputInfo() const { return outputInfo_; }
    QString recordingTitle() const { return recordingTitle_; }
    bool replayMode() const { return replayMode_; }  // cleared when the session ends
    double replayBufferedSeconds() const { return replayBuffered_; }
    double systemLevel() const { return systemLevel_; }
    double microphoneLevel() const { return microphoneLevel_; }

signals:
    void stateChanged();
    void statsChanged();
    void startedChanged();
    // reason: "user" | "sourceClosed" | "error"
    void finished(const QString& path, const QString& reason, qint64 durationMs);
    void errorOccurred(const QString& message);
    void warningOccurred(const QString& message);
    void replaySaved(const QString& path, qint64 lengthMs);
    void codecSupportChanged();

private:
    void onStateChanged(elv::RecorderState state) override;
    void onStarted(const elv::RecordingInfo& info) override;
    void onStats(const elv::RecorderStats& stats) override;
    void onFinished(const elv::RecordingInfo& info, const elv::RecorderStats& stats, elv::StopReason reason) override;
    void onError(const std::string& message) override;
    void onWarning(const std::string& message) override;
    void onReplaySaved(const std::filesystem::path& path, std::chrono::milliseconds length) override;
    bool startWith(bool replay);

    AppSettings& settings_;
    SourceModel& sources_;
    std::unique_ptr<elv::IRecorder> recorder_;
    elv::RecorderState state_ = elv::RecorderState::Idle;
    qint64 elapsedMs_ = 0;
    qint64 framesDropped_ = 0;
    double systemLevel_ = -1;
    double microphoneLevel_ = -1;
    bool replayMode_ = false;
    bool pendingReplay_ = false;
    double replayBuffered_ = 0;
    QString outputInfo_;
    QString recordingTitle_;
    QString pendingTitle_;
    QVariantMap codecSupport_;
    bool probing_ = false;
};

// image://preview/<n>: the frame being recorded, from IRecorder::preview().
class PreviewProvider : public QQuickAsyncImageProvider {
public:
    explicit PreviewProvider(elv::IRecorder& recorder);
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    elv::IRecorder& recorder_;
    QThreadPool pool_;
};
