// Player process: `ELVideo.exe --player [file]`. Runs Qt Quick on OpenGL for the mpv
// render API, separately from the recorder (which stays on D3D11), so a player
// problem never touches a recording.

#include "player/PlayerMain.h"

#include "AppSettings.h"
#include "player/SeekThumbnailer.h"
#include "Desktop.h"
#include "Fonts.h"
#include "WatchProgress.h"
#include "core/IRecorder.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>

#include <spdlog/spdlog.h>

#include <clocale>

int runPlayer(int argc, char* argv[]) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ELVideo"));
    QApplication::setApplicationVersion(QString::fromLatin1(elv::version()));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/elvideo.ico")));
    loadAppFonts();
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    std::setlocale(LC_NUMERIC, "C");  // libmpv refuses to start otherwise; Qt set it from the system

    // The file to open: the first argument after --player that is not an option.
    QString file;
    const QStringList args = QApplication::arguments();
    for (qsizetype i = args.indexOf(QStringLiteral("--player")) + 1; i < args.size(); ++i)
        if (!args[i].startsWith(QLatin1String("--"))) {
            file = args[i];
            break;
        }
    spdlog::info("player starting{}", file.isEmpty() ? std::string() : ": " + file.toStdString());

    AppSettings settings;
    settings.load();
    Desktop desktop;
    WatchProgress progress;

    SeekThumbnailer seekThumbs;
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("seekthumb"), new SeekThumbnailProvider(seekThumbs));
    auto* context = engine.rootContext();
    context->setContextProperty(QStringLiteral("seekThumbs"), &seekThumbs);
    context->setContextProperty(QStringLiteral("desktop"), &desktop);
    context->setContextProperty(QStringLiteral("appSettings"), &settings);
    context->setContextProperty(QStringLiteral("progress"), &progress);
    context->setContextProperty(QStringLiteral("initialFile"),
                                file.isEmpty() ? QString() : QUrl::fromLocalFile(QFileInfo(file).absoluteFilePath()).toString());
#ifndef NDEBUG
    // Test hook: ELV_TEST_SEEKTHUMB=<png> saves the hover preview for 5 s once a file is open.
    if (const QString out = qEnvironmentVariable("ELV_TEST_SEEKTHUMB"); !out.isEmpty()) {
        auto* clock = new QElapsedTimer;
        QObject::connect(&seekThumbs, &SeekThumbnailer::fileChanged, &app, [&seekThumbs, clock] {
            clock->start();
            seekThumbs.request(5.0);
        });
        QObject::connect(&seekThumbs, &SeekThumbnailer::ready, &app, [&seekThumbs, out, clock] {
            seekThumbs.image().save(out);
            spdlog::info("seek thumbnail test: t={} {}x{} after {} ms", seekThumbs.time(), seekThumbs.image().width(),
                         seekThumbs.image().height(), clock->elapsed());
        });
    }
#endif
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("ELVideo", "Player");
    return app.exec();
}
