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
    void doDropEvent(QString path);

    // All captured frames (index → pixmap)
    QMap<int, QPixmap> m_bigMap;
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
    RangeSlider    *m_rangeSlider;
    QSlider        *m_sortSlider;
    QString chromeSettings;
    // Grid preview animation
    QList<QPixmap> m_previewList;
    QTimer         m_previewTimer;
    int            m_previewIndex = 0;
    int            m_gridCols     = 1;
    int            m_gridRows     = 1;

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

    // Frame extraction
    FrameExtractor *m_extractor = nullptr;

    // File history
    QMenu   *m_recentMenu   = nullptr;
    QMenu   *m_exportedMenu = nullptr;
    void addToHistory(const QString &path);
    void addToExported(const QString &path);
    void rebuildRecentMenu();
    void rebuildExportedMenu();

    // Grid helpers
    struct GridDims { int cols, rows; };
    GridDims findOptimalGrid(int N) const;

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
    void previewTick();
    void playTick();
    void togglePause();
    void startBgRemoval();
    void onBgFrameReady(int index, QPixmap result);
    void onBgProgress(int done, int total);
    void onBgFinished();
    void openFile();
    void saveSpriteSheet();
    void exportVideo();
    void openWithExplorer();
    void openWithXnView();
    void openWithFastStone();
    void onFramesExtracted(QMap<int, QPixmap> frames, int delayMs);

    void openWithViewer(const QString &settingsKey, const QString &title);
};

#endif // MAINWINDOW_H
