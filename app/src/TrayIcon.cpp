#include "TrayIcon.h"

#include "AppSettings.h"
#include "Desktop.h"
#include "RecorderModel.h"

#include <QPainter>
#include <QPixmap>

namespace {

// The app icon with a status badge in the lower-right corner.
QIcon badged(const QIcon& base, const QColor& color, bool pauseBars) {
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48}) {
        QPixmap pixmap = base.pixmap(size, size);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal d = size * 0.56;
        const QRectF badge(size - d, size - d, d, d);
        p.setPen(QPen(QColor(0x16, 0x18, 0x1F), size / 12.0));
        p.setBrush(color);
        p.drawEllipse(badge.adjusted(0.5, 0.5, -0.5, -0.5));
        if (pauseBars) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x16, 0x18, 0x1F));
            const qreal w = d * 0.14, h = d * 0.44;
            const QPointF c = badge.center();
            p.drawRect(QRectF(c.x() - w * 1.6, c.y() - h / 2, w, h));
            p.drawRect(QRectF(c.x() + w * 0.6, c.y() - h / 2, w, h));
        }
        p.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

QString formatElapsed(qint64 ms) {
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg(s / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

TrayIcon::TrayIcon(RecorderModel& recorder, AppSettings& settings, Desktop& desktop, QObject* parent)
    : QObject(parent), recorder_(recorder) {
    idleIcon_ = QIcon(QStringLiteral(":/elvideo.ico"));
    recordingIcon_ = badged(idleIcon_, QColor(0xE5, 0x48, 0x4D), false);
    pausedIcon_ = badged(idleIcon_, QColor(0xF2, 0xA9, 0x3B), true);

    menu_.addAction(tr("ELVideo 열기"), this, &TrayIcon::showRequested);
    menu_.addSeparator();
    toggleAction_ = menu_.addAction(QString(), &recorder_, &RecorderModel::toggle);
    pauseAction_ = menu_.addAction(QString(), &recorder_, &RecorderModel::togglePause);
    replayAction_ = menu_.addAction(QString(), &recorder_, &RecorderModel::replayHotkey);
    menu_.addSeparator();
    menu_.addAction(tr("녹화 폴더 열기"), this,
                    [&settings, &desktop] { desktop.openFolder(settings.effectiveOutputDir()); });
    menu_.addSeparator();
    menu_.addAction(tr("종료"), this, &TrayIcon::quitRequested);

    tray_.setContextMenu(&menu_);
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) emit showRequested();
    });
    connect(&recorder_, &RecorderModel::stateChanged, this, &TrayIcon::update);
    connect(&recorder_, &RecorderModel::statsChanged, this, &TrayIcon::update);
    connect(&recorder_, &RecorderModel::startedChanged, this, &TrayIcon::update);

    update();
    tray_.show();
}

void TrayIcon::notify(const QString& title, const QString& message) {
    tray_.showMessage(title, message, idleIcon_, 4000);
}

void TrayIcon::update() {
    const QString state = recorder_.state();
    const bool active = recorder_.active();

    const bool replay = recorder_.replayMode();
    toggleAction_->setText(replay ? tr("리플레이 버퍼 끄기") : active ? tr("녹화 정지") : tr("녹화 시작"));
    toggleAction_->setEnabled(!recorder_.busy());
    pauseAction_->setText(recorder_.paused() ? tr("다시 시작") : tr("일시정지"));
    pauseAction_->setEnabled(active && !replay);
    replayAction_->setText(replay ? tr("리플레이 저장") : tr("리플레이 버퍼 켜기"));
    replayAction_->setEnabled(replay || (!active && !recorder_.busy()));

    const QIcon& icon = !active ? idleIcon_ : recorder_.paused() ? pausedIcon_ : recordingIcon_;
    if (tray_.icon().cacheKey() != icon.cacheKey()) tray_.setIcon(icon);

    QString tip = QStringLiteral("ELVideo");
    if (replay)
        tip += tr(" — 리플레이 버퍼 %1초").arg(static_cast<int>(recorder_.replayBufferedSeconds()));
    else if (state == QLatin1String("recording"))
        tip += tr(" — 녹화 중 ") + formatElapsed(recorder_.elapsedMs());
    else if (state == QLatin1String("paused"))
        tip += tr(" — 일시정지 ") + formatElapsed(recorder_.elapsedMs());
    tray_.setToolTip(tip);
}
