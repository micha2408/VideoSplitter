#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMutex>
#include <QImage>
#include <QMovie>

#include <vlc/vlc.h>
#include <QRubberBand>

#include <QLabel>
#include <QList>

#include <QElapsedTimer>
#include <QSettings>

class Label;
class SplitView;
class VideoWidget : public QMainWindow
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;



private:

    void doDropEvent(const QString &path);
    QPixmap currentpixMap;
    QElapsedTimer eTime;
    // libVLC
    libvlc_instance_t *m_vlcInstance = nullptr;
    libvlc_media_player_t *m_mediaPlayer = nullptr;
    libvlc_media_t *m_media = nullptr;
    // Framebuffer
    QMutex m_frameMutex;
    int m_videoWidth = 1280; // Default (wird in Format-Callback gesetzt)
    int m_videoHeight = 720;
    int m_pitch = 0;         // Bytes pro Zeile
    struct Frame
    {
        Frame()
            : current(-1)
            , count(-1)
            , image(640,480,QImage::Format_ARGB32)
        {
            image.fill(Qt::black);
        }
        void newImage(int w, int h)
        {
            image=QImage(w,h,QImage::Format_ARGB32);
        }
        int current;
        int count;
        QImage image;          // letzter Frame als QImage
    } frame;
    int m_currentFrame;
    QMovie gif;
    Label *m_label;         // Anzeige des letzten Frames
    SplitView *splitView;
    // interne Hilfsfunktionen
    void initVlc();
    void releaseVlc();
    bool playFile(const QString &path);
    void processFrame(const QImage &img, int index, int count);

    // statische Callback-Funktionen für libVLC
    static void *lockCallback(void *opaque, void **planes);
    static void unlockCallback(void *opaque, void *picture, void *const *planes);
    static void displayCallback(void *opaque, void *picture);
    static unsigned formatCallback(void **opaque, char *chroma,
                                   unsigned *width, unsigned *height,
                                   unsigned *pitches, unsigned *lines);
    static void formatCleanupCallback(void *opaque);
//    static void eventCallback(const libvlc_event_t* event, void* data);

public slots:
    void menu_split_1024(bool);
    void menu_split_2048(bool);
    void menu_uncrop_pic(bool);
    void menu_pause_video(bool checked);
};

#endif // MAINWINDOW_H
