#pragma once

#include <QQuickFramebufferObject>
#include <QtQml/qqmlregistration.h>
#include <QString>
#include <QVariant>

#include <memory>

struct mpv_handle;
struct mpv_event_property;

// libmpv in Qt Quick: video through mpv's OpenGL render API into a framebuffer
// object (so the process must run Qt Quick on OpenGL), and mpv's properties as
// QML properties. Every mpv call happens on the GUI thread except rendering.
class MpvItem : public QQuickFramebufferObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool idle READ idle NOTIFY idleChanged)  // no file loaded
    Q_PROPERTY(QString path READ path NOTIFY fileChanged)
    Q_PROPERTY(QString title READ title NOTIFY fileChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)  // seconds
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool eof READ eof NOTIFY eofChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)  // 0..150
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(double loopA READ loopA NOTIFY loopChanged)  // -1 when unset
    Q_PROPERTY(double loopB READ loopB NOTIFY loopChanged)
    Q_PROPERTY(double subDelay READ subDelay NOTIFY subDelayChanged)
    Q_PROPERTY(double audioDelay READ audioDelay NOTIFY audioDelayChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)      // mpv track-list
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)
    Q_PROPERTY(int playlistPos READ playlistPos NOTIFY playlistChanged)
    Q_PROPERTY(int playlistCount READ playlistCount NOTIFY playlistChanged)
    Q_PROPERTY(QVariantList playlist READ playlist NOTIFY playlistChanged)  // mpv playlist: filename, current
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoSizeChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoSizeChanged)
    Q_PROPERTY(QString hwdec READ hwdec NOTIFY hwdecChanged)  // hwdec-current

public:
    explicit MpvItem(QQuickItem* parent = nullptr);
    ~MpvItem() override;

    Renderer* createRenderer() const override;

    // Opens a file or URL; the folder's other videos become the playlist.
    Q_INVOKABLE void open(const QString& pathOrUrl);
    // What the watch progress calls a playlist entry: drive://<id> for a Drive
    // stream, the path itself otherwise. `path` is already in this form.
    Q_INVOKABLE QString keyOf(const QString& filename) const;
    Q_INVOKABLE void command(const QVariantList& args);
    Q_INVOKABLE void setMpvProperty(const QString& name, const QVariant& value);
    Q_INVOKABLE QVariant getMpvProperty(const QString& name) const;

    Q_INVOKABLE void seek(double seconds, bool relative = false);  // exact
    Q_INVOKABLE void togglePause() { setPaused(!paused_); }
    Q_INVOKABLE void frameStep(bool backward = false);
    Q_INVOKABLE void cycleAbLoop();  // A, then B, then off
    Q_INVOKABLE void clearAbLoop();
    Q_INVOKABLE void addSubDelay(double seconds);
    Q_INVOKABLE void addAudioDelay(double seconds);
    Q_INVOKABLE void selectTrack(const QString& type, int id);  // id 0: off ("sub"/"audio"/"video")
    Q_INVOKABLE void addSubtitle(const QString& path);
    Q_INVOKABLE void playlistStep(int delta);
    Q_INVOKABLE void playlistPlay(int index);
    // Saves the current video frame; returns the file path (empty on failure).
    Q_INVOKABLE QString screenshot(const QString& directory);
    // Remembers the position for the next time this file is opened.
    Q_INVOKABLE void saveResumePosition();

    bool idle() const { return idle_; }
    QString path() const { return path_; }
    QString title() const { return title_; }
    double position() const { return position_; }
    double duration() const { return duration_; }
    bool paused() const { return paused_; }
    void setPaused(bool paused);
    bool eof() const { return eof_; }
    double volume() const { return volume_; }
    void setVolume(double volume);
    bool muted() const { return muted_; }
    void setMuted(bool muted);
    double speed() const { return speed_; }
    void setSpeed(double speed);
    double loopA() const { return loopA_; }
    double loopB() const { return loopB_; }
    double subDelay() const { return subDelay_; }
    double audioDelay() const { return audioDelay_; }
    QVariantList tracks() const { return tracks_; }
    QVariantList chapters() const { return chapters_; }
    int playlistPos() const { return playlistPos_; }
    int playlistCount() const { return playlistCount_; }
    QVariantList playlist() const { return playlist_; }
    int videoWidth() const { return videoWidth_; }
    int videoHeight() const { return videoHeight_; }
    QString hwdec() const { return hwdec_; }

    std::shared_ptr<mpv_handle> handle() const { return mpv_; }
    // Called (queued) by the renderer once mpv's render context exists.
    void onRendererReady();

signals:
    void idleChanged();
    void fileChanged();
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void eofChanged();
    void volumeChanged();
    void mutedChanged();
    void speedChanged();
    void loopChanged();
    void subDelayChanged();
    void audioDelayChanged();
    void tracksChanged();
    void chaptersChanged();
    void playlistChanged();
    void videoSizeChanged();
    void hwdecChanged();
    void fileLoaded();
    void endOfFile();
    void playedToEnd(const QString& path);  // a file ran to its end (also mid-playlist)
    void error(const QString& message);

private:
    void processEvents();
    void onProperty(const mpv_event_property& property);

    std::shared_ptr<mpv_handle> mpv_;  // shared with the renderer, which may outlive the item
    // mpv only creates its video output when a render context exists; a file loaded
    // before that plays audio only. So loads wait for the renderer.
    bool rendererReady_ = false;
    QString pendingOpen_;
    bool idle_ = true;
    QString path_;
    QString title_;
    double position_ = 0;
    double duration_ = 0;
    bool paused_ = false;
    bool eof_ = false;
    double volume_ = 100;
    bool muted_ = false;
    double speed_ = 1;
    double loopA_ = -1;
    double loopB_ = -1;
    double subDelay_ = 0;
    double audioDelay_ = 0;
    QVariantList tracks_;
    QVariantList chapters_;
    int playlistPos_ = -1;
    int playlistCount_ = 0;
    QVariantList playlist_;
    int videoWidth_ = 0;
    int videoHeight_ = 0;
    QString hwdec_;
};
