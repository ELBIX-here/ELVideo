#include "WatchProgress.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>

namespace {

// Near the end counts as watched: the last 5 %, or for a lecture-length video
// (10 minutes and up) the last half minute, where the closing remarks are.
bool nearEnd(double position, double duration) {
    if (duration <= 0) return false;
    return position >= duration * 0.95 || (duration >= 600 && duration - position <= 30);
}

}  // namespace

WatchProgress::WatchProgress(QObject* parent) : QObject(parent) {
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(1500);
    connect(&saveTimer_, &QTimer::timeout, this, &WatchProgress::save);

    load();
    // The file is replaced on every save, so watch its folder rather than the file.
    // Folder notifications do not always arrive (packaged or virtualized AppData),
    // so the file's time is also checked every couple of seconds; it is one stat.
    const QString dir = QFileInfo(filePath()).absolutePath();
    QDir().mkpath(dir);
    watcher_.addPath(dir);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &WatchProgress::reloadIfChanged);
    pollTimer_.setInterval(2000);
    connect(&pollTimer_, &QTimer::timeout, this, &WatchProgress::reloadIfChanged);
    pollTimer_.start();
}

void WatchProgress::reloadIfChanged() {
    if (saving_) return;
    const QFileInfo info(filePath());
    if (!info.exists() || info.lastModified() == loadedAt_) return;
    load();
    ++revision_;
    emit changed();
}

WatchProgress::~WatchProgress() { flush(); }

QString WatchProgress::filePath() {
    return QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + QStringLiteral("/ELVideo/progress.json");
}

// Windows paths compare without case, and with either separator.
QString WatchProgress::key(const QString& path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

void WatchProgress::load() {
    entries_.clear();
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    loadedAt_ = QFileInfo(file).lastModified();
    const auto j = nlohmann::json::parse(file.readAll().toStdString(), nullptr, false);
    if (!j.is_object() || !j.contains("videos") || !j["videos"].is_array()) return;
    for (const auto& v : j["videos"]) {
        if (!v.is_object() || !v.contains("path") || !v["path"].is_string()) continue;
        Entry e;
        e.path = QString::fromStdString(v["path"].get<std::string>());
        e.position = v.value("position", 0.0);
        e.duration = v.value("duration", -1.0);
        e.watched = v.value("watched", false);
        e.updated = v.value("updated", static_cast<qint64>(0));
        entries_.insert(key(e.path), e);
    }
    // Our own changes not yet written win over what the file says.
    for (auto it = pending_.cbegin(); it != pending_.cend(); ++it) entries_.insert(it.key(), it.value());
}

void WatchProgress::save() {
    saveTimer_.stop();
    if (pending_.isEmpty()) return;
    saving_ = true;
    load();  // the other process may have written since; keep its entries
    pending_.clear();

    nlohmann::json videos = nlohmann::json::array();
    for (const auto& e : std::as_const(entries_)) {
        videos.push_back({{"path", e.path.toStdString()},
                          {"position", e.position},
                          {"duration", e.duration},
                          {"watched", e.watched},
                          {"updated", e.updated}});
    }
    QSaveFile file(filePath());  // write-then-rename, so a crash never leaves half a file
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QByteArray::fromStdString(nlohmann::json{{"videos", videos}}.dump(1)));
        if (!file.commit()) spdlog::warn("cannot write watch progress: {}", file.errorString().toStdString());
    } else {
        spdlog::warn("cannot write watch progress: {}", file.errorString().toStdString());
    }
    loadedAt_ = QFileInfo(filePath()).lastModified();
    saving_ = false;
}

void WatchProgress::flush() {
    if (!pending_.isEmpty()) save();
}

void WatchProgress::touch(const QString& path, const std::function<void(Entry&)>& change) {
    if (path.isEmpty()) return;
    const QString k = key(path);
    Entry e = entries_.value(k);
    e.path = path;
    change(e);
    entries_.insert(k, e);
    pending_.insert(k, e);
    saveTimer_.start();
    ++revision_;
    emit changed();
}

QVariantMap WatchProgress::get(const QString& path) const {
    const auto it = entries_.constFind(key(path));
    if (it == entries_.cend()) return {};
    return {{QStringLiteral("position"), it->position},
            {QStringLiteral("duration"), it->duration},
            {QStringLiteral("watched"), it->watched},
            {QStringLiteral("updated"), it->updated}};
}

void WatchProgress::update(const QString& path, double position, double duration) {
    if (position < 0) return;
    touch(path, [&](Entry& e) {
        e.position = position;
        if (duration > 0) e.duration = duration;
        if (nearEnd(position, e.duration)) e.watched = true;
        e.updated = QDateTime::currentMSecsSinceEpoch();
    });
}

void WatchProgress::setWatched(const QString& path, bool watched) {
    touch(path, [&](Entry& e) {
        e.watched = watched;
        if (!watched) e.position = 0;
        e.updated = QDateTime::currentMSecsSinceEpoch();
    });
}

void WatchProgress::setDuration(const QString& path, double duration) {
    if (duration <= 0) return;
    const auto it = entries_.constFind(key(path));
    if (it != entries_.cend() && qFuzzyCompare(it->duration, duration)) return;
    touch(path, [&](Entry& e) { e.duration = duration; });
}

QVariantList WatchProgress::inProgress(int limit) const {
    std::vector<const Entry*> list;
    for (const auto& e : entries_)
        if (!e.watched && e.position >= 5 && QFileInfo::exists(e.path)) list.push_back(&e);
    std::sort(list.begin(), list.end(), [](const Entry* a, const Entry* b) { return a->updated > b->updated; });
    QVariantList out;
    for (const Entry* e : list) {
        if (out.size() >= limit) break;
        const QFileInfo info(e->path);
        out.push_back(QVariantMap{{QStringLiteral("path"), e->path},
                                  {QStringLiteral("name"), info.completeBaseName()},
                                  {QStringLiteral("folder"), info.dir().dirName()},
                                  {QStringLiteral("position"), e->position},
                                  {QStringLiteral("duration"), e->duration},
                                  {QStringLiteral("updated"), e->updated}});
    }
    return out;
}
