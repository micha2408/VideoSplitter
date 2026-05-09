#pragma once
#include <QObject>
#include <QMap>
#include <QPixmap>
#include <QTemporaryDir>

class QProcess;

class VideoExporter : public QObject
{
    Q_OBJECT
public:
    enum Format { MP4, WebM, GIF, PNG_Sequence };

    struct Options
    {
        Format  format    = MP4;
        double  fps       = 25.0;
        QString outputPath;
        QString ffmpegPath = "ffmpeg";  // from PATH by default
    };

    explicit VideoExporter(QObject *parent = nullptr);

    // frames: index -> pixmap (already cropped/processed)
    void exportFrames(const QMap<int, QPixmap> &frames, const Options &opts);
    void cancel();

signals:
    void progress(int done, int total);
    void finished(const QString &outputPath);
    void error(const QString &msg);

private slots:
    void onProcessFinished(int exitCode);
    void onProcessOutput();

private:
    void writeFrames(const QMap<int, QPixmap> &frames);
    QStringList buildArgs(const Options &opts, const QString &inputPattern) const;

    QTemporaryDir  m_tempDir;
    QProcess      *m_process = nullptr;
    Options        m_opts;
    int            m_frameCount = 0;
};
