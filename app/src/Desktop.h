#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QWindow;

// Small Windows shell helpers for QML.
class Desktop : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Keeps the window out of every screen capture, including our own recordings
    // (WDA_EXCLUDEFROMCAPTURE, Windows 10 2004+). Older systems fall back to
    // WDA_MONITOR, which shows the window as black. Returns false if both fail.
    Q_INVOKABLE bool excludeFromCapture(QWindow* window) const;
    // Dark title bar to match the app, regardless of the system theme.
    Q_INVOKABLE void useDarkTitleBar(QWindow* window) const;

    Q_INVOKABLE void openFile(const QString& path) const;
    Q_INVOKABLE void revealFile(const QString& path) const;  // Explorer with the file selected
    Q_INVOKABLE void openFolder(const QString& path) const;  // created if missing
    Q_INVOKABLE QString toLocalFile(const QUrl& url) const;
    Q_INVOKABLE QUrl toUrl(const QString& path) const;
    Q_INVOKABLE QString fileName(const QString& path) const;
    Q_INVOKABLE QString nativePath(const QString& path) const;
};
