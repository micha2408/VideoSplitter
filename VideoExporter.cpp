#include "VideoExporter.h"

#include <QProcess>
#include <QDir>
#include <QDebug>

VideoExporter::VideoExporter(QObject *parent)
    : QObject(parent)
{}

void VideoExporter::exportFrames(const QMap<int, QPixmap> &frames, const Options &opts)
{
    if (frames.isEmpty()) { emit error("Keine Frames vorhanden"); return; }
    if (!m_tempDir.isValid()) { emit error("Temp-Verzeichnis konnte nicht erstellt werden"); return; }

    m_opts       = opts;
    m_frameCount = frames.size();

    writeFrames(frames);

    const QString inputPattern = m_tempDir.path() + "/frame_%04d.png";
    const QStringList args     = buildArgs(opts, inputPattern);

    m_stderrBuf.clear();
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardError, this, &VideoExporter::onProcessOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VideoExporter::onProcessFinished);

    qDebug() << opts.ffmpegPath << args;
    m_process->start(opts.ffmpegPath, args);
    if (!m_process->waitForStarted(3000))
    {
        emit error("ffmpeg nicht gefunden — ist es im PATH?");
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void VideoExporter::cancel()
{
    if (m_process)
    {
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void VideoExporter::writeFrames(const QMap<int, QPixmap> &frames)
{
    int i = 0;
    for (const QPixmap &px : frames)
    {
        const QString path = QString("%1/frame_%2.png")
            .arg(m_tempDir.path())
            .arg(i++, 4, 10, QChar('0'));
        px.save(path, "PNG");
        emit progress(i, m_frameCount);
    }
}

QStringList VideoExporter::buildArgs(const Options &opts, const QString &inputPattern) const
{
    QStringList args;
    args << "-y"                          // overwrite output
         << "-framerate" << QString::number(opts.fps, 'f', 3)
         << "-i" << inputPattern;

    switch (opts.format)
    {
    case MP4:
        // scale ensures even dimensions — H.264/yuv420p requires width+height divisible by 2
        args << "-vf"    << "scale=trunc(iw/2)*2:trunc(ih/2)*2"
             << "-c:v"   << "libx264"
             << "-pix_fmt" << "yuv420p"
             << "-crf"   << "18"
             << "-movflags" << "+faststart";
        break;

    case WebM:
        // VP9 supports alpha channel (yuva420p)
        args << "-c:v" << "libvpx-vp9"
             << "-pix_fmt" << "yuva420p"
             << "-crf" << "15"
             << "-b:v" << "0";
        break;

    case GIF:
        // Two-pass: generate optimal palette first
        args.clear();
        args << "-y"
             << "-framerate" << QString::number(opts.fps, 'f', 3)
             << "-i" << inputPattern
             << "-vf" << QString("fps=%1,split[s0][s1];[s0]palettegen=max_colors=256[p];[s1][p]paletteuse=dither=bayer")
                            .arg(opts.fps, 0, 'f', 3)
             << "-loop" << "0";            // loop forever
        break;

    case PNG_Sequence:
        args << "-c:v" << "png";
        // output path will be a pattern like out_%04d.png
        break;
    }

    args << opts.outputPath;
    return args;
}

void VideoExporter::onProcessOutput()
{
    const QString out = m_process->readAllStandardError();
    m_stderrBuf += out;

    // Parse "frame=N" for progress
    const int pos = out.lastIndexOf("frame=");
    if (pos >= 0)
    {
        const int eol   = out.indexOf(' ', pos + 6);
        const int frame = out.mid(pos + 6, eol - pos - 6).trimmed().toInt();
        if (frame > 0)
            emit progress(frame, m_frameCount);
    }
}

void VideoExporter::onProcessFinished(int exitCode)
{
    // Flush remaining stderr
    if (m_process)
        m_stderrBuf += m_process->readAllStandardError();

    m_process->deleteLater();
    m_process = nullptr;

    if (exitCode == 0)
    {
        emit finished(m_opts.outputPath);
    }
    else
    {
        // Show last 800 chars of stderr so user sees the actual ffmpeg error
        const QString detail = m_stderrBuf.length() > 800
            ? "…" + m_stderrBuf.right(800)
            : m_stderrBuf;
        emit error(QString("ffmpeg Code %1:\n\n%2").arg(exitCode).arg(detail));
    }
}
