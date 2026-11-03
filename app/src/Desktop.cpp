#include "Desktop.h"

#include <QDesktopServices>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QWindow>

#include <windows.h>

#include <dwmapi.h>

#include <shellapi.h>
#include <shlobj.h>

#include <spdlog/spdlog.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

bool Desktop::excludeFromCapture(QWindow* window) const {
    if (!window) return false;
#ifndef NDEBUG
    // Test hook: ELV_TEST_CAPTURABLE=1 keeps the windows capturable for UI screenshots.
    if (qEnvironmentVariableIntValue("ELV_TEST_CAPTURABLE")) return true;
#endif
    const auto hwnd = reinterpret_cast<HWND>(window->winId());  // creates the native window if needed
    if (SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)) return true;
    spdlog::warn("WDA_EXCLUDEFROMCAPTURE failed ({}), falling back to WDA_MONITOR", GetLastError());
    return SetWindowDisplayAffinity(hwnd, WDA_MONITOR) != FALSE;
}

void Desktop::useDarkTitleBar(QWindow* window, bool dark) const {
    if (!window) return;
    const BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
}

void Desktop::openFile(const QString& path) const { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }

bool Desktop::hasPlayer() const {
#ifdef ELV_HAVE_MPV
    return true;
#else
    return false;
#endif
}

void Desktop::openInPlayer(const QString& path) const {
    if (!hasPlayer()) {
        if (!path.isEmpty()) openFile(path);
        return;
    }
    QStringList args{QStringLiteral("--player")};
    if (!path.isEmpty()) args << QDir::toNativeSeparators(path);
    QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
}

void Desktop::revealFile(const QString& path) const {
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(native.c_str())) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
        return;
    }
    openFolder(QFileInfo(path).absolutePath());
}

void Desktop::openFolder(const QString& path) const {
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString Desktop::toLocalFile(const QUrl& url) const { return url.toLocalFile(); }
QUrl Desktop::toUrl(const QString& path) const { return QUrl::fromLocalFile(path); }
QString Desktop::fileName(const QString& path) const { return QFileInfo(path).fileName(); }
QString Desktop::nativePath(const QString& path) const { return QDir::toNativeSeparators(path); }
