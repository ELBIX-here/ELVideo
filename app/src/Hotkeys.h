#pragma once

#include <QObject>
#include <QString>

#include <windows.h>

// Global hotkeys through RegisterHotKey on a hidden message-only window. The
// same window receives "show yourself" requests from a second app instance.
class Hotkeys : public QObject {
    Q_OBJECT
    // Human-readable problems with the current hotkeys (empty when all registered).
    Q_PROPERTY(QString startStopError READ startStopError NOTIFY errorsChanged)
    Q_PROPERTY(QString pauseError READ pauseError NOTIFY errorsChanged)
    Q_PROPERTY(QString replayError READ replayError NOTIFY errorsChanged)
    // While true (a hotkey field is being edited) nothing is registered, so the
    // field receives the key combination instead of the running hotkey.
    Q_PROPERTY(bool suspended READ suspended WRITE setSuspended NOTIFY suspendedChanged)

public:
    enum Id { StartStop = 1, Pause = 2, Replay = 3 };

    explicit Hotkeys(QObject* parent = nullptr);
    ~Hotkeys() override;

    void setSequences(const QString& startStop, const QString& pause, const QString& replay);

    QString startStopError() const { return errors_[StartStop]; }
    QString pauseError() const { return errors_[Pause]; }
    QString replayError() const { return errors_[Replay]; }
    bool suspended() const { return suspended_; }
    void setSuspended(bool suspended);

    // QML key event -> portable text ("Ctrl+Shift+F9"); empty for modifier-only
    // presses and keys RegisterHotKey cannot use.
    Q_INVOKABLE QString sequenceFromKey(int key, int modifiers) const;

    // Called by a second instance before it exits: asks the first one to show itself.
    static bool activateRunningInstance();

signals:
    void activated(int id);
    void showRequested();
    void errorsChanged();
    void suspendedChanged();

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void apply();
    QString registerOne(Id id, const QString& sequence);

    HWND hwnd_ = nullptr;
    QString sequences_[4];
    QString errors_[4];
    bool registered_[4]{};
    bool suspended_ = false;
};
