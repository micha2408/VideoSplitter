#pragma once
#include <QObject>
#include <QPixmap>
#include <QMap>
#include <QTimer>
#include <QNetworkAccessManager>

class ComfyBgRemover : public QObject
{
    Q_OBJECT
public:
    explicit ComfyBgRemover(const QString &host = "http://127.0.0.1:8188",
                            QObject *parent = nullptr);

    void process(const QMap<int, QPixmap> &frames,
                 const QString &model    = "BiRefNet-general",
                 const QString &nodeType = "BiRefNet_Hugo");
    void cancel();

signals:
    void frameReady(int index, QPixmap result);
    void progress(int done, int total);
    void finished();
    void error(const QString &msg);

private slots:
    void pollHistory();

private:
    void processNext();
    void uploadFrame();
    void submitWorkflow(const QString &uploadedFilename);
    void downloadResult(const QString &filename, const QString &subfolder);

    QString                m_host;
    QString                m_model;
    QString                m_nodeType;
    QNetworkAccessManager  m_net;
    QTimer                 m_pollTimer;

    QMap<int, QPixmap>     m_frames;
    QList<int>             m_queue;
    int                    m_currentIndex = -1;
    QString                m_currentPromptId;
    int                    m_doneCount    = 0;
    bool                   m_cancelled    = false;
};
