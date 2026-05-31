#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QTimer>
#include <QList>
#include <QLabel>
#include <QSlider>
#include <QElapsedTimer>
#include <QActionGroup>
#include <QPixmap>
#include <QFileDialog>
#include <QSettings>

class Label;
class RangeSlider;
class QStackedWidget;
class ComfyBgRemover;
class VideoExporter;
class FrameExtractor;

class VideoWidget : public QMainWindow
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override = default;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    bool copyDirectoryRecursive(const QString &sourceDir,
                                const QString &targetDir,
                                bool overwrite = false);
    void doDropEvent(const QString &path);
    // All captured frames (index → pixmap)
    QMap<int, QPixmap> m_bigMap;
    QMap<int, QPixmap> m_bigMapBackup;
    int  m_delay      = 0;
    bool m_fillingMap = false;

    // UI
    QStackedWidget *m_stack;
    Label          *m_label;         // stack page 0: video view
    QLabel         *m_gridLabel;     // sprite sheet (inside grid page)
    QLabel         *m_previewLabel;  // animated preview (inside grid page)
    QLabel         *m_labelLower;
    QLabel         *m_labelUpper;
    QLabel         *m_labelSort;
    QLabel         *m_labelSpeed;
    RangeSlider    *m_rangeSlider;
    QSlider        *m_sortSlider;
    QSlider        *m_speedSlider;
    // Grid preview animation
    QList<QPixmap> m_previewList;
    QTimer         m_previewTimer;
    int            m_previewIndex = 0;

    // Playback
    QTimer       m_playTimer;
    int          m_playIndex   = 0;
    bool         m_paused      = false;
    enum ActiveHandle { NoHandle, LowerHandle, UpperHandle };
    ActiveHandle m_lastHandle  = NoHandle;

    // State
    int  m_resolution  = 1024;
    bool m_showingGrid = false;

    // Background removal
    ComfyBgRemover *m_bgRemover    = nullptr;
    QAction        *m_actBgRemove  = nullptr;
    QActionGroup   *m_modelGroup   = nullptr;
    QActionGroup   *m_nodeGroup    = nullptr;

    // Frame extraction
    FrameExtractor *m_extractor = nullptr;

    // File history
    QMenu   *m_recentMenu   = nullptr;
    void addToHistory(const QString &path, const QPixmap &preview = QPixmap());
    void rebuildRecentMenu();

    // Preview cache (key = filePart of "path,url")
    QMap<QString, QPixmap> m_previewCache;
    QStringList            m_previewQueue;
    FrameExtractor        *m_previewExtractor = nullptr;
    static constexpr int   kPreviewMaxSize    = 64;
    void enqueuePreviewExtraction(const QString &path);

    const QString lastFile() const
    {
        return QSettings().value("lastFile").toString();
    }
    const QString loadingFile() const
    {
        return QSettings().value("loadingFile").toString();
    }
    void setLastFile(QString path) const
    {
        QSettings().setValue("lastFile", path);
    }
    void setLoadingFile(QString path) const
    {
        QSettings().setValue("loadingFile", path);
    }
    // Grid helpers
    struct GridDims { int cols, rows; };
    GridDims findOptimalGrid(int N) const;
    GridDims m_grid = {1,1};
    struct Sliders { int first, last, step; };
    const Sliders getSliderValues() const;
    QPixmap composeGrid(int first, int count, int step);
    void paintGrid();
    void startPlayback();
    void updateTitle();
    void showFrame(int index);

private slots:
    void toggleView();
    void lowerValueChanged(int value);
    void upperValueChanged(int value);
    void sortValueChanged(int value);
    void speedValueChanged(int value);
    void previewTick();
    void playTick();
    void togglePause();
    void startBgRemoval();
    void onBgFrameReady(int index, QPixmap result);
    void onBgProgress(int done, int total);
    void onBgFinished();
    void openFile();
    void saveSpriteSheet(const QString &path);
    void saveVideo(const QString &path);
    void exportVideo();
    void exportAll();
    void openWithExplorer();
    void openWithUrl();
    void openWithXnView();
    void openWithFastStone();
    void onFramesExtracted(QMap<int, QPixmap> frames, int delayMs);
    void onPreviewReady(QString sourcePath, QPixmap preview);

    void openWithViewer(const QString &settingsKey, const QString &title);
};

#endif // MAINWINDOW_H
