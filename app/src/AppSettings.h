#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

// User settings, stored as JSON in %APPDATA%\ELVideo\settings.json.
// QML writes the properties directly; every change is saved shortly after.
class AppSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString outputDir MEMBER outputDir NOTIFY changed)
    Q_PROPERTY(QString container MEMBER container NOTIFY changed)  // "fmp4" | "mp4"
    Q_PROPERTY(int fps MEMBER fps NOTIFY changed)
    Q_PROPERTY(int bitrateKbps MEMBER bitrateKbps NOTIFY changed)
    Q_PROPERTY(bool preferHardware MEMBER preferHardware NOTIFY changed)
    Q_PROPERTY(bool captureCursor MEMBER captureCursor NOTIFY changed)
    Q_PROPERTY(bool captureSystemAudio MEMBER captureSystemAudio NOTIFY changed)
    Q_PROPERTY(QString systemAudioDevice MEMBER systemAudioDevice NOTIFY changed)  // empty: Windows default
    Q_PROPERTY(int systemAudioVolume MEMBER systemAudioVolume NOTIFY changed)      // percent
    Q_PROPERTY(bool captureMicrophone MEMBER captureMicrophone NOTIFY changed)
    Q_PROPERTY(QString microphoneDevice MEMBER microphoneDevice NOTIFY changed)
    Q_PROPERTY(int microphoneVolume MEMBER microphoneVolume NOTIFY changed)
    Q_PROPERTY(int audioBitrateKbps MEMBER audioBitrateKbps NOTIFY changed)
    Q_PROPERTY(QString hotkeyStartStop MEMBER hotkeyStartStop NOTIFY changed)
    Q_PROPERTY(QString hotkeyPause MEMBER hotkeyPause NOTIFY changed)
    Q_PROPERTY(bool hideWindowWhileRecording MEMBER hideWindowWhileRecording NOTIFY changed)
    Q_PROPERTY(bool showFloatingBar MEMBER showFloatingBar NOTIFY changed)
    Q_PROPERTY(bool closeToTray MEMBER closeToTray NOTIFY changed)
    Q_PROPERTY(QString lastSource MEMBER lastSource NOTIFY changed)
    Q_PROPERTY(QString defaultOutputDir READ defaultOutputDir CONSTANT)
    Q_PROPERTY(QString filePath READ filePath CONSTANT)

public:
    explicit AppSettings(QObject* parent = nullptr);
    ~AppSettings() override;

    void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void resetHotkeys();

    // outputDir, or the default when it is empty.
    QString effectiveOutputDir() const;
    static QString defaultOutputDir();
    static QString filePath();

    QString outputDir;
    QString container = QStringLiteral("fmp4");
    int fps = 60;
    int bitrateKbps = 12'000;
    bool preferHardware = true;
    bool captureCursor = true;
    bool captureSystemAudio = true;
    QString systemAudioDevice;
    int systemAudioVolume = 100;
    bool captureMicrophone = false;
    QString microphoneDevice;
    int microphoneVolume = 100;
    int audioBitrateKbps = 192;
    QString hotkeyStartStop;
    QString hotkeyPause;
    bool hideWindowWhileRecording = true;
    bool showFloatingBar = true;
    bool closeToTray = true;
    QString lastSource;

    static constexpr auto kDefaultStartStop = "Ctrl+Shift+F9";
    static constexpr auto kDefaultPause = "Ctrl+Shift+F10";

signals:
    void changed();

private:
    QTimer saveTimer_;
    bool loading_ = false;
};
