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
#include <Label.h>

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

    // Drag & Drop aktivieren
    setAcceptDrops(true);

    // Voreinstellung: schwarzes Bild
    m_frame = QImage(640, 360, QImage::Format_ARGB32);
    m_frame.fill(Qt::black);

    // libVLC initialisieren
    initVlc();

    splitView = new SplitView(m_label);
    splitView->hide();

}

void VideoWidget::menu_split_1024(bool)
{
    m_label->sendSize(1024);
    splitView->show();

}
void VideoWidget::menu_split_2048(bool)
{
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

void Label::mousePressEvent(QMouseEvent *ev)
{
    origin = ev->pos();
    if (ev->modifiers() & Qt::ControlModifier)
    {
        // STRG ist gedrückt
        return;
    }
    if (!rubberBand)
        rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    saList << SelAdj();
    rubberBand->setGeometry(QRect(origin, QSize()));
    rubberBand->show();
}

void Label::mouseMoveEvent(QMouseEvent *ev)
{
    if (ev->modifiers() & Qt::ControlModifier)
    {
        setAdjustion(origin - ev->pos());
        return;
    }
    if (rubberBand)
        rubberBand->setGeometry(QRect(origin, ev->pos()).normalized());
}

void Label::mouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->modifiers() & Qt::ControlModifier)
    {
        // STRG ist gedrückt
        qDebug() << origin << ev->pos();
        return;
    }
    if (!rubberBand) return;
    rubberBand->hide();
    QRect newSel = QRect(origin, ev->pos()).normalized();
    if(newSel.width()<8)
    {
        resetSelection();
        resetSelection();
        return;
    }
    if(newSel.height()<8)
    {
        resetSelection();
        resetSelection();
        return;
    }
    setSelection(QRect(origin, ev->pos()).normalized());
}

