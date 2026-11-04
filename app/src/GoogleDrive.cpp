#include "GoogleDrive.h"

#include "FolderListing.h"
#include "elv_google_oauth.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThreadPool>
#include <QTimer>
#include <QUrlQuery>

#include <spdlog/spdlog.h>

#include <windows.h>
#include <dpapi.h>

#include <algorithm>
#include <chrono>

namespace gdrive {
namespace {

const QString kApi = QStringLiteral("https://www.googleapis.com/drive/v3");
const QString kTokenUrl = QStringLiteral("https://oauth2.googleapis.com/token");
const QString kScope = QStringLiteral("https://www.googleapis.com/auth/drive.readonly");
const QString kFolderMime = QStringLiteral("application/vnd.google-apps.folder");
const QString kShortcutMime = QStringLiteral("application/vnd.google-apps.shortcut");

QString storePath() {
    return QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + QStringLiteral("/ELVideo/google.dat");
}

// --- the refresh token, encrypted for this Windows user ----------------------------

QByteArray protect(const QByteArray& in) {
    DATA_BLOB plain{static_cast<DWORD>(in.size()), reinterpret_cast<BYTE*>(const_cast<char*>(in.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&plain, L"ELVideo Google Drive", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    QByteArray result(reinterpret_cast<const char*>(out.pbData), static_cast<qsizetype>(out.cbData));
    LocalFree(out.pbData);
    return result;
}

QByteArray unprotect(const QByteArray& in) {
    DATA_BLOB sealed{static_cast<DWORD>(in.size()), reinterpret_cast<BYTE*>(const_cast<char*>(in.data()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&sealed, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    QByteArray result(reinterpret_cast<const char*>(out.pbData), static_cast<qsizetype>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

struct Stored {
    QString refreshToken;
    QString email;
};

std::optional<Stored> readStore() {
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const QJsonObject j = QJsonDocument::fromJson(unprotect(f.readAll())).object();
    const QString refresh = j.value(QStringLiteral("refresh_token")).toString();
    if (refresh.isEmpty()) return std::nullopt;
    return Stored{refresh, j.value(QStringLiteral("email")).toString()};
}

bool writeStore(const Stored& s) {
    QDir().mkpath(QFileInfo(storePath()).absolutePath());
    QSaveFile f(storePath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray plain =
        QJsonDocument(QJsonObject{{QStringLiteral("refresh_token"), s.refreshToken}, {QStringLiteral("email"), s.email}})
            .toJson(QJsonDocument::Compact);
    f.write(protect(plain));
    return f.commit();
}

// The access token in use by this process.
QMutex tokenMutex;
QString cachedToken;
std::chrono::steady_clock::time_point cachedUntil;

// What listings taught us, for breadcrumbs.
QMutex namesMutex;
QHash<QString, QPair<QString, QString>> names;  // id -> { name, parent }

// --- blocking HTTP on a worker thread -------------------------------------------------

struct Reply {
    int status = 0;
    QByteArray body;
    QString error;
};

Reply send(QNetworkRequest request, const QByteArray* postBody = nullptr) {
    QNetworkAccessManager nam;
    QEventLoop loop;
    request.setTransferTimeout(30000);
    QNetworkReply* reply = postBody ? nam.post(request, *postBody) : nam.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    Reply r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    r.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError && r.status == 0) r.error = reply->errorString();
    reply->deleteLater();
    return r;
}

Reply tokenRequest(const QUrlQuery& form) {
    QNetworkRequest req{QUrl(kTokenUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();
    return send(req, &body);
}

// GET with the access token; a rejected token is refreshed once.
std::optional<QJsonObject> getJson(const QString& url, QString* error) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        const QString token = accessToken(error, attempt > 0);
        if (token.isEmpty()) return std::nullopt;
        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        const Reply r = send(req);
        if (r.status == 401 && attempt == 0) continue;
        if (r.status < 200 || r.status >= 300) {
            if (error) *error = r.error.isEmpty() ? QStringLiteral("Drive %1").arg(r.status) : r.error;
            spdlog::warn("drive: {} -> {} {}", url.left(120).toStdString(), r.status, r.body.left(200).toStdString());
            return std::nullopt;
        }
        return QJsonDocument::fromJson(r.body).object();
    }
    return std::nullopt;
}

QString enc(const QString& s) { return QString::fromUtf8(QUrl::toPercentEncoding(s)); }

QString base64Url(const QByteArray& bytes) {
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString randomString(int bytes) {
    QByteArray b(bytes, Qt::Uninitialized);
    for (char& c : b) c = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return base64Url(b);
}

void naturalSort(std::vector<Item>& items) {
    QCollator collator{QLocale(QLocale::Korean)};
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(items.begin(), items.end(), [&](const Item& a, const Item& b) { return collator.compare(a.name, b.name) < 0; });
}

bool isVideoItem(const Item& i) {
    return !i.isFolder() && (i.mime.startsWith(QLatin1String("video/")) || FolderListing::isVideo(i.name));
}

}  // namespace

bool Item::isFolder() const { return mime == kFolderMime; }

QString mediaUrl(const QString& fileId) { return kApi + QStringLiteral("/files/") + fileId + QStringLiteral("?alt=media&supportsAllDrives=true"); }

QString keyOf(const QString& pathOrUrl) {
    static const QRegularExpression re(QStringLiteral("^https://www\\.googleapis\\.com/drive/v3/files/([^?/]+)\\?alt=media"));
    const auto m = re.match(pathOrUrl);
    return m.hasMatch() ? videoPath(m.captured(1)) : pathOrUrl;
}

bool configured() { return sizeof(ELV_GOOGLE_CLIENT_ID) > 1 && sizeof(ELV_GOOGLE_CLIENT_SECRET) > 1; }

bool signedIn() { return readStore().has_value(); }

QString accountEmail() {
    const auto s = readStore();
    return s ? s->email : QString();
}

QString accessToken(QString* error, bool refresh) {
    QMutexLocker lock(&tokenMutex);
    if (!refresh && !cachedToken.isEmpty() && std::chrono::steady_clock::now() < cachedUntil) return cachedToken;
    const auto stored = readStore();
    if (!stored) {
        if (error) *error = QStringLiteral("Google Drive에 로그인해야 합니다");
        return {};
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), QStringLiteral(ELV_GOOGLE_CLIENT_ID));
    form.addQueryItem(QStringLiteral("client_secret"), QStringLiteral(ELV_GOOGLE_CLIENT_SECRET));
    form.addQueryItem(QStringLiteral("refresh_token"), stored->refreshToken);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    const Reply r = tokenRequest(form);
    const QJsonObject j = QJsonDocument::fromJson(r.body).object();
    const QString token = j.value(QStringLiteral("access_token")).toString();
    if (token.isEmpty()) {
        // invalid_grant: access was withdrawn (Google account settings) or the token expired.
        if (error)
            *error = j.value(QStringLiteral("error")).toString() == QLatin1String("invalid_grant")
                         ? QStringLiteral("Google Drive에 다시 로그인해야 합니다")
                         : QStringLiteral("Google에 연결할 수 없습니다 (%1)").arg(r.error.isEmpty() ? QString::number(r.status) : r.error);
        cachedToken.clear();
        return {};
    }
    cachedToken = token;
    // Renew a little before Google's hour runs out.
    cachedUntil = std::chrono::steady_clock::now() + std::chrono::seconds(j.value(QStringLiteral("expires_in")).toInt(3600) - 300);
    return token;
}

std::optional<std::vector<Item>> children(const QString& folderId, QString* error) {
    const QString q = folderId == kSharedWithMe ? QStringLiteral("sharedWithMe and trashed = false")
                                                : QStringLiteral("'%1' in parents and trashed = false").arg(folderId);
    const QString fields = QStringLiteral(
        "nextPageToken,files(id,name,mimeType,size,videoMediaMetadata(durationMillis),shortcutDetails(targetId,targetMimeType))");
    std::vector<Item> out;
    QString page;
    do {
        QString url = kApi + QStringLiteral("/files?q=") + enc(q) + QStringLiteral("&fields=") + enc(fields) +
                      QStringLiteral("&pageSize=1000&supportsAllDrives=true&includeItemsFromAllDrives=true");
        if (!page.isEmpty()) url += QStringLiteral("&pageToken=") + enc(page);
        const auto j = getJson(url, error);
        if (!j) return std::nullopt;
        for (const QJsonValue& v : j->value(QStringLiteral("files")).toArray()) {
            const QJsonObject f = v.toObject();
            Item item;
            item.id = f.value(QStringLiteral("id")).toString();
            item.name = f.value(QStringLiteral("name")).toString();
            item.mime = f.value(QStringLiteral("mimeType")).toString();
            if (item.mime == kShortcutMime) {
                const QJsonObject s = f.value(QStringLiteral("shortcutDetails")).toObject();
                item.id = s.value(QStringLiteral("targetId")).toString();
                item.mime = s.value(QStringLiteral("targetMimeType")).toString();
            }
            item.size = f.value(QStringLiteral("size")).toString().toLongLong();
            const QString ms = f.value(QStringLiteral("videoMediaMetadata")).toObject().value(QStringLiteral("durationMillis")).toString();
            if (!ms.isEmpty()) item.durationSec = ms.toDouble() / 1000.0;
            if (!item.id.isEmpty()) out.push_back(std::move(item));
        }
        page = j->value(QStringLiteral("nextPageToken")).toString();
    } while (!page.isEmpty());
    naturalSort(out);
    {
        QMutexLocker lock(&namesMutex);
        for (const Item& i : out)
            if (i.isFolder()) names.insert(i.id, {i.name, folderId});
    }
    return out;
}

std::vector<Item> sharedDrives() {
    std::vector<Item> out;
    const auto j = getJson(kApi + QStringLiteral("/drives?pageSize=100&fields=") + enc(QStringLiteral("drives(id,name)")), nullptr);
    if (!j) return out;
    for (const QJsonValue& v : j->value(QStringLiteral("drives")).toArray()) {
        Item i;
        i.id = v.toObject().value(QStringLiteral("id")).toString();
        i.name = v.toObject().value(QStringLiteral("name")).toString();
        i.mime = kFolderMime;
        out.push_back(i);
    }
    QMutexLocker lock(&namesMutex);
    for (const Item& i : out) names.insert(i.id, {i.name, QString()});
    return out;
}

QByteArray thumbnail(const QString& fileId, int size) {
    const auto j = getJson(kApi + QStringLiteral("/files/") + fileId + QStringLiteral("?supportsAllDrives=true&fields=thumbnailLink"), nullptr);
    if (!j) return {};
    QString link = j->value(QStringLiteral("thumbnailLink")).toString();
    if (link.isEmpty()) return {};
    // Links end in a size ("=s220"); ask for one sharp enough for the card.
    static const QRegularExpression sizeSuffix(QStringLiteral("=s\\d+$"));
    link.replace(sizeSuffix, QStringLiteral("=s%1").arg(size));
    const QString token = accessToken();
    if (token.isEmpty()) return {};
    QNetworkRequest req{QUrl(link)};
    req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    const Reply r = send(req);
    return r.status >= 200 && r.status < 300 ? r.body : QByteArray();
}

QString knownName(const QString& id) {
    QMutexLocker lock(&namesMutex);
    return names.value(id).first;
}

QString knownParent(const QString& id) {
    QMutexLocker lock(&namesMutex);
    return names.value(id).second;
}

}  // namespace gdrive

// --- QML side ----------------------------------------------------------------------------

GoogleDrive::GoogleDrive(QObject* parent) : QObject(parent) {
    if (const auto s = gdrive::signedIn()) {
        connected_ = true;
        email_ = gdrive::accountEmail();
    }
}

GoogleDrive::~GoogleDrive() { stopServer(); }

void GoogleDrive::setBusy(bool busy) {
    if (busy == busy_) return;
    busy_ = busy;
    emit busyChanged();
}

void GoogleDrive::setError(const QString& error) {
    if (error == error_) return;
    error_ = error;
    emit errorChanged();
}

void GoogleDrive::stopServer() {
    if (server_) {
        server_->close();
        server_->deleteLater();
    }
    server_ = nullptr;
}

void GoogleDrive::signIn() {
    setError({});
    if (!gdrive::configured()) {
        setError(tr("이 빌드에는 Google 로그인 설정이 없습니다"));
        return;
    }
    stopServer();
    server_ = new QTcpServer(this);
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        setError(tr("로그인을 시작할 수 없습니다: %1").arg(server_->errorString()));
        stopServer();
        return;
    }
    const QString redirect = QStringLiteral("http://127.0.0.1:%1").arg(server_->serverPort());
    const QString verifier = gdrive::randomString(48);
    const QString challenge = gdrive::base64Url(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256));
    state_ = gdrive::randomString(18);

    QUrl url(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"), QStringLiteral(ELV_GOOGLE_CLIENT_ID));
    q.addQueryItem(QStringLiteral("redirect_uri"), redirect);
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("scope"), gdrive::kScope);
    q.addQueryItem(QStringLiteral("code_challenge"), challenge);
    q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    q.addQueryItem(QStringLiteral("state"), state_);
    q.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
    q.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));  // always hands out a refresh token
    url.setQuery(q);

    // The browser comes back to http://127.0.0.1:<port>/?code=…&state=…
    connect(server_, &QTcpServer::newConnection, this, [this, redirect, verifier] {
        while (server_ && server_->hasPendingConnections()) {
            QTcpSocket* socket = server_->nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, redirect, verifier] {
                const QByteArray head = socket->peek(8192);
                if (!head.contains("\r\n\r\n")) return;  // wait for the whole request line and headers
                socket->readAll();
                const QList<QByteArray> line = head.left(head.indexOf("\r\n")).split(' ');
                const QUrl target(QString::fromLatin1(line.value(1)));
                const QUrlQuery query(target);
                const bool isAnswer = target.path() == QLatin1String("/") &&
                                      (query.hasQueryItem(QStringLiteral("code")) || query.hasQueryItem(QStringLiteral("error")));
                const bool ok = isAnswer && query.queryItemValue(QStringLiteral("state")) == state_ &&
                                query.hasQueryItem(QStringLiteral("code"));
                const QString message = ok ? tr("ELVideo에 Google Drive가 연결되었습니다.") : tr("Google Drive를 연결하지 못했습니다.");
                const QByteArray page =
                    QStringLiteral(
                        "<!doctype html><meta charset=utf-8><title>ELVideo</title>"
                        "<body style=\"font-family:'Pretendard','Segoe UI',sans-serif;background:#f5f5f7;color:#1d1d1f;"
                        "display:flex;align-items:center;justify-content:center;height:100vh;margin:0\">"
                        "<div style=\"text-align:center\"><p style=\"font-size:21px;font-weight:600;margin:0 0 8px\">%1</p>"
                        "<p style=\"color:#6e6e73;margin:0\">%2</p></div>")
                        .arg(message, tr("이 창을 닫고 ELVideo로 돌아가세요."))
                        .toUtf8();
                socket->write(QByteArray(isAnswer ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n") +
                              "Content-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
                              QByteArray::number(isAnswer ? page.size() : 0) + "\r\n\r\n" + (isAnswer ? page : QByteArray()));
                socket->disconnectFromHost();
                if (!isAnswer) return;  // the browser asking for /favicon.ico
                stopServer();
                if (ok) {
                    finishSignIn(query.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded), redirect, verifier);
                } else {
                    setBusy(false);
                    setError(tr("로그인을 취소했습니다"));
                }
            });
        }
    });
    // Nobody answered in five minutes: give up quietly.
    QTimer::singleShot(std::chrono::minutes(5), this, [this, server = QPointer<QTcpServer>(server_)] {
        if (!server || server != server_) return;
        stopServer();
        setBusy(false);
        setError(tr("시간이 지나 로그인을 멈췄습니다"));
    });
    setBusy(true);
    if (!QDesktopServices::openUrl(url)) {
        stopServer();
        setBusy(false);
        setError(tr("브라우저를 열 수 없습니다"));
    }
}

void GoogleDrive::finishSignIn(const QString& code, const QString& redirectUri, const QString& verifier) {
    QThreadPool::globalInstance()->start([this, guard = QPointer<GoogleDrive>(this), code, redirectUri, verifier] {
        QUrlQuery form;
        form.addQueryItem(QStringLiteral("code"), code);
        form.addQueryItem(QStringLiteral("client_id"), QStringLiteral(ELV_GOOGLE_CLIENT_ID));
        form.addQueryItem(QStringLiteral("client_secret"), QStringLiteral(ELV_GOOGLE_CLIENT_SECRET));
        form.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
        form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
        form.addQueryItem(QStringLiteral("code_verifier"), verifier);
        const gdrive::Reply r = gdrive::tokenRequest(form);
        const QJsonObject j = QJsonDocument::fromJson(r.body).object();
        const QString refresh = j.value(QStringLiteral("refresh_token")).toString();
        const QString access = j.value(QStringLiteral("access_token")).toString();
        QString email;
        QString error;
        if (refresh.isEmpty() || access.isEmpty()) {
            error = tr("Google이 로그인을 받아 주지 않았습니다 (%1)").arg(j.value(QStringLiteral("error")).toString());
        } else {
            // Who signed in, for the settings page.
            QNetworkRequest req{QUrl(gdrive::kApi + QStringLiteral("/about?fields=user(emailAddress)"))};
            req.setRawHeader("Authorization", "Bearer " + access.toUtf8());
            const gdrive::Reply about = gdrive::send(req);
            email = QJsonDocument::fromJson(about.body).object().value(QStringLiteral("user")).toObject()
                        .value(QStringLiteral("emailAddress")).toString();
            if (!gdrive::writeStore({refresh, email})) error = tr("로그인 정보를 저장할 수 없습니다");
            QMutexLocker lock(&gdrive::tokenMutex);
            gdrive::cachedToken = access;
            gdrive::cachedUntil = std::chrono::steady_clock::now() + std::chrono::minutes(50);
        }
        QMetaObject::invokeMethod(qApp, [guard, email, error] {
            if (!guard) return;
            guard->setBusy(false);
            if (!error.isEmpty()) {
                guard->setError(error);
                return;
            }
            spdlog::info("drive: signed in");
            guard->connected_ = true;
            guard->email_ = email;
            emit guard->connectedChanged();
            emit guard->signedIn();
        }, Qt::QueuedConnection);
    });
}

void GoogleDrive::signOut() {
    // Ask Google to forget the grant too, so a later sign-in asks again.
    if (const auto s = gdrive::readStore()) {
        QThreadPool::globalInstance()->start([refresh = s->refreshToken] {
            QNetworkRequest req{QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke?token=") + gdrive::enc(refresh))};
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
            const QByteArray none;
            gdrive::send(req, &none);
        });
    }
    QFile::remove(gdrive::storePath());
    {
        QMutexLocker lock(&gdrive::tokenMutex);
        gdrive::cachedToken.clear();
    }
    connected_ = false;
    email_.clear();
    setError({});
    emit connectedChanged();
}

void GoogleDrive::browse(const QString& id) {
    const int generation = ++browseGeneration_;
    browseId_ = id;
    browseItems_.clear();
    browseVideoCount_ = -1;
    browseLoading_ = true;
    setError({});
    emit browseChanged();
    QThreadPool::globalInstance()->start([this, guard = QPointer<GoogleDrive>(this), id, generation] {
        QVariantList items;
        int videos = -1;
        QString error;
        auto folder = [](const gdrive::Item& i, const QString& kind) {
            return QVariantMap{{QStringLiteral("id"), i.id}, {QStringLiteral("name"), i.name}, {QStringLiteral("kind"), kind}};
        };
        if (id.isEmpty()) {
            // The top: My Drive, files shared with me, then shared drives.
            if (gdrive::accessToken(&error).isEmpty()) {
                // stays empty; the page shows the error
            } else {
                items.push_back(QVariantMap{{QStringLiteral("id"), gdrive::kRoot}, {QStringLiteral("name"), tr("내 드라이브")},
                                            {QStringLiteral("kind"), QStringLiteral("drive")}});
                items.push_back(QVariantMap{{QStringLiteral("id"), gdrive::kSharedWithMe}, {QStringLiteral("name"), tr("공유 문서함")},
                                            {QStringLiteral("kind"), QStringLiteral("shared")}});
                for (const auto& d : gdrive::sharedDrives()) items.push_back(folder(d, QStringLiteral("drive")));
            }
        } else if (const auto kids = gdrive::children(id, &error)) {
            videos = 0;
            for (const auto& k : *kids) {
                if (k.isFolder()) items.push_back(folder(k, QStringLiteral("folder")));
                else if (gdrive::isVideoItem(k)) ++videos;
            }
            if (id == gdrive::kSharedWithMe) videos = -1;
        }
        QMetaObject::invokeMethod(qApp, [guard, generation, items, videos, error] {
            if (!guard || guard->browseGeneration_ != generation) return;
            guard->browseItems_ = items;
            guard->browseVideoCount_ = videos;
            guard->browseLoading_ = false;
            guard->setError(error);
            emit guard->browseChanged();
        }, Qt::QueuedConnection);
    });
}

QVariantList GoogleDrive::trail(const QString& rootPath, const QString& path) const {
    QVariantList out;
    if (!gdrive::isFolder(path) || path == rootPath) return out;
    const QString rootId = gdrive::folderId(rootPath);
    for (QString id = gdrive::folderId(path); !id.isEmpty() && id != rootId; id = gdrive::knownParent(id)) {
        out.prepend(QVariantMap{{QStringLiteral("name"), gdrive::knownName(id)}, {QStringLiteral("path"), gdrive::folderPath(id)}});
        if (out.size() > 32) break;  // a loop in what we learned; never expected
    }
    return out;
}

void GoogleDrive::preparePlayback(const QString& folderId, const QString& videoPath, const QString& folderName) {
    QThreadPool::globalInstance()->start([this, guard = QPointer<GoogleDrive>(this), folderId, videoPath, folderName] {
        QString error;
        const QString token = gdrive::accessToken(&error);
        const QString fileId = gdrive::videoId(videoPath);
        std::vector<gdrive::Item> videos;
        if (!token.isEmpty() && !folderId.isEmpty()) {
            if (const auto kids = gdrive::children(folderId, &error))
                for (const auto& k : *kids)
                    if (gdrive::isVideoItem(k)) videos.push_back(k);
        }
        if (!token.isEmpty() && std::none_of(videos.begin(), videos.end(), [&](const auto& v) { return v.id == fileId; })) {
            // Opened on its own (or the folder could not be read): just this video.
            videos.clear();
            gdrive::Item one;
            one.id = fileId;
            if (const auto j = gdrive::getJson(gdrive::kApi + QStringLiteral("/files/") + fileId +
                                                   QStringLiteral("?supportsAllDrives=true&fields=name"), &error))
                one.name = j->value(QStringLiteral("name")).toString();
            videos.push_back(one);
        }
        // An m3u keeps the titles mpv shows in the title bar and the playlist.
        QString file;
        int start = 0;
        if (!token.isEmpty()) {
            file = QDir::temp().filePath(QStringLiteral("elvideo-drive-%1.m3u").arg(QCoreApplication::applicationPid()));
            QFile f(file);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                QByteArray m3u = "#EXTM3U\n";
                for (size_t i = 0; i < videos.size(); ++i) {
                    if (videos[i].id == fileId) start = static_cast<int>(i);
                    const QString title = videos[i].name.isEmpty() ? videos[i].id : QFileInfo(videos[i].name).completeBaseName();
                    m3u += "#EXTINF:-1," + title.toUtf8() + "\n" + gdrive::mediaUrl(videos[i].id).toUtf8() + "\n";
                }
                f.write(m3u);
            } else {
                error = tr("재생목록을 만들 수 없습니다");
                file.clear();
            }
        }
        QMetaObject::invokeMethod(qApp, [guard, file, start, token, folderName, error] {
            if (!guard) return;
            if (file.isEmpty()) emit guard->playbackFailed(error.isEmpty() ? tr("Google Drive에 연결할 수 없습니다") : error);
            else emit guard->playbackReady(file, start, QStringLiteral("Authorization: Bearer ") + token, folderName);
        }, Qt::QueuedConnection);
    });
}

void GoogleDrive::refreshHeader() {
    QThreadPool::globalInstance()->start([guard = QPointer<GoogleDrive>(this)] {
        const QString token = gdrive::accessToken();
        if (token.isEmpty()) return;
        QMetaObject::invokeMethod(qApp, [guard, token] {
            if (guard) emit guard->headerReady(QStringLiteral("Authorization: Bearer ") + token);
        }, Qt::QueuedConnection);
    });
}
