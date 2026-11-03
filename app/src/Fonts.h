#pragma once

#include <QFontDatabase>
#include <QStringList>

#include <spdlog/spdlog.h>

// Registers the bundled Pretendard faces (SIL OFL 1.1, app/resources/fonts) so
// QML can use the "Pretendard" family. Call after the QGuiApplication exists.
inline void loadAppFonts() {
    for (const auto* name : {"Light", "Regular", "SemiBold"}) {
        const QString path = QStringLiteral(":/fonts/Pretendard-%1.otf").arg(QLatin1String(name));
        if (QFontDatabase::addApplicationFont(path) < 0) spdlog::warn("cannot load font {}", path.toStdString());
    }
}
