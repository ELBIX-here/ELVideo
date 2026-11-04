#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>

#include <optional>
#include <vector>

class QTcpServer;

// Google Drive, read only (drive.readonly), for lecture folders kept in Drive.
//
// Sign-in is the OAuth flow for installed apps: the browser opens Google's page,
// the answer comes back to a one-off server on 127.0.0.1 (with PKCE), and the
// refresh token is kept encrypted for this Windows user (DPAPI) in
// %LOCALAPPDATA%\ELVideo\google.dat, so the player process can sign in as well.
// The OAuth client is the PC client of the Cloud project ELVideo shares with
// ELViewer (google_oauth.json at build time).
//
// Library entries: a Drive folder is "drive:<folderId>", a Drive video
// "drive://<fileId>" (the same ids the Android app uses).
namespace gdrive {

inline const QString kFolderPrefix = QStringLiteral("drive:");
inline const QString kVideoPrefix = QStringLiteral("drive://");
inline const QString kRoot = QStringLiteral("root");            // My Drive
inline const QString kSharedWithMe = QStringLiteral("shared");  // the "공유 문서함" list, not a folder

inline bool isVideo(const QString& path) { return path.startsWith(kVideoPrefix); }
inline bool isFolder(const QString& path) { return path.startsWith(kFolderPrefix) && !isVideo(path); }
inline bool isDrive(const QString& path) { return path.startsWith(kFolderPrefix); }
inline QString folderId(const QString& path) { return path.mid(kFolderPrefix.size()); }
inline QString videoId(const QString& path) { return path.mid(kVideoPrefix.size()); }
inline QString folderPath(const QString& id) { return kFolderPrefix + id; }
inline QString videoPath(const QString& id) { return kVideoPrefix + id; }
QString mediaUrl(const QString& fileId);
// drive://<id> for a Drive media URL, anything else unchanged (the player's paths).
QString keyOf(const QString& pathOrUrl);

bool configured();  // this build has an OAuth client
bool signedIn();
QString accountEmail();

struct Item {
    QString id;
    QString name;
    QString mime;
    qint64 size = 0;
    double durationSec = -1;  // Drive's own reading of a video
    bool isFolder() const;
};

// Blocking calls for worker threads (QThreadPool), never the UI thread.
// They throw nothing; on failure they return nothing and set `error`.
QString accessToken(QString* error = nullptr, bool refresh = false);
std::optional<std::vector<Item>> children(const QString& folderId, QString* error = nullptr);
std::vector<Item> sharedDrives();
QByteArray thumbnail(const QString& fileId, int size);

// Names learned from listings, for breadcrumbs: id -> { name, parent id }.
QString knownName(const QString& id);
QString knownParent(const QString& id);

}  // namespace gdrive

// The QML side: signing in and out, and the folder browser used to add a
// Drive folder to the library. A context property "drive" in both processes.
class GoogleDrive : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString email READ email NOTIFY connectedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)          // signing in
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)    // the last sign-in or browse problem
    // Folder browser: [{ id, name, kind: "drive" | "shared" | "folder" }] of browseId.
    Q_PROPERTY(QString browseId READ browseId NOTIFY browseChanged)
    Q_PROPERTY(QVariantList browseItems READ browseItems NOTIFY browseChanged)
    Q_PROPERTY(int browseVideoCount READ browseVideoCount NOTIFY browseChanged)  // -1 unknown
    Q_PROPERTY(bool browseLoading READ browseLoading NOTIFY browseChanged)

public:
    explicit GoogleDrive(QObject* parent = nullptr);
    ~GoogleDrive() override;

    bool configured() const { return gdrive::configured(); }
    bool connected() const { return connected_; }
    QString email() const { return email_; }
    bool busy() const { return busy_; }
    QString error() const { return error_; }
    QString browseId() const { return browseId_; }
    QVariantList browseItems() const { return browseItems_; }
    int browseVideoCount() const { return browseVideoCount_; }
    bool browseLoading() const { return browseLoading_; }

    // Opens Google's sign-in page in the browser and waits for the answer.
    Q_INVOKABLE void signIn();
    Q_INVOKABLE void signOut();
    // Lists the folders of `id` ("" for the top: My Drive, shared with me, shared drives).
    Q_INVOKABLE void browse(const QString& id);
    // Breadcrumb from a library folder down to a folder under it: [{ name, path }].
    Q_INVOKABLE QVariantList trail(const QString& rootPath, const QString& path) const;
    Q_INVOKABLE bool isDrive(const QString& path) const { return gdrive::isDrive(path); }
    Q_INVOKABLE QString folderPath(const QString& id) const { return gdrive::folderPath(id); }

    // Player: writes the folder's videos as a playlist (titles and media URLs)
    // and answers with playbackReady(playlistFile, startIndex, authHeader, folderName).
    Q_INVOKABLE void preparePlayback(const QString& folderId, const QString& videoPath, const QString& folderName);
    // A fresh "Authorization: Bearer …" for the player, as headerReady().
    Q_INVOKABLE void refreshHeader();

signals:
    void connectedChanged();
    void busyChanged();
    void errorChanged();
    void browseChanged();
    void signedIn();
    void playbackReady(const QString& playlistFile, int startIndex, const QString& authHeader, const QString& folderName);
    void playbackFailed(const QString& message);
    void headerReady(const QString& authHeader);

private:
    void setBusy(bool busy);
    void setError(const QString& error);
    void finishSignIn(const QString& code, const QString& redirectUri, const QString& verifier);
    void stopServer();

    bool connected_ = false;
    QString email_;
    bool busy_ = false;
    QString error_;
    QPointer<QTcpServer> server_;
    QString state_;
    QString browseId_;
    QVariantList browseItems_;
    int browseVideoCount_ = -1;
    bool browseLoading_ = false;
    int browseGeneration_ = 0;
};
