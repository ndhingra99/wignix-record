/* WigNix screen recording. Shift+Print runs this: the first press picks a
 * region with slop and starts recording; a press while recording stops it. */
#include "recorder.h"

#include <QApplication>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QRect>
#include <csignal>
#include <unistd.h>

static volatile sig_atomic_t stopRequested = 0;
static void onUsr1(int) { stopRequested = 1; }

static QString pidPath()
{
    QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + QStringLiteral("/wignix-record-%1.pid").arg(getuid());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    /* already recording: tell that instance to stop */
    QFile pid(pidPath());
    if (pid.open(QIODevice::ReadOnly)) {
        const pid_t other = QString::fromLatin1(pid.readAll()).trimmed().toInt();
        pid.close();
        if (other > 0 && kill(other, 0) == 0) {
            kill(other, SIGUSR1);
            return 0;
        }
    }

    QFile qss(QStringLiteral("/etc/xdg/wignix-menu.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    /* the pointer becomes a crosshair; Escape cancels */
    QProcess slop;
    slop.start(QStringLiteral("slop"), { QStringLiteral("-f"), QStringLiteral("%x %y %w %h"), QStringLiteral("-b"), QStringLiteral("2"),
                                         QStringLiteral("-c"), QStringLiteral("0,0,0,1"), QStringLiteral("-o"), QStringLiteral("-q") });
    slop.waitForFinished(-1);
    if (slop.exitCode() != 0)
        return 1;
    const QStringList f = QString::fromLatin1(slop.readAllStandardOutput()).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (f.size() != 4)
        return 1;
    /* even sizes for yuv420p; at least a small box */
    int x = f[0].toInt() & ~1, y = f[1].toInt() & ~1;
    int w = qMax(16, f[2].toInt() & ~1), h = qMax(16, f[3].toInt() & ~1);

    if (pid.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        pid.write(QByteArray::number(getpid()));
        pid.close();
    }
    signal(SIGUSR1, onUsr1);

    Recorder rec(QRect(x, y, w, h));
    if (!rec.start()) {
        QFile::remove(pidPath());
        return 1;
    }
    /* once stopped, the next key press may start a new recording while this
     * one is still being compressed */
    QObject::connect(&rec, &Recorder::stopped, [] { QFile::remove(pidPath()); });
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&rec] { if (stopRequested) { stopRequested = 0; rec.stop(); } });
    poll.start(200);
    const int rc = app.exec();
    QFile::remove(pidPath());
    return rc;
}
