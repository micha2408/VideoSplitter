#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QVector>
#include <QTimer>
#include <QList>
#include <QLabel>
#include <QSlider>
#include <QElapsedTimer>
#include <QActionGroup>
#include <QPixmap>
#include <QFileDialog>
#include <QSettings>
#include <QCheckBox>

class Label;
class RangeSlider;
class QStackedWidget;
class ComfyBgRemover;
class VideoExporter;
class FrameExtractor;
class QPlainTextEdit;
class QSplitter;

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
    // All captured frames (contiguous, index 0..size()-1)
    QVector<QPixmap> m_bigMap;
    QVector<QPixmap> m_bigMapBackup;
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
    QCheckBox      *m_revers;
    bool           m_isReversed = false;
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

    // Protokollfenster (unten, per Trenner in der Höhe verstellbar)
    QSplitter      *m_splitter  = nullptr;
    QPlainTextEdit *m_logView   = nullptr;
    QAction        *m_actLog    = nullptr;
    int             m_logHeight = 0;   // gemerkte Höhe; nur zur Laufzeit, nicht persistent
    void logMessage(const QString &text);
    void setLogVisible(bool on);
    int  logLinesHeight(int lines) const;

    // File history
    QMenu   *m_recentMenu   = nullptr;   // Zuletzt geöffnet
    QMenu   *m_exportMenu   = nullptr;   // Zuletzt exportiert (nur Videos, kein PNG)
    void addToHistory(const QString &path, const QPixmap &preview = QPixmap());
    void rebuildRecentMenu();
    void addToExportHistory(const QString &path);
    void rebuildExportMenu();
    // Dateiname-Vorgabe des Export-Dialogs auf das neu geladene Video zurücksetzen
    void resetExportNameForVideo(const QString &path);

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
    // Seitenverhaeltnis des Einzelbild-Exports (Reihenfolge = Reihenfolge im Dialog)
    enum Ratio { RatioOriginal = 0, Ratio4_3, Ratio3_4, Ratio1_1 };

    // ─── Sprite-Sheet-Import ────────────────────────────────────────────────
    // Beim Export bekommt ein Sprite-Sheet den Zusatz
    // "(cols_rows_frames_fps_breite_hoehe)"; Breite und Hoehe sind die Masse des
    // Ursprungsvideos. Beim Oeffnen wird dieser Zusatz wieder ausgewertet, um aus
    // dem Sheet die Einzelframes zurueckzugewinnen. Aeltere Dateien tragen nur
    // die ersten vier Parameter — dann fragt resolveSpriteFrameSize() nach.
    struct SpriteInfo
    {
        int cols = 0, rows = 0, frames = 0, fps = 0;
        int srcW = 0, srcH = 0;   // 0 = nicht im Dateinamen enthalten
        bool hasSourceSize() const { return srcW > 0 && srcH > 0; }
    };
    // Vorgabe fuer die lange Seite, wenn die Originalgroesse geschaetzt wird
    static constexpr int kSpriteImportLongSide = 1024;
    // Zusatz im Dateinamen erkennen und auslesen (vier oder sechs Parameter)
    static bool  parseSpriteFileName(const QString &path, SpriteInfo &info);
    // Lage der Zelle mit dem angegebenen Index im Sheet
    static QRect spriteCellRect(const QPixmap &sheet, const SpriteInfo &info, int index);
    // Startwert des Nachfrage-Dialogs aus dem Seitenverhaeltnis der Zelle
    static QSize spriteCellSuggestion(const QSize &cell);
    // Sheet anhand der SpriteInfo wieder in Einzelframes zerlegen
    static QVector<QPixmap> sliceSpriteSheet(const QPixmap &sheet, const SpriteInfo &info,
                                             const QSize &frameSize);
    // Originalgroesse aus dem Dateinamen lesen oder erfragen (leer = abgebrochen)
    QSize resolveSpriteFrameSize(const QPixmap &sheet, const SpriteInfo &info);

    // Grid helpers
    struct GridDims { int cols, rows; };
    GridDims findOptimalGrid(int N) const;
    GridDims m_grid = {1,1};
    QSize    m_cellSize;   // Zellgröße des zuletzt gebauten Grids (Einzelbild: proportional)
    struct Sliders { int first, last, step; };
    const Sliders getSliderValues() const;
    QPixmap composeGrid(int first, int count, int step);
    // Anzahl der Frames, die der aktuelle Bereich exportiert (1 = Einzelbild)
    int exportFrameCount() const;
    // Vorgabename im Subordner-Modus: "video"/"picture" + 8-stelliger Hash
    QString exportDefaultName() const;
    // Tatsächlicher Sprite-Sheet-Dateiname inkl. Parameterliste
    // (cols_rows_frames_fps_breite_hoehe), beim Einzelbild stattdessen mit
    // _BreitexHoehe der tatsächlichen Ausgabe
    QString spriteFileName(const QString &pngPath, Ratio ratio = RatioOriginal) const;
    // Parameterliste bzw. Größenangabe am Ende eines Namens entfernen, damit ein
    // erneuter Export die alte Liste ersetzt statt eine zweite anzuhängen
    static QString stripExportSuffix(const QString &stem);
    // Liste der bereits existierenden Zieldateien der gewählten Formate
    QStringList existingExportTargets(const QString &dir, const QString &base,
                                      bool webm, bool mp4, bool gif, bool png,
                                      Ratio ratio = RatioOriginal) const;
    // Sollgröße eines Seitenverhältnisses bei der aktuellen Auflösung
    QSize ratioTargetSize(Ratio ratio) const;
    // Größe des zu exportierenden Einzelbildes (Original = proportional skaliert)
    QSize exportImageSize(Ratio ratio) const;
    // Maße eines Quellframes nach Anwendung des Crop-Rechtecks — die Groesse,
    // die beim Export als Parameter 5 und 6 im Dateinamen landet
    QSize sourceFrameSize() const;
    // Mindestens 10 Pixel mit Alpha < 32?
    static bool hasTransparency(const QImage &img);
    // Proportional in die Zielbox skalieren und den Rest auffüllen
    QPixmap padToRatio(const QPixmap &src, Ratio ratio) const;
    void paintGrid();
    void startPlayback();
    void updateTitle();
    void showFrame(int index);
    void reduceMinMax();

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
    void saveSpriteSheet(const QString &path, Ratio ratio = RatioOriginal);
    void saveVideo(const QString &path);
    void exportDialog();
    void runExport(const QString &dir, const QString &baseName,
                   bool webm, bool mp4, bool gif, bool png,
                   Ratio ratio = RatioOriginal);
    void openWithExplorer();
    void openWithUrl();
    void openWithXnView();
    void openWithFastStone();
    void onFramesExtracted(QVector<QPixmap> frames, int delayMs);
    void onPreviewReady(QString sourcePath, QPixmap preview);

    void openWithViewer(const QString &settingsKey, const QString &title);
};

#endif // MAINWINDOW_H