void Label::paintEvent(QPaintEvent *e)
{
    if(imagePlus.image.isNull())
    {
        QLabel::paintEvent(e);
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPixmap scaled=imagePlus.image;
    for(auto sa:saList)
    {
        scaled=scaled.copy(sa.selection);
        scaled=scaled.scaled(
                           size(),
                           Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);
    }
    int x = (width() - scaled.width()) / 2;
    int y = (height() - scaled.height()) / 2;
    p.fillRect(rect(),QColor(Qt::blue));
    sendPic(this);
    p.drawPixmap(x, y, scaled);
    p.setPen(QPen(Qt::red, 2));
    p.drawRect(x, y, scaled.width(), scaled.height());


}

VideoWidget::~VideoWidget()
{
    releaseVlc();
}

void VideoWidget::initVlc()
{
    const char *vlc_args[] = {
        "--no-xlib"
    };

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
    libvlc_event_attach(eventManager, libvlc_MediaPlayerStopped, eventCallback, this);
}

void VideoWidget::eventCallback(const libvlc_event_t* event, void* data)
{
    VideoWidget* self = static_cast<VideoWidget*>(data);

    if (event->type == libvlc_MediaPlayerStopped)
    {
        libvlc_media_player_set_media(self->m_mediaPlayer, self->m_media);
        libvlc_media_player_play(self->m_mediaPlayer);
    }
}
void VideoWidget::releaseVlc()
{
    if (m_mediaPlayer) {
#if (LIBVLC_VERSION_MAJOR == 3)
        libvlc_media_player_stop(m_mediaPlayer);
#elif (LIBVLC_VERSION_MAJOR == 4)
    libvlc_media_player_stop_async(m_mediaPlayer);
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

void VideoWidget::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;

    QString filePath = urls.first().toLocalFile();
    if (!filePath.isEmpty())
    {
        if(filePath.endsWith(".gif"))
        {
            gif.stop();
#if (LIBVLC_VERSION_MAJOR == 3)
            libvlc_media_player_stop(m_mediaPlayer);
#elif (LIBVLC_VERSION_MAJOR == 4)
            libvlc_media_player_stop_async(m_mediaPlayer);
#endif
            gif.setFileName(filePath);
            eTime.invalidate();
            // gif.setCacheMode(QMovie::CacheAll);
            connect(&gif, &QMovie::frameChanged, this, [this](int)
            {
                QImage frame = gif.currentImage();
                processFrame(frame,gif.currentFrameNumber(),gif.frameCount());

            });
            gif.start();

        } else
        {
            gif.stop();
#if (LIBVLC_VERSION_MAJOR == 3)
            libvlc_media_player_stop(m_mediaPlayer);
#elif (LIBVLC_VERSION_MAJOR == 4)
            libvlc_media_player_stop_async(m_mediaPlayer);
#endif
            playFile(filePath);
        }
    }
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
void VideoWidget::playFile(const QString &path)
{
    if (!m_vlcInstance || !m_mediaPlayer)
    {
        qWarning() << "VLC nicht initialisiert!";
        return;
    }

    if (m_media)
    {
        libvlc_media_release(m_media);
        m_media = nullptr;
    }

    QByteArray ba = QDir::toNativeSeparators(path).toUtf8();
#if (LIBVLC_VERSION_MAJOR == 3)
    m_media = libvlc_media_new_path(m_vlcInstance, ba.constData());
#elif (LIBVLC_VERSION_MAJOR == 4)
    m_media = libvlc_media_new_path(ba.constData());
#endif
    if (!m_media)
    {
        qWarning() << "Konnte Media nicht laden:" << path;
        return;
    }

    libvlc_media_player_set_media(m_mediaPlayer, m_media);
    libvlc_media_player_play(m_mediaPlayer);
}

// ---- libVLC Callback-Implementierung ----

// Wird von VLC aufgerufen, um einen Zeiger auf den Framebuffer zu bekommen
void *VideoWidget::lockCallback(void *opaque, void **planes)
{
    VideoWidget *self = static_cast<VideoWidget*>(opaque);
    self->m_frameMutex.lock();

    if (self->m_frame.isNull()) {
        self->m_frame = QImage(self->m_videoWidth, self->m_videoHeight, QImage::Format_ARGB32);
        self->m_frame.fill(Qt::black);
    }

    *planes = self->m_frame.bits();
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

    // Hier hast du den aktuellen Frame als QImage:
    // self->m_frame

    // Für Demo einfach im UI anzeigen:
    QImage imgCopy;
    {
        QMutexLocker locker(&self->m_frameMutex);
        imgCopy = self->m_frame.copy();
    }

    // Achtung: displayCallback läuft NICHT im GUI-Thread.
    // Daher über QueuedConnection in den GUI-Thread:
    QMetaObject::invokeMethod(self->m_label, [self, imgCopy]() {
        self->m_label->setPixmap(QPixmap::fromImage(imgCopy).scaled(
            self->m_label->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
    });
}

// Wird von VLC aufgerufen, um das Videoformat festzulegen
unsigned VideoWidget::formatCallback(void **opaque, char *chroma,
                                    unsigned *width, unsigned *height,
                                    unsigned *pitches, unsigned *lines)
{
    VideoWidget *self = static_cast<VideoWidget*>(*opaque);

    // Wir wollen 32-bit RGB (RV32 = BGRA → QImage::Format_ARGB32 passt)
#if (LIBVLC_VERSION_MAJOR == 3)
    chroma[0] = 'R';
    chroma[1] = 'V';
    chroma[2] = '3';
    chroma[3] = '2';
#elif (LIBVLC_VERSION_MAJOR == 4)
    chroma[0] = 'B';
    chroma[1] = 'G';
    chroma[2] = 'R';
    chroma[3] = 'A';
#endif


    self->m_videoWidth = *width;
    self->m_videoHeight = *height;
    self->m_pitch = self->m_videoWidth * 4; // 4 Bytes pro Pixel

    *pitches = self->m_pitch;
    *lines = self->m_videoHeight;

    // Frame-Buffer anlegen
    self->m_frameMutex.lock();
    self->m_frame = QImage(self->m_videoWidth,
                           self->m_videoHeight,
                           QImage::Format_ARGB32);
    self->m_frame.fill(Qt::black);
    self->m_frameMutex.unlock();

    return 1; // OK
}

void VideoWidget::formatCleanupCallback(void *opaque)
{
    VideoWidget *self = static_cast<VideoWidget*>(opaque);
    QMutexLocker locker(&self->m_frameMutex);
    self->m_frame = QImage();
}
