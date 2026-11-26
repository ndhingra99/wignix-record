/* WigNix screen recording: a slop-picked region is captured by ffmpeg with
 * the system's audio. A black frame marks the region while it records and a
 * small panel in the bottom-left corner shows the elapsed time and a Stop
 * button. Recording stops on Stop, on a second key press, or after thirty
 * minutes; the capture is then compressed to an MP4 at low priority and the
 * bar's notification service shows the file name. */
#pragma once

#include <QObject>
#include <QRect>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QFrame>
#include <QLabel>
#include <QList>
#include <QString>

class Recorder : public QObject
{
    Q_OBJECT
public:
    explicit Recorder(const QRect &area, QObject *parent = nullptr);
    bool start();

public slots:
    void stop();

signals:
    void stopped();

private:
    QRect m_area;
    QProcess *m_ffmpeg = nullptr;
    QProcess *m_pass2 = nullptr;
    QString m_dir, m_raw, m_out;
    QTimer *m_limit, *m_tick, *m_grace;
    QElapsedTimer m_clock;
    QList<QWidget *> m_frame;
    QFrame *m_panel = nullptr;
    QLabel *m_time = nullptr;
    bool m_stopping = false;
    QString monitorSource() const;
    void showFrame();
    void showPanel();
    void captureEnded(int code);
    void compress();
    static void notify(const QString &subject);
};
