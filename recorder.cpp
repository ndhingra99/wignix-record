/* WigNix: see recorder.h. */
#include "recorder.h"

#include <QApplication>
#include <QScreen>
#include <QDir>
#include <QDateTime>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileInfo>
#include <QDBusInterface>
#include <QDBusConnection>
#include <QVariantMap>

static const int FRAME = 2;            /* frame thickness, drawn outside the capture */
static const int LIMIT_MS = 30 * 60 * 1000;
static const int FPS = 25;

Recorder::Recorder(const QRect &area, QObject *parent)
    : QObject(parent), m_area(area), m_limit(new QTimer(this)), m_tick(new QTimer(this)), m_grace(new QTimer(this))
{
    m_dir = QDir::homePath() + QStringLiteral("/Videos/ScreenCaptures");
    QDir().mkpath(m_dir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    m_raw = m_dir + QStringLiteral("/.") + stamp + QStringLiteral("-raw.mkv");
    m_out = m_dir + QStringLiteral("/") + stamp + QStringLiteral(".mp4");
    m_limit->setSingleShot(true);
    connect(m_limit, &QTimer::timeout, this, &Recorder::stop);
    connect(m_tick, &QTimer::timeout, this, [this] {
        const qint64 s = m_clock.elapsed() / 1000;
        if (m_time)
            m_time->setText(QStringLiteral("Recording %1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0')));
    });
    m_grace->setSingleShot(true);
    connect(m_grace, &QTimer::timeout, this, [this] { if (m_ffmpeg) m_ffmpeg->kill(); });
}

/* The monitor of the default sink carries what the speakers play. */
QString Recorder::monitorSource() const
{
    QProcess p;
    p.start(QStringLiteral("pactl"), { QStringLiteral("get-default-sink") });
    p.waitForFinished(3000);
    const QString sink = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
    if (p.exitCode() == 0 && !sink.isEmpty())
        return sink + QStringLiteral(".monitor");
    return QStringLiteral("@DEFAULT_MONITOR@");
}

/* Straight to the bar's notification service. */
void Recorder::notify(const QString &subject)
{
    QDBusInterface n(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                     QStringLiteral("org.freedesktop.Notifications"), QDBusConnection::sessionBus());
    n.call(QStringLiteral("Notify"), QStringLiteral("Screen recording"), 0u, QString(), subject, QString(),
           QStringList(), QVariantMap(), -1);
}

bool Recorder::start()
{
    const QString display = qEnvironmentVariable("DISPLAY", QStringLiteral(":0"));
    QStringList args {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
        QStringLiteral("-thread_queue_size"), QStringLiteral("512"),
        QStringLiteral("-f"), QStringLiteral("x11grab"), QStringLiteral("-framerate"), QString::number(FPS),
        QStringLiteral("-video_size"), QStringLiteral("%1x%2").arg(m_area.width()).arg(m_area.height()),
        QStringLiteral("-i"), QStringLiteral("%1+%2,%3").arg(display).arg(m_area.x()).arg(m_area.y()),
        QStringLiteral("-thread_queue_size"), QStringLiteral("512"),
        QStringLiteral("-f"), QStringLiteral("pulse"), QStringLiteral("-i"), monitorSource(),
        /* the cheapest x264 setting keeps the recording light on the CPU; the
         * real compression happens afterwards */
        QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
        QStringLiteral("-crf"), QStringLiteral("18"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-g"), QString::number(FPS * 2),
        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
        QStringLiteral("-t"), QString::number(LIMIT_MS / 1000),
        m_raw
    };
    m_ffmpeg = new QProcess(this);
    m_ffmpeg->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_ffmpeg, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) { captureEnded(code); });
    m_ffmpeg->start(QStringLiteral("ffmpeg"), args);
    if (!m_ffmpeg->waitForStarted(5000)) {
        notify(tr("ffmpeg could not be started"));
        return false;
    }
    m_clock.start();
    m_limit->start(LIMIT_MS);
    m_tick->start(1000);
    showFrame();
    showPanel();
    return true;
}

static QWidget *bar(const QRect &r)
{
    QWidget *w = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    w->setAttribute(Qt::WA_ShowWithoutActivating, true);
    w->setStyleSheet(QStringLiteral("background: black;"));
    w->setGeometry(r);
    w->show();
    return w;
}

/* Four thin black bars just outside the capture rectangle. */
void Recorder::showFrame()
{
    const QRect a = m_area;
    m_frame << bar(QRect(a.x() - FRAME, a.y() - FRAME, a.width() + 2 * FRAME, FRAME))
            << bar(QRect(a.x() - FRAME, a.y() + a.height(), a.width() + 2 * FRAME, FRAME))
            << bar(QRect(a.x() - FRAME, a.y(), FRAME, a.height()))
            << bar(QRect(a.x() + a.width(), a.y(), FRAME, a.height()));
}

/* Elapsed time and the Stop button, bottom-left, in the bar's panel style. */
void Recorder::showPanel()
{
    m_panel = new QFrame(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    m_panel->setObjectName(QStringLiteral("wignixNotice"));
    m_panel->setAttribute(Qt::WA_ShowWithoutActivating, true);
    m_panel->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout *row = new QHBoxLayout(m_panel);
    row->setContentsMargins(10, 6, 10, 6);
    row->setSpacing(12);
    m_time = new QLabel(QStringLiteral("Recording 00:00"), m_panel);
    m_time->setObjectName(QStringLiteral("noticeApp"));
    QPushButton *stop = new QPushButton(tr("Stop"), m_panel);
    stop->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_time);
    row->addWidget(stop);
    connect(stop, &QPushButton::clicked, this, &Recorder::stop);
    m_panel->adjustSize();
    const QRect screen = qApp->primaryScreen()->geometry();
    m_panel->move(screen.x() + 4, screen.y() + screen.height() - m_panel->height() - 4);
    m_panel->show();
}

void Recorder::stop()
{
    if (m_stopping || !m_ffmpeg)
        return;
    m_stopping = true;
    m_limit->stop();
    m_tick->stop();
    for (QWidget *w : m_frame)
        w->hide();
    if (m_panel)
        m_panel->hide();
    /* 'q' lets ffmpeg close the file properly */
    m_ffmpeg->write("q\n");
    m_grace->start(8000);
    emit stopped();
}

void Recorder::captureEnded(int code)
{
    m_grace->stop();
    if (code != 0 && !QFile::exists(m_raw)) {
        notify(tr("Recording failed"));
        qApp->exit(1);
        return;
    }
    compress();
}

/* Second pass at low priority: a compact, shareable MP4; the raw capture
 * goes away once it is written. */
void Recorder::compress()
{
    const QString name = QFileInfo(m_out).fileName();
    notify(tr("Compressing %1 …").arg(name));
    m_pass2 = new QProcess(this);
    m_pass2->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_pass2, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, name](int code, QProcess::ExitStatus) {
        if (code == 0) {
            QFile::remove(m_raw);
            notify(tr("Saved %1").arg(name));
        } else {
            notify(tr("Compression failed; the raw capture was kept"));
        }
        qApp->exit(code == 0 ? 0 : 1);
    });
    m_pass2->start(QStringLiteral("nice"), {
        QStringLiteral("-n"), QStringLiteral("19"), QStringLiteral("ffmpeg"),
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
        QStringLiteral("-i"), m_raw,
        QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("medium"),
        QStringLiteral("-crf"), QStringLiteral("23"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("128k"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"), m_out });
}
