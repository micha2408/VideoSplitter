#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QHash>
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
    // Auswahl je Frame (Klick aufs Bild im Frame Grabber); abgewählte Frames werden bei
    // Vorschau, Wiedergabe und Export übersprungen. Gleiche Länge wie m_bigMap.
    QVector<bool> m_frameEnabled;
    bool frameEnabled(int index) const;
    // PingPong-Modus: hinter die Vorwärts-Frames werden dieselben Frames in
    // umgekehrter Reihenfolge gehängt. Diese Kopien tragen die Schlüssel
    // m_bigMap.size() + Quellindex und haben eine eigene Auswahl, die beim
    // Ausschalten erhalten bleibt (leer = für dieses Video noch nie aktiviert).
    bool          m_pingPong = false;
    QVector<bool> m_pingEnabled;
    // Schlüssel (Original oder PingPong-Kopie) → Index in m_bigMap
    int  sourceIndex(int key) const;
    void setPingPong(bool on);
    // Transparenz-Prüfung je Frame, gemerkt über QPixmap::cacheKey()
    mutable QHash<qint64, bool> m_alphaCache;
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
    // Grid-Vorschau: aufbereitete Frames des angezeigten Grids (Schlüssel → Bild)
    QHash<int, QPixmap> m_previewFrames;

    // Playback
    QTimer       m_playTimer;
    int          m_playIndex   = 0;    // aktuelle Bildposition (Schlüssel, ggf. PingPong-Kopie)
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
    GridDims m_grid = {1,1};   // Raster des Export-Sheets (nur ausgewählte Frames)
    QSize    m_cellSize;       // Zellgröße des Export-Sheets (Einzelbild: proportional)
    struct Sliders { int first, last, step; };
    const Sliders getSliderValues() const;
    // Alle Frames im Sliderbereich mit Schrittweite – unabhängig von der Auswahl
    QList<int> rangeKeys() const;
    // Davon nur die ausgewählten Frames – genau diese werden exportiert
    QList<int> selectedKeys() const;
    // Zellgröße eines Sheets mit diesen Frames im Raster g
    QSize gridCellSize(const QList<int> &keys, const GridDims &g) const;
    // Sheet aus den angegebenen Frames; dimDisabled graut abgewählte Frames aus.
    // prepared erhält auf Wunsch die aufbereiteten Frames in derselben Reihenfolge.
    QPixmap composeGrid(const QList<int> &keys, const GridDims &g, bool dimDisabled,
                        QList<QPixmap> *prepared = nullptr) const;
    // Angezeigtes Grid im Frame Grabber: alle Frames des Bereichs, ein Klick auf
    // ein Einzelbild schaltet es an bzw. ab
    QPixmap          m_gridPixmap;
    GridDims         m_displayGrid = {1,1};
    QList<int>       m_displayKeys;
    QRect            m_gridDrawRect;   // Lage des skalierten Grids im Label
    QPixmap          m_gridScaled;     // skaliertes Grid ohne Rahmen
    void fitGridPixmap();
    // Rahmen um den aktuellen Frame auf das skalierte Grid zeichnen
    void drawGridMarker();
    // Vorschau rechts neben dem Grid auf den aktuellen Frame setzen
    void updatePreview(int key);
    // Frame unter der Mausposition im Grid-Label, -1 = keiner
    int  gridKeyAt(const QPoint &pos) const;
    void setFrameEnabled(int index, bool on);
    // Anzahl der Frames, die der aktuelle Bereich exportiert (1 = Einzelbild)
    int exportFrameCount() const;
    // Vorgabename im Subordner-Modus: "video"/"picture" + 8-stelliger Hash
    QString exportDefaultName() const;
    // Tatsächlicher Sprite-Sheet-Dateiname inkl. Parameterliste
    // (cols_rows_frames_fps_breite_hoehe), beim Einzelbild stattdessen mit
    // _BreitexHoehe der tatsächlichen Ausgabe
    QString spriteFileName(const QString &pngPath) const;
    // Parameterliste bzw. Größenangabe am Ende eines Namens entfernen, damit ein
    // erneuter Export die alte Liste ersetzt statt eine zweite anzuhängen
    static QString stripExportSuffix(const QString &stem);
    // Liste der bereits existierenden Zieldateien der gewählten Formate
    QStringList existingExportTargets(const QString &dir, const QString &base,
                                      bool webm, bool mp4, bool gif, bool png) const;
    // Größe des zu exportierenden Einzelbildes (proportional skaliert)
    QSize exportImageSize() const;
    // Maße eines Quellframes nach Anwendung des Crop-Rechtecks — die Groesse,
    // die beim Export als Parameter 5 und 6 im Dateinamen landet
    QSize sourceFrameSize() const;
    // Mindestens 10 Pixel mit Alpha < 32?
    static bool hasTransparency(const QImage &img);
    // Bild zentriert in eine Box legen und den Rand auffüllen
    static QImage padCentered(const QImage &fitted, const QSize &target,
                              bool transparentFill);
    // Ausschnitt ohne zu skalieren auf ein Seitenverhältnis erweitern (Label-
    // Auswahl); Pixel des Originalbildes außerhalb des Ausschnitts gehen vor Rand
    QPixmap expandToRatio(const QPixmap &src, const QRect &crop, const QSize &ratio,
                          bool transparentFill) const;
    // Enthält der Quellframe transparente Pixel? (Ergebnis wird gemerkt)
    bool frameHasTransparency(int index) const;
    // Quellframe mit Zuschnitt und Seitenverhältnis-Erweiterung
    QPixmap preparedFrame(int index) const;
    // Maße, die preparedFrame() liefern würde – ohne das Bild zu bauen
    QSize preparedFrameSize(int index) const;
    void paintGrid();
    void startPlayback();
    void updateTitle();
    // Aktuellen Frame anzeigen (Video: Bild, Grid: Vorschau und Rahmen)
    void displayCurrent();
    void restartPlayTimer();
    // Nächster ausgewählter Frame in Richtung dir (±1), mit Umlauf; -1 = keiner
    int  nextActiveKey(int from, int dir) const;
    // Ziel der Pfeiltasten hoch (dir = -1) / runter (+1) im Grid
    int  rowTargetKey(int dir) const;
    // Pfeiltasten in der Pause; true = Taste verbraucht
    bool handleNavigationKey(int key);
    void keepPlayIndexInRange();
    // Vorschau des Bildes an einem Griff des Bereichsreglers (nur in der Pause)
    int  m_sliderPreview = -1;   // angezeigter Vorschau-Frame, -1 = keiner
    void showSliderPreview(int index);
    void endSliderPreview();
    void onRangeChanged();
    void reduceMinMax();

private slots:
    void toggleView();
    void lowerValueChanged(int value);
    void upperValueChanged(int value);
    void sortValueChanged(int value);
    void speedValueChanged(int value);
    void playTick();
    void togglePause();
    void startBgRemoval();
    void onBgFrameReady(int index, QPixmap result);
    void onBgProgress(int done, int total);
    void onBgFinished();
    void openFile();
    void saveSpriteSheet(const QString &path);
    void saveVideo(const QString &path);
    void exportDialog();
    void runExport(const QString &dir, const QString &baseName,
                   bool webm, bool mp4, bool gif, bool png);
    void openWithExplorer();
    void openWithUrl();
    void openWithXnView();
    void openWithFastStone();
    void onFramesExtracted(QVector<QPixmap> frames, int delayMs);
    void onPreviewReady(QString sourcePath, QPixmap preview);

    void openWithViewer(const QString &settingsKey, const QString &title);
};

#endif // MAINWINDOW_H
