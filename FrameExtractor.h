#pragma once
#include <QObject>
#include <QPixmap>
#include <QTemporaryDir>

class QProcess;

class FrameExtractor : public QObject
{
    Q_OBJECT
public:
    explicit FrameExtractor(const QString &ffmpegPath = "ffmpeg",
                            QObject *parent = nullptr);

    void extract(const QString &videoPath);
    void cancel();

signals:
    void progress(int done, int total);
    void finished(QMap<int, QPixmap> frames, int delayMs);
    void error(const QString &msg);

private slots:
    void onProbeFinished(int exitCode);
    void onExtractOutput();
    void onExtractFinished(int exitCode);

private:
    void runExtract();
    void loadFrames();

    QString        m_ffmpeg;
    QString        m_videoPath;
    QTemporaryDir  m_tempDir;
    QProcess      *m_process = nullptr;
    double         m_fps     = 25.0;
    int            m_total   = 0;
    QString        m_stderrBuf;
};
