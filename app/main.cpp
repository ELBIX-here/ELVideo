#include "core/IRecorder.h"
#include "src/AppSettings.h"
#include "src/CrashHandler.h"
#include "src/Desktop.h"
#include "src/Hotkeys.h"
#include "src/RecorderModel.h"
#include "src/SourceModel.h"
#include "src/TrayIcon.h"

#include <QApplication>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>

#include <windows.h>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

namespace {

// %LOCALAPPDATA%\ELVideo\logs\elvideo.log (engine and Qt messages), 3 x 5 MB.
void setupLogging() {
    const QString dir = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + QStringLiteral("/ELVideo/logs");
    QDir().mkpath(dir);
    try {
        auto logger = spdlog::rotating_logger_mt("elvideo", QDir::toNativeSeparators(dir + QStringLiteral("/elvideo.log")).toLocal8Bit().toStdString(),
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

    // One instance owns the hotkeys and the tray icon; a second launch shows the first.
    const HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\ELVideo.SingleInstance");
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

    setupLogging();
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

    Hotkeys hotkeys;
    const auto applyHotkeys = [&] {
        hotkeys.setSequences(settings.hotkeyStartStop, settings.hotkeyPause, settings.hotkeyReplay);
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
    });
    QObject::connect(&tray, &TrayIcon::quitRequested, &app, &QCoreApplication::quit);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("preview"), new PreviewProvider(recorder.engine()));
    auto* context = engine.rootContext();
    context->setContextProperty(QStringLiteral("engineVersion"), QString::fromLatin1(elv::version()));
    context->setContextProperty(QStringLiteral("captureSupported"), platform.captureSupported);
    context->setContextProperty(QStringLiteral("appSettings"), &settings);
    context->setContextProperty(QStringLiteral("sources"), &sources);
    context->setContextProperty(QStringLiteral("recorder"), &recorder);
    context->setContextProperty(QStringLiteral("hotkeys"), &hotkeys);
    context->setContextProperty(QStringLiteral("tray"), &tray);
    context->setContextProperty(QStringLiteral("desktop"), &desktop);
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
