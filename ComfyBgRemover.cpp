#include "ComfyBgRemover.h"

#include <QBuffer>
#include <QHttpMultiPart>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QDebug>

ComfyBgRemover::ComfyBgRemover(const QString &host, QObject *parent)
    : QObject(parent), m_host(host)
{
    m_pollTimer.setInterval(600);
    connect(&m_pollTimer, &QTimer::timeout, this, &ComfyBgRemover::pollHistory);
}

void ComfyBgRemover::process(const QMap<int, QPixmap> &frames, const QString &model, const QString &nodeType)
{
    if (frames.isEmpty()) return;
    m_frames    = frames;
    m_model     = model;
    m_nodeType  = nodeType;
    m_queue     = frames.keys();
    m_doneCount = 0;
    m_cancelled = false;
    processNext();
}

void ComfyBgRemover::cancel()
{
    m_cancelled = true;
    m_pollTimer.stop();
    m_queue.clear();
}

void ComfyBgRemover::processNext()
{
    if (m_cancelled || m_queue.isEmpty())
    {
        emit finished();
        return;
    }
    m_currentIndex = m_queue.takeFirst();
    uploadFrame();
}

// ── Step 1: upload frame as PNG ──────────────────────────────────────────────

void ComfyBgRemover::uploadFrame()
{
    // Serialize current frame to PNG in memory
    QByteArray pngData;
    QBuffer buf(&pngData);
    buf.open(QIODevice::WriteOnly);
    m_frames[m_currentIndex].toImage().save(&buf, "PNG");
    buf.close();

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        "form-data; name=\"image\"; filename=\"frame.png\"");
    imagePart.setBody(pngData);
    multiPart->append(imagePart);

    QHttpPart typePart;
    typePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       "form-data; name=\"type\"");
    typePart.setBody("input");
    multiPart->append(typePart);

    QHttpPart overwritePart;
    overwritePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                            "form-data; name=\"overwrite\"");
    overwritePart.setBody("true");
    multiPart->append(overwritePart);

    auto *reply = m_net.post(
        QNetworkRequest(QUrl(m_host + "/upload/image")), multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]
    {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            emit error("Upload fehlgeschlagen: " + reply->errorString());
            return;
        }
        const auto doc  = QJsonDocument::fromJson(reply->readAll());
        const QString fn = doc.object().value("name").toString();
        if (fn.isEmpty())
        {
            emit error("Upload: kein Dateiname in Antwort");
            return;
        }
        submitWorkflow(fn);
    });
}

// ── Step 2: submit workflow ───────────────────────────────────────────────────

void ComfyBgRemover::submitWorkflow(const QString &uploadedFilename)
{
    QJsonObject loadImage;
    loadImage["class_type"] = "LoadImage";
    loadImage["inputs"] = QJsonObject{
        {"image",  uploadedFilename},
        {"upload", "image"}
    };

    QJsonObject birefnet;
    birefnet["class_type"] = m_nodeType;
    if (m_nodeType == "BiRefNetRMBG")
    {
        birefnet["inputs"] = QJsonObject{
            {"image",             QJsonArray{"1", 0}},
            {"model",             m_model},
            {"mask_blur",         0},
            {"mask_offset",       0},
            {"invert_output",     false},
            {"refine_foreground", true},
            {"background",        "Alpha"}
        };
    }
    else
    {
        birefnet["inputs"] = QJsonObject{
            {"image",                QJsonArray{"1", 0}},
            {"model",                m_model},
            {"load_local_model",     false},
            {"background_color_name","transparency"},
            {"device",               "auto"}
        };
    }

    QJsonObject saveImage;
    saveImage["class_type"] = "SaveImage";
    saveImage["inputs"] = QJsonObject{
        {"images",          QJsonArray{"2", 0}},
        {"filename_prefix", "rmbg_frame"}
    };

    QJsonObject prompt{
        {"1", loadImage},
        {"2", birefnet},
        {"3", saveImage}
    };

    const QByteArray body = QJsonDocument(QJsonObject{
        {"prompt",    prompt},
        {"client_id", "vlcqtplayer"}
    }).toJson(QJsonDocument::Compact);

    QNetworkRequest req(QUrl(m_host + "/prompt"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = m_net.post(req, body);

    connect(reply, &QNetworkReply::finished, this, [this, reply]
    {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            emit error("Prompt fehlgeschlagen: " + reply->errorString());
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        m_currentPromptId = doc.object().value("prompt_id").toString();
        if (m_currentPromptId.isEmpty())
        {
            emit error("Kein prompt_id in Antwort");
            return;
        }
        m_pollTimer.start();
    });
}

// ── Step 3: poll until done ───────────────────────────────────────────────────

void ComfyBgRemover::pollHistory()
{
    auto *reply = m_net.get(
        QNetworkRequest(QUrl(m_host + "/history/" + m_currentPromptId)));

    connect(reply, &QNetworkReply::finished, this, [this, reply]
    {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return; // retry next tick

        const auto root = QJsonDocument::fromJson(reply->readAll()).object();
        const auto job  = root.value(m_currentPromptId).toObject();
        if (job.isEmpty()) return; // not ready yet

        const auto status = job.value("status").toObject();
        if (!status.value("completed").toBool()) return;

        m_pollTimer.stop();

        // Find output image from SaveImage node ("3")
        const auto outputs = job.value("outputs").toObject();
        const auto node3   = outputs.value("3").toObject();
        const auto images  = node3.value("images").toArray();
        if (images.isEmpty())
        {
            emit error("Keine Ausgabe in Node 3");
            return;
        }
        const auto img0      = images[0].toObject();
        const QString fn     = img0.value("filename").toString();
        const QString sub    = img0.value("subfolder").toString();
        downloadResult(fn, sub);
    });
}

// ── Step 4: download result ───────────────────────────────────────────────────

void ComfyBgRemover::downloadResult(const QString &filename, const QString &subfolder)
{
    const QString url = QString("%1/view?filename=%2&subfolder=%3&type=output")
        .arg(m_host, filename, subfolder);

    auto *reply = m_net.get(QNetworkRequest(QUrl(url)));

    connect(reply, &QNetworkReply::finished, this, [this, reply]
    {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            emit error("Download fehlgeschlagen: " + reply->errorString());
            return;
        }

        QImage img;
        img.loadFromData(reply->readAll(), "PNG");
        if (img.isNull())
        {
            emit error("Ungültiges Bild vom Server");
            return;
        }

        // Ensure ARGB32 so transparency is preserved in QPixmap
        const QPixmap result = QPixmap::fromImage(
            img.convertToFormat(QImage::Format_ARGB32));

        ++m_doneCount;
        emit frameReady(m_currentIndex, result);
        emit progress(m_doneCount, m_frames.size());
        processNext();
    });
}
