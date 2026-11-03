#pragma once

#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

class AppSettings;
class Desktop;
class RecorderModel;

// Notification-area icon: shows the recording state and offers the main commands.
class TrayIcon : public QObject {
    Q_OBJECT

public:
    TrayIcon(RecorderModel& recorder, AppSettings& settings, Desktop& desktop, QObject* parent = nullptr);

    // Balloon message, e.g. "ELVideo is still running in the tray".
    Q_INVOKABLE void notify(const QString& title, const QString& message);

signals:
    void showRequested();
    void quitRequested();
    void screenshotRequested();

private:
    void update();

    RecorderModel& recorder_;
    QSystemTrayIcon tray_;
    QMenu menu_;
    QAction* toggleAction_ = nullptr;
    QAction* pauseAction_ = nullptr;
    QAction* replayAction_ = nullptr;
    QIcon idleIcon_;
    QIcon recordingIcon_;
    QIcon pausedIcon_;
};
