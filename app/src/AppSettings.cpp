#include "AppSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>

namespace {

QString str(const nlohmann::json& j, const char* key, const QString& fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? QString::fromStdString(it->get<std::string>()) : fallback;
}

template <typename T>
T value(const nlohmann::json& j, const char* key, T fallback) {
    const auto it = j.find(key);
    if (it == j.end()) return fallback;
    try {
        return it->get<T>();
    } catch (const nlohmann::json::exception&) {
        return fallback;
    }
}

}  // namespace

AppSettings::AppSettings(QObject* parent)
    : QObject(parent), hotkeyStartStop(QString::fromLatin1(kDefaultStartStop)), hotkeyPause(QString::fromLatin1(kDefaultPause)) {
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(500);
    connect(&saveTimer_, &QTimer::timeout, this, &AppSettings::save);
    connect(this, &AppSettings::changed, this, [this] {
        if (!loading_) saveTimer_.start();
    });
}

AppSettings::~AppSettings() {
    if (saveTimer_.isActive()) save();
}

QString AppSettings::filePath() {
    return QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA")) + QStringLiteral("/ELVideo/settings.json");
}

QString AppSettings::defaultOutputDir() {
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/ELVideo");
}

QString AppSettings::effectiveOutputDir() const { return outputDir.isEmpty() ? defaultOutputDir() : outputDir; }

void AppSettings::load() {
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) return;  // first run: defaults

    const auto j = nlohmann::json::parse(file.readAll().toStdString(), nullptr, false);
    if (!j.is_object()) {
        spdlog::warn("settings file is not valid JSON; using defaults");
        return;
    }

    loading_ = true;
    outputDir = str(j, "outputDir", outputDir);
    container = str(j, "container", container);
    if (container != QLatin1String("mp4")) container = QStringLiteral("fmp4");
    fps = std::clamp(value(j, "fps", fps), 1, 240);
    bitrateKbps = std::clamp(value(j, "bitrateKbps", bitrateKbps), 500, 200'000);
    preferHardware = value(j, "preferHardware", preferHardware);
    captureCursor = value(j, "captureCursor", captureCursor);
    captureSystemAudio = value(j, "captureSystemAudio", captureSystemAudio);
    systemAudioDevice = str(j, "systemAudioDevice", systemAudioDevice);
    systemAudioVolume = std::clamp(value(j, "systemAudioVolume", systemAudioVolume), 0, 200);
    captureMicrophone = value(j, "captureMicrophone", captureMicrophone);
    microphoneDevice = str(j, "microphoneDevice", microphoneDevice);
    microphoneVolume = std::clamp(value(j, "microphoneVolume", microphoneVolume), 0, 200);
    audioBitrateKbps = std::clamp(value(j, "audioBitrateKbps", audioBitrateKbps), 96, 192);
    hotkeyStartStop = str(j, "hotkeyStartStop", hotkeyStartStop);
    hotkeyPause = str(j, "hotkeyPause", hotkeyPause);
    hideWindowWhileRecording = value(j, "hideWindowWhileRecording", hideWindowWhileRecording);
    showFloatingBar = value(j, "showFloatingBar", showFloatingBar);
    closeToTray = value(j, "closeToTray", closeToTray);
    lastSource = str(j, "lastSource", lastSource);
    emit changed();
    loading_ = false;
}

void AppSettings::save() {
    saveTimer_.stop();
    const nlohmann::json j = {
        {"outputDir", outputDir.toStdString()},
        {"container", container.toStdString()},
        {"fps", fps},
        {"bitrateKbps", bitrateKbps},
        {"preferHardware", preferHardware},
        {"captureCursor", captureCursor},
        {"captureSystemAudio", captureSystemAudio},
        {"systemAudioDevice", systemAudioDevice.toStdString()},
        {"systemAudioVolume", systemAudioVolume},
        {"captureMicrophone", captureMicrophone},
        {"microphoneDevice", microphoneDevice.toStdString()},
        {"microphoneVolume", microphoneVolume},
        {"audioBitrateKbps", audioBitrateKbps},
        {"hotkeyStartStop", hotkeyStartStop.toStdString()},
        {"hotkeyPause", hotkeyPause.toStdString()},
        {"hideWindowWhileRecording", hideWindowWhileRecording},
        {"showFloatingBar", showFloatingBar},
        {"closeToTray", closeToTray},
        {"lastSource", lastSource.toStdString()},
    };

    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile file(filePath());  // write-then-rename, so a crash never leaves half a file
    if (!file.open(QIODevice::WriteOnly)) {
        spdlog::error("cannot write settings: {}", file.errorString().toStdString());
        return;
    }
    file.write(QByteArray::fromStdString(j.dump(2)));
    if (!file.commit()) spdlog::error("cannot write settings: {}", file.errorString().toStdString());
}

void AppSettings::resetHotkeys() {
    hotkeyStartStop = QString::fromLatin1(kDefaultStartStop);
    hotkeyPause = QString::fromLatin1(kDefaultPause);
    emit changed();
}
