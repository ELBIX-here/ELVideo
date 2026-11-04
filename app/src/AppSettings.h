#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QTimer>

// User settings, stored as JSON in %APPDATA%\ELVideo\settings.json.
// QML writes the properties directly; every change is saved shortly after.
class AppSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString outputDir MEMBER outputDir NOTIFY changed)
    Q_PROPERTY(QString container MEMBER container NOTIFY changed)  // "fmp4" | "mp4"
    Q_PROPERTY(int fps MEMBER fps NOTIFY changed)
    Q_PROPERTY(int bitrateKbps MEMBER bitrateKbps NOTIFY changed)
    Q_PROPERTY(QString codec MEMBER codec NOTIFY changed)  // "h264" | "hevc" | "av1"
    Q_PROPERTY(bool preferHardware MEMBER preferHardware NOTIFY changed)
    Q_PROPERTY(bool captureCursor MEMBER captureCursor NOTIFY changed)
    Q_PROPERTY(bool captureSystemAudio MEMBER captureSystemAudio NOTIFY changed)
    Q_PROPERTY(QString systemAudioDevice MEMBER systemAudioDevice NOTIFY changed)  // empty: Windows default
    Q_PROPERTY(int systemAudioVolume MEMBER systemAudioVolume NOTIFY changed)      // percent
    // Window recordings: system sound is only that window's app (process loopback).
    Q_PROPERTY(bool appAudioOnly MEMBER appAudioOnly NOTIFY changed)
    Q_PROPERTY(bool captureMicrophone MEMBER captureMicrophone NOTIFY changed)
    Q_PROPERTY(QString microphoneDevice MEMBER microphoneDevice NOTIFY changed)
    Q_PROPERTY(int microphoneVolume MEMBER microphoneVolume NOTIFY changed)
    Q_PROPERTY(int audioBitrateKbps MEMBER audioBitrateKbps NOTIFY changed)
    Q_PROPERTY(bool webcamEnabled MEMBER webcamEnabled NOTIFY changed)
    Q_PROPERTY(QString webcamDevice MEMBER webcamDevice NOTIFY changed)  // empty: first camera
    Q_PROPERTY(int webcamCorner MEMBER webcamCorner NOTIFY changed)      // 0 TL, 1 TR, 2 BL, 3 BR
    Q_PROPERTY(int webcamSize MEMBER webcamSize NOTIFY changed)          // percent of output width
    Q_PROPERTY(bool webcamMirror MEMBER webcamMirror NOTIFY changed)
    Q_PROPERTY(QString hotkeyStartStop MEMBER hotkeyStartStop NOTIFY changed)
    Q_PROPERTY(QString hotkeyPause MEMBER hotkeyPause NOTIFY changed)
    Q_PROPERTY(QString hotkeyReplay MEMBER hotkeyReplay NOTIFY changed)
    Q_PROPERTY(int replaySeconds MEMBER replaySeconds NOTIFY changed)
    Q_PROPERTY(QString hotkeyScreenshot MEMBER hotkeyScreenshot NOTIFY changed)
    Q_PROPERTY(QString screenshotDir MEMBER screenshotDir NOTIFY changed)        // empty: default
    Q_PROPERTY(QString screenshotFormat MEMBER screenshotFormat NOTIFY changed)  // "png" | "jpg"
    Q_PROPERTY(bool screenshotCopy MEMBER screenshotCopy NOTIFY changed)         // to the clipboard
    Q_PROPERTY(bool screenshotSave MEMBER screenshotSave NOTIFY changed)         // to a file
    Q_PROPERTY(bool screenshotCursor MEMBER screenshotCursor NOTIFY changed)
    Q_PROPERTY(int screenshotDelay MEMBER screenshotDelay NOTIFY changed)        // seconds
    Q_PROPERTY(QString defaultScreenshotDir READ defaultScreenshotDir CONSTANT)
    Q_PROPERTY(bool hideWindowWhileRecording MEMBER hideWindowWhileRecording NOTIFY changed)
    Q_PROPERTY(bool showFloatingBar MEMBER showFloatingBar NOTIFY changed)
    Q_PROPERTY(bool closeToTray MEMBER closeToTray NOTIFY changed)
    Q_PROPERTY(QString appearance MEMBER appearance NOTIFY changed)  // "system" | "light" | "dark"
    Q_PROPERTY(QStringList libraryFolders MEMBER libraryFolders NOTIFY changed)  // video library roots
    // Display names of library roots that have no name in their path (Drive folders: "drive:<id>").
    Q_PROPERTY(QVariantMap libraryNames MEMBER libraryNames NOTIFY changed)
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
    QString effectiveScreenshotDir() const;
    static QString defaultScreenshotDir();
    static QString filePath();

    QString outputDir;
    QString container = QStringLiteral("fmp4");
    int fps = 60;
    int bitrateKbps = 12'000;
    QString codec = QStringLiteral("h264");
    bool preferHardware = true;
    bool captureCursor = true;
    bool captureSystemAudio = true;
    QString systemAudioDevice;
    int systemAudioVolume = 100;
    bool appAudioOnly = false;
    bool captureMicrophone = false;
    QString microphoneDevice;
    int microphoneVolume = 100;
    int audioBitrateKbps = 192;
    bool webcamEnabled = false;
    QString webcamDevice;
    int webcamCorner = 3;
    int webcamSize = 22;
    bool webcamMirror = true;
    QString hotkeyStartStop;
    QString hotkeyPause;
    QString hotkeyReplay;
    int replaySeconds = 30;
    QString hotkeyScreenshot;
    QString screenshotDir;
    QString screenshotFormat = QStringLiteral("png");
    bool screenshotCopy = true;
    bool screenshotSave = true;
    bool screenshotCursor = false;
    int screenshotDelay = 0;
    bool hideWindowWhileRecording = true;
    bool showFloatingBar = true;
    bool closeToTray = true;
    QString appearance = QStringLiteral("system");
    QStringList libraryFolders;
    QVariantMap libraryNames;
    QString lastSource;

    static constexpr auto kDefaultStartStop = "Ctrl+Shift+F9";
    static constexpr auto kDefaultPause = "Ctrl+Shift+F10";
    static constexpr auto kDefaultReplay = "Ctrl+Shift+F11";
    static constexpr auto kDefaultScreenshot = "Ctrl+Shift+F12";

signals:
    void changed();

private:
    QTimer saveTimer_;
    bool loading_ = false;
};
