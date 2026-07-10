#pragma once
#include <QObject>
#include <QPixmap>
#include <QSize>
#include <QTemporaryDir>
#include <QVector>

class QProcess;

class FrameExtractor : public QObject
{
    Q_OBJECT
public:
    explicit FrameExtractor(const QString &ffmpegPath = "ffmpeg",
                            QObject *parent = nullptr);

    void extract(const QString &videoPath);
    void extractFirstFrame(const QString &videoPath, QSize maxSize = QSize(64, 64));
    void cancel();

signals:
    void progress(int done, int total);
    void finished(QVector<QPixmap> frames, int delayMs);
    void firstFrameReady(QString sourcePath, QPixmap preview);
    void error(const QString &msg);

private slots:
    void onProbeFinished(int exitCode);
    void onExtractOutput();
    void onExtractFinished(int exitCode);
    void onFirstFrameFinished(int exitCode);

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
    QSize          m_firstFrameMaxSize;
};
