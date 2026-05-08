#include "VideoWidget.h"

#include <QLabel>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QVBoxLayout>
#include <QDebug>
#include <QDir>
#include <QMovie>

#include <windows.h>
#include <QPicture>
#include <QMenuBar>
#include <QMenu>
#include <QRubberBand>
#include <QPainter>
#include "splitview.h"
#include "Label.h"

#include <QTimer>

#include <QSettings>

#define MENU(a,b,checkable)\
{\
    QAction *act = menu->addAction( #a " " #b ) ;\
    act -> setCheckable(checkable); \
    act->setObjectName(#a "_" #b); \
    connect(act ,&QAction::triggered,this,&VideoWidget::menu_##a##_##b);\
 }

VideoWidget::VideoWidget(QWidget *parent)
    : QMainWindow(parent)
    , splitView(nullptr)
{
    setWindowTitle("VLC Frame Grabber (Qt 5)");
    QCoreApplication::setOrganizationName("michaelSW");
    QCoreApplication::setOrganizationDomain("uyuni.de");
    QCoreApplication::setApplicationName("FrameGrabber");

    QMenuBar *bar = new QMenuBar(this);
    setMenuBar(bar);
    QMenu *menu = new QMenu("menu",bar);
    bar->addMenu(menu);    
    MENU(split,1024,false)
    MENU(split,2048,false)
    MENU(uncrop,pic,false)
    MENU(pause,video,true)
    menu->dumpObjectTree();
    resize(800, 600);
    // Zentrales Widget + Layout
    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    m_label = new Label("Zieh ein Video hierher", central);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    layout->addWidget(m_label);
    setCentralWidget(central);

    QMetaObject::invokeMethod(this, [this]
        {
            QSettings settings;
            doDropEvent(settings.value("video").toString());
        }, Qt::QueuedConnection);
    // Drag & Drop aktivieren
    setAcceptDrops(true);

    // libVLC initialisieren
    initVlc();

}

void VideoWidget::menu_split_1024(bool)
{
    if(splitView)
    {   // dont forget to cleanup the old...
    } else
    {
        splitView = new SplitView(m_label);
    }
    m_label->sendSize(1024);
    splitView->show();

}
void VideoWidget::menu_split_2048(bool)
{
    if(splitView)
    {   // dont forget to cleanup the old...
    } else
    {
        splitView = new SplitView(m_label);
    }
    m_label->sendSize(2048);
    splitView->show();
}
void VideoWidget::menu_uncrop_pic(bool)
{
    m_label->resetSelection();

}
void VideoWidget::menu_pause_video(bool checked)
{
    gif.setPaused(checked);
}

VideoWidget::~VideoWidget()
{
    releaseVlc();
}

void VideoWidget::initVlc()
{
    wchar_t path[256];
    QString dllPath(QDir::toNativeSeparators(VLC_SDK_PATH "/lib"));
    dllPath.toWCharArray(path);
    qDebug() << dllPath << " : " << SetDllDirectoryW(path);

    // m_vlcInstance = libvlc_new(sizeof(vlc_args)/sizeof(vlc_args[0]), vlc_args);
    m_vlcInstance = libvlc_new(0,nullptr);
    if (!m_vlcInstance) {
        qWarning() << "libVLC konnte nicht initialisiert werden!";
        return;
    }

    m_mediaPlayer = libvlc_media_player_new(m_vlcInstance);
    if (!m_mediaPlayer) {
        qWarning() << "Media Player konnte nicht erstellt werden!";
        return;
    }

    // Callbacks registrieren
    libvlc_video_set_callbacks(
        m_mediaPlayer,
        &VideoWidget::lockCallback,
        &VideoWidget::unlockCallback,
        &VideoWidget::displayCallback,
        this);

    libvlc_video_set_format_callbacks(
        m_mediaPlayer,
        &VideoWidget::formatCallback,
        &VideoWidget::formatCleanupCallback);

    libvlc_event_manager_t* eventManager = libvlc_media_player_event_manager(m_mediaPlayer);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerEndReached, [](const libvlc_event_t*, void*d)
        {
            auto self = static_cast<VideoWidget*>(d);
            QMetaObject::invokeMethod(self->m_label, [self]
                                      {
                                          libvlc_media_player_stop(self->m_mediaPlayer);
                                          libvlc_media_player_play(self->m_mediaPlayer);
                                          self->frame.count=self->frame.current;
                                          self->frame.current=0;
                                      });

        }, this );
}


void VideoWidget::releaseVlc()
{
    if (m_mediaPlayer) {
#if (LIBVLC_VERSION_MAJOR == 4)
        libvlc_media_player_stop_async(m_mediaPlayer);
#else
        libvlc_media_player_stop(m_mediaPlayer);
#endif
        libvlc_media_player_release(m_mediaPlayer);
        m_mediaPlayer = nullptr;
    }
    if (m_media) {
        libvlc_media_release(m_media);
        m_media = nullptr;
    }
    if (m_vlcInstance) {
        libvlc_release(m_vlcInstance);
        m_vlcInstance = nullptr;
    }
}

// Drag & Drop
void VideoWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void VideoWidget::doDropEvent(const QString &path)
{
    if (path.isEmpty()) return;
    gif.stop();
#if (LIBVLC_VERSION_MAJOR == 4)
    libvlc_media_player_stop_async(m_mediaPlayer);
#else
    libvlc_media_player_stop(m_mediaPlayer);
#endif
    m_label->resetSelAdjList();
    if(path.endsWith(".gif"))
    {
        gif.setFileName(path);
        if(gif.isValid())
        {
            eTime.invalidate();
            connect(&gif, &QMovie::frameChanged, this, [this](int)
                    {
                        processFrame(gif.currentImage(),gif.currentFrameNumber(),gif.frameCount());
                    });
            gif.start();
        } else return;

    } else
    {
        frame = Frame();
        eTime.invalidate();
        if(!playFile(path)) return;
    }
    QSettings settings;
    settings.setValue("video",path);
}

void VideoWidget::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;

    doDropEvent(urls.first().toLocalFile());
}

void VideoWidget::processFrame(const QImage &img, int index, int count)
{
    if(eTime.isValid())
    {
        m_label->setImage(QPixmap::fromImage(img), index, count, eTime.elapsed());
        eTime.restart();
        m_label->update();
    } else eTime.start();
}

bool VideoWidget::playFile(const QString &path)
{
    if (!m_vlcInstance || !m_mediaPlayer)
    {
        qWarning() << "VLC nicht initialisiert!";
        return false;
    }

    if (m_media)
    {
        libvlc_media_release(m_media);
        m_media = nullptr;
    }

    QByteArray ba = QDir::toNativeSeparators(path).toUtf8();
#if (LIBVLC_VERSION_MAJOR == 4)
    m_media = libvlc_media_new_path(ba.constData());
#else
    m_media = libvlc_media_new_path(m_vlcInstance, ba.constData());
#endif

    if (!m_media)
    {
        qWarning() << "Konnte Media nicht laden:" << path;
        return false;
    }
    libvlc_media_player_set_media(m_mediaPlayer, m_media);
    libvlc_media_player_play(m_mediaPlayer);
    return true;
}

// ---- libVLC Callback-Implementierung ----

// Wird von VLC aufgerufen, um einen Zeiger auf den Framebuffer zu bekommen
void *VideoWidget::lockCallback(void *opaque, void **planes)
{
    VideoWidget *self = static_cast<VideoWidget*>(opaque);
    self->m_frameMutex.lock();

    *planes = self->frame.image.bits();
    return nullptr;
}

// Wird nach dem Schreiben des Frames aufgerufen
void VideoWidget::unlockCallback(void *opaque, void *picture, void *const *planes)
{
    Q_UNUSED(picture);
    Q_UNUSED(planes);
    VideoWidget *self = static_cast<VideoWidget*>(opaque);
    self->m_frameMutex.unlock();
}

// Wird aufgerufen, wenn ein Frame "fertig" ist und angezeigt werden soll
void VideoWidget::displayCallback(void *opaque, void *picture)
{
    Q_UNUSED(picture);
    VideoWidget *self = static_cast<VideoWidget*>(opaque);

    libvlc_media_stats_t p_stats;
    libvlc_media_get_stats(self->m_media,&p_stats);
    QMutexLocker locker(&self->m_frameMutex);

    QMetaObject::invokeMethod(self->m_label, [self]
                              {
                                  self->processFrame(self->frame.image.copy(),self->frame.current,self->frame.count);
                                  self->frame.current++;
                              });
}

// Wird von VLC aufgerufen, um das Videoformat festzulegen
unsigned VideoWidget::formatCallback(void **opaque, char *chroma,
                                    unsigned *width, unsigned *height,
                                    unsigned *pitches, unsigned *lines)
{
    VideoWidget *self = static_cast<VideoWidget*>(*opaque);

    // Wir wollen 32-bit RGB (RV32 = BGRA → QImage::Format_ARGB32 passt)
#if (LIBVLC_VERSION_MAJOR == 4)
    chroma[0] = 'B';
    chroma[1] = 'G';
    chroma[2] = 'R';
    chroma[3] = 'A';
#else
    chroma[0] = 'R';
    chroma[1] = 'V';
    chroma[2] = '3';
    chroma[3] = '2';
#endif


    self->m_videoWidth = *width;
    self->m_videoHeight = *height;
    self->m_pitch = self->m_videoWidth * 4; // 4 Bytes pro Pixel

    *pitches = self->m_pitch;
    *lines = self->m_videoHeight;

    // Frame-Buffer anlegen
    self->m_frameMutex.lock();
    self->frame.newImage(self->m_videoWidth,self->m_videoHeight);
    self->m_frameMutex.unlock();

    return 1; // OK
}

void VideoWidget::formatCleanupCallback(void *opaque)
{
}
