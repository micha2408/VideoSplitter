#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMutex>
#include <QImage>
#include <QMovie>
#include <QMap>
#include <QTimer>
#include <QList>
#include <QStack>
#include <QLabel>
#include <QSlider>
#include <QElapsedTimer>
#include <QActionGroup>

#include <vlc/vlc.h>

class Label;
class RangeSlider;
class QStackedWidget;
class ComfyBgRemover;
class VideoExporter;

class VideoWidget : public QMainWindow
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void doDropEvent(const QString &path);

    // libVLC
    libvlc_instance_t     *m_vlcInstance  = nullptr;
    libvlc_media_player_t *m_mediaPlayer  = nullptr;
    libvlc_media_t        *m_media        = nullptr;

    // VLC decode buffer
    QMutex m_frameMutex;
    int m_videoWidth  = 1280;
    int m_videoHeight = 720;
    int m_pitch       = 0;
    struct Frame
    {
        Frame() : current(0), count(-1), image(640, 480, QImage::Format_ARGB32)
        { image.fill(Qt::black); }
        void newImage(int w, int h) { image = QImage(w, h, QImage::Format_ARGB32); }
        int current;
        int count;
        QImage image;
    } frame;

    // Captured frames
    QMap<int, QPixmap> m_bigMap;
    int  m_delay      = 0;
    bool m_fillingMap = false;

    // Undo stack (applyCrop + BiRefNet schieben je einen Zustand drauf)
    struct UndoState
    {
        QMap<int, QPixmap> bigMap;
        int delay = 0;
        int lower = 0;
        int upper = 0;
        int step  = 1;
    };
    QStack<UndoState> m_undoStack;
    QAction          *m_actUndo = nullptr;

    // GIF
    QMovie gif;
    QElapsedTimer eTime;

    // UI
    QStackedWidget *m_stack;
    Label          *m_label;         // stack page 0: live video/GIF
    QLabel         *m_gridLabel;     // sprite sheet (inside grid page)
    QLabel         *m_previewLabel;  // animated preview (inside grid page)
    QLabel         *m_labelLower;
    QLabel         *m_labelUpper;
    QLabel         *m_labelSort;
    RangeSlider    *m_rangeSlider;
    QSlider        *m_sortSlider;

    // Preview animation (grid view)
    QList<QPixmap> m_previewList;
    QTimer         m_previewTimer;
    int            m_previewIndex = 0;
    int            m_gridCols     = 1;
    int            m_gridRows     = 1;

    // Playback (video view, cycles through m_bigMap selection)
    QTimer m_playTimer;
    int    m_playIndex   = 0;
    bool   m_paused      = false;
    enum   ActiveHandle  { NoHandle, LowerHandle, UpperHandle };
    ActiveHandle m_lastHandle = NoHandle;

    // State
    int  m_resolution  = 1024;
    bool m_showingGrid = false;

    // Background removal
    ComfyBgRemover *m_bgRemover    = nullptr;
    QAction        *m_actBgRemove  = nullptr;
    QActionGroup   *m_modelGroup   = nullptr;

    // Grid layout helper
    struct GridDims { int cols, rows; };
    GridDims findOptimalGrid(int N, double cropAspect) const;
    double   getCropAspect() const;

    // Helpers
    void initVlc();
    void releaseVlc();
    bool playFile(const QString &path);
    void processFrame(const QImage &img, int index, int count);
    QPixmap composeGrid(int first, int count, int step);
    void paintGrid();
    void startPlayback();
    void updateTitle();
    void pushUndo();
    void showFrame(int index);

    // VLC callbacks
    static void    *lockCallback(void *opaque, void **planes);
    static void     unlockCallback(void *opaque, void *picture, void *const *planes);
    static void     displayCallback(void *opaque, void *picture);
    static unsigned formatCallback(void **opaque, char *chroma,
                                   unsigned *width, unsigned *height,
                                   unsigned *pitches, unsigned *lines);
    static void     formatCleanupCallback(void *opaque);

private slots:
    void toggleView();
    void lowerValueChanged(int value);
    void upperValueChanged(int value);
    void sortValueChanged(int value);
    void previewTick();
    void playTick();
    void togglePause();
    void startBgRemoval();
    void onBgFrameReady(int index, QPixmap result);
    void onBgProgress(int done, int total);
    void onBgFinished();
    void saveCurrentFrame();
    void saveSpriteSheet();
    void applyCrop();
    void undoCrop();
    void exportVideo();
};

#endif // MAINWINDOW_H
