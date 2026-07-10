#include "FrameExtractor.h"

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

FrameExtractor::FrameExtractor(const QString &ffmpegPath, QObject *parent)
    : QObject(parent), m_ffmpeg(ffmpegPath)
{}

void FrameExtractor::extract(const QString &videoPath)
{
    m_videoPath = videoPath;
    m_stderrBuf.clear();
    m_total = 0;

    if (!m_tempDir.isValid())
    {
        emit error("Temp-Verzeichnis konnte nicht erstellt werden");
        return;
    }

    // ffprobe: fps und Frameanzahl ermitteln
    const QString ffprobe = QFileInfo(m_ffmpeg).absolutePath() + "/ffprobe";
    QStringList probeArgs = {
        "-v", "quiet",
        "-select_streams", "v:0",
        "-show_entries", "stream=avg_frame_rate,nb_frames",
        "-of", "csv=p=0",
        videoPath
    };

    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FrameExtractor::onProbeFinished);
    m_process->start(ffprobe, probeArgs);
    if (!m_process->waitForStarted(3000))
    {
        m_process->deleteLater();
        m_process = nullptr;
        // ffprobe nicht gefunden — direkt extrahieren mit Default-fps
        runExtract();
    }
}

void FrameExtractor::onProbeFinished(int exitCode)
{
    const QString out = m_process->readAllStandardOutput().trimmed();
    m_process->deleteLater();
    m_process = nullptr;

    // Format: "num/den,nb_frames"  e.g. "30000/1001,450"
    if (exitCode == 0 && !out.isEmpty())
    {
        const QStringList parts = out.split(',');
        if (parts.size() >= 1)
        {
            const QStringList frac = parts[0].split('/');
            if (frac.size() == 2)
            {
                const double num = frac[0].toDouble();
                const double den = frac[1].toDouble();
                if (den > 0) m_fps = num / den;
            }
        }
        if (parts.size() >= 2)
            m_total = parts[1].trimmed().toInt();
    }

    runExtract();
}

void FrameExtractor::runExtract()
{
    const QString pattern = m_tempDir.path() + "/frame_%04d.png";
    QStringList args = {
        "-y",
        "-i",  m_videoPath,
        "-vsync", "0",        // keep all frames, no duplicate/drop
        pattern
    };

    m_stderrBuf.clear();
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &FrameExtractor::onExtractOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FrameExtractor::onExtractFinished);

    m_process->start(m_ffmpeg, args);
    if (!m_process->waitForStarted(3000))
    {
        emit error("ffmpeg nicht gefunden — ist es im PATH?");
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void FrameExtractor::onExtractOutput()
{
    const QString out = m_process->readAllStandardError();
    m_stderrBuf += out;

    // Parse "frame=N" for live progress
    const int pos = out.lastIndexOf("frame=");
    if (pos >= 0)
    {
        const int eol   = out.indexOf(' ', pos + 6);
        const int frame = out.mid(pos + 6, eol - pos - 6).trimmed().toInt();
        if (frame > 0)
            emit progress(frame, qMax(frame, m_total));
    }
}

void FrameExtractor::onExtractFinished(int exitCode)
{
    m_stderrBuf += m_process->readAllStandardError();
    m_process->deleteLater();
    m_process = nullptr;

    if (exitCode != 0)
    {
        const QString detail = m_stderrBuf.length() > 800
            ? "…" + m_stderrBuf.right(800) : m_stderrBuf;
        emit error(QString("ffmpeg Code %1:\n%2").arg(exitCode).arg(detail));
        return;
    }

    loadFrames();
}

void FrameExtractor::loadFrames()
{
    QDir dir(m_tempDir.path());
    const QStringList files = dir.entryList({"frame_*.png"}, QDir::Files, QDir::Name);

    if (files.isEmpty())
    {
        emit error("Keine Frames extrahiert");
        return;
    }

    const int total = files.size();
    // Recalculate fps from actual frame count if ffprobe gave 0
    if (m_fps <= 0) m_fps = 25.0;
    const int delayMs = qMax(16, qRound(1000.0 / m_fps));

    QVector<QPixmap> frames;
    frames.reserve(total);
    for (int i = 0; i < total; ++i)
    {
        QPixmap px(dir.absoluteFilePath(files[i]));
        if (!px.isNull())
            frames.append(px);
        emit progress(i + 1, total);
    }

    emit finished(frames, delayMs);
}

void FrameExtractor::extractFirstFrame(const QString &videoPath, QSize maxSize)
{
    if (!m_tempDir.isValid())
    {
        emit firstFrameReady(videoPath, QPixmap());
        return;
    }

    m_videoPath         = videoPath;
    m_firstFrameMaxSize = maxSize;
    m_stderrBuf.clear();

    const QString outPath = m_tempDir.path() + "/preview.png";
    QFile::remove(outPath); // alte preview.png aus vorherigem Aufruf entfernen
    const QString scale = QString("scale=%1:%2:force_original_aspect_ratio=decrease")
                              .arg(maxSize.width()).arg(maxSize.height());
    const QStringList args = {
        "-y",
        "-i",        videoPath,
        "-frames:v", "1",
        "-vf",       scale,
        "-update",   "1",
        outPath
    };

    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FrameExtractor::onFirstFrameFinished);
    m_process->start(m_ffmpeg, args);
    if (!m_process->waitForStarted(3000))
    {
        m_process->deleteLater();
        m_process = nullptr;
        emit firstFrameReady(videoPath, QPixmap());
    }
}

void FrameExtractor::onFirstFrameFinished(int exitCode)
{
    const QString src     = m_videoPath;
    const QString outPath = m_tempDir.path() + "/preview.png";
    m_process->deleteLater();
    m_process = nullptr;

    QPixmap px;
    if (exitCode == 0) px.load(outPath);
    QFile::remove(outPath);

    emit firstFrameReady(src, px);
}

void FrameExtractor::cancel()
{
    if (m_process)
    {
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
}
