#include "core/IRecorder.h"
#include "src/AppSettings.h"
#include "src/CrashHandler.h"
#include "src/Desktop.h"
#include "src/Hotkeys.h"
#include "src/LibraryModel.h"
#include "src/RecorderModel.h"
#include "src/ScreenCapture.h"
#include "src/SourceModel.h"
#include "src/TrayIcon.h"
#ifdef ELV_HAVE_MPV
#include "src/player/PlayerMain.h"
#endif

#include <QApplication>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>

#include <windows.h>

#include <string_view>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

namespace {

// %LOCALAPPDATA%\ELVideo\logs\<name>.log (engine and Qt messages), 3 x 5 MB. The
// recorder and player processes use separate files so rotation never collides.
void setupLogging(const QString& name) {
    const QString dir = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + QStringLiteral("/ELVideo/logs");
    QDir().mkpath(dir);
    try {
        auto logger = spdlog::rotating_logger_mt("elvideo", QDir::toNativeSeparators(dir + QStringLiteral("/") + name + QStringLiteral(".log")).toLocal8Bit().toStdString(),
                                                 5 * 1024 * 1024, 3);
        logger->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(logger);
        spdlog::flush_every(std::chrono::seconds(2));
    } catch (const spdlog::spdlog_ex&) {
        // Logging is best effort; keep the default console logger.
    }

    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
        const auto text = message.toStdString();
        switch (type) {
        case QtDebugMsg: spdlog::debug("qt: {}", text); break;
        case QtInfoMsg: spdlog::info("qt: {}", text); break;
        case QtWarningMsg: spdlog::warn("qt: {}", text); break;
        default: spdlog::error("qt: {}", text); break;
        }
    });
}

}  // namespace

int main(int argc, char* argv[]) {
    CrashHandler::install(QString::fromLatin1(elv::version()));

    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) != "--player") continue;
#ifdef ELV_HAVE_MPV
        setupLogging(QStringLiteral("elvideo-player"));
        return runPlayer(argc, argv);
#else
        MessageBoxW(nullptr, L"This ELVideo build has no video player (libmpv was missing).", L"ELVideo", MB_ICONWARNING);
        return 1;
#endif
    }

    // One instance owns the hotkeys and the tray icon; a second launch shows the first.
    const wchar_t* instanceName = L"Local\\ELVideo.SingleInstance";
#ifndef NDEBUG
    // Test hook: ELV_TEST_INSTANCE=1 runs a debug build beside an installed copy
    // (its hotkeys then fail to register; the installed copy keeps them).
    if (qEnvironmentVariableIntValue("ELV_TEST_INSTANCE")) instanceName = L"Local\\ELVideo.SingleInstance.Test";
#endif
    const HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, instanceName);
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        Hotkeys::activateRunningInstance();
        return 0;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ELVideo"));
    QApplication::setApplicationVersion(QString::fromLatin1(elv::version()));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/elvideo.ico")));
    QApplication::setQuitOnLastWindowClosed(false);  // the tray keeps running
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    setupLogging(QStringLiteral("elvideo"));
    const auto platform = elv::queryPlatform();
    spdlog::info("ELVideo {} starting on Windows {}", elv::version(), platform.windowsBuild);

    // Dumps left by earlier runs; the main window offers to open the folder.
    const QStringList crashDumps = CrashHandler::takeNewDumps();
    for (const auto& dump : crashDumps) spdlog::warn("previous run crashed: {}", dump.toStdString());

    // Test hook: --crash-test <ui|stack|abort|terminate|pure|thread> [seconds=3]
    const QStringList args = QApplication::arguments();
    if (const auto i = args.indexOf(QStringLiteral("--crash-test")); i > 0 && i + 1 < args.size()) {
        const QString kind = args[i + 1];
        const int seconds = i + 2 < args.size() ? args[i + 2].toInt() : 3;
        spdlog::warn("crash test '{}' in {} s", kind.toStdString(), seconds);
        spdlog::default_logger()->flush();
        QTimer::singleShot(seconds * 1000, [kind] { CrashHandler::crashForTest(kind); });
    }

    AppSettings settings;
    settings.load();

    SourceModel sources;
    sources.setSelectedId(settings.lastSource);
    sources.refresh();

    RecorderModel recorder(settings, sources);
    Desktop desktop;
    TrayIcon tray(recorder, settings, desktop);

    ScreenCapture screenCapture(settings);
    LibraryModel library(settings);
    // From the tray menu: give the menu time to close so it is not in the picture.
    QObject::connect(&tray, &TrayIcon::screenshotRequested, &screenCapture,
                     [&] { QTimer::singleShot(250, &screenCapture, &ScreenCapture::start); });

    Hotkeys hotkeys;
    const auto applyHotkeys = [&] {
        hotkeys.setSequences(settings.hotkeyStartStop, settings.hotkeyPause, settings.hotkeyReplay,
                             settings.hotkeyScreenshot);
    };
    QObject::connect(&settings, &AppSettings::changed, &hotkeys, applyHotkeys);
    applyHotkeys();
    QObject::connect(&hotkeys, &Hotkeys::activated, &recorder, [&](int id) {
        if (id == Hotkeys::StartStop)
            recorder.toggle();
        else if (id == Hotkeys::Pause)
            recorder.togglePause();
        else if (id == Hotkeys::Replay)
            recorder.replayHotkey();
        else if (id == Hotkeys::Screenshot)
            screenCapture.start();
    });
    QObject::connect(&tray, &TrayIcon::quitRequested, &app, &QCoreApplication::quit);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("preview"), new PreviewProvider(recorder.engine()));
    engine.addImageProvider(QStringLiteral("frozen"), new FrozenImageProvider(screenCapture));
    engine.addImageProvider(QStringLiteral("library"), new LibraryThumbnailProvider);
    auto* context = engine.rootContext();
    context->setContextProperty(QStringLiteral("engineVersion"), QString::fromLatin1(elv::version()));
    context->setContextProperty(QStringLiteral("captureSupported"), platform.captureSupported);
    context->setContextProperty(QStringLiteral("appSettings"), &settings);
    context->setContextProperty(QStringLiteral("sources"), &sources);
    context->setContextProperty(QStringLiteral("recorder"), &recorder);
    context->setContextProperty(QStringLiteral("hotkeys"), &hotkeys);
    context->setContextProperty(QStringLiteral("tray"), &tray);
    context->setContextProperty(QStringLiteral("desktop"), &desktop);
    context->setContextProperty(QStringLiteral("screenCapture"), &screenCapture);
    context->setContextProperty(QStringLiteral("library"), &library);
#ifdef NDEBUG
    context->setContextProperty(QStringLiteral("debugBuild"), false);
#else
    context->setContextProperty(QStringLiteral("debugBuild"), true);
#endif
    context->setContextProperty(QStringLiteral("crashDumps"), crashDumps);
    context->setContextProperty(QStringLiteral("crashDumpDir"), CrashHandler::dumpDirectory());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("ELVideo", "Main");
    // Codec availability for the settings page; after the splash so startup stays quick.
    QTimer::singleShot(4000, &recorder, &RecorderModel::probeCodecs);

    const int result = app.exec();
    spdlog::info("ELVideo exiting");
    spdlog::shutdown();
    return result;
}
