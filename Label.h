#ifndef LABEL_H
#define LABEL_H

#include <QRect>
#include <QSize>
#include <QObject>
#include <QLabel>
#include <QRubberBand>
#include <QPolygonF>
#include <QHash>
#include <QPixmap>
#include <functional>

class QPainter;

class Label : public QLabel
{
    Q_OBJECT
public:
    Label(const QString &text, QWidget *parent)
        : QLabel(text,parent)
    {
        setFocusPolicy(Qt::StrongFocus);  // oder Qt::ClickFocus
    }
    void setImage(const QPixmap &p, int i, int c, int d)
    {
        imagePlus=ImagePlus(p,i,c,d);
        emit sendPic(this);
    }
    int currentIndex() const { return imagePlus.index; }

    // Seitenverhaeltnis, auf das das Bild erweitert wird (leer = Grundstellung)
    QSize padRatio() const { return m_padRatio; }

    // Fuellung des hinzugefuegten Randes
    enum class PadFill { Auto, Extend, Transparent };
    PadFill padFill() const { return m_padFill; }

    QRect cropRectInImageCoords() const { return m_imageCropRect; }
    const QPixmap &scale(int &x, int &y);
    void setDefaults()
    {
        endErase();
        rubberBand = nullptr;
        origin=QPoint();
        m_cropPending=false;
        m_imageCropRect=QRect();
        m_cropBeforeClick=QRect();
        m_padBeforeClick=QSize();
        m_newSel=QRect();
        m_padRatio=QSize();
        m_padFill=PadFill::Auto;
        imagePlus = ImagePlus();
    }
    bool isErasing() const { return m_erasing; }

    // Werkzeug im Radiermodus: Pixel entfernen oder aus dem Original zurückholen
    enum class EraseTool { Erase, Restore };
    EraseTool eraseTool() const { return m_eraseTool; }
    // R bzw. W: Modus mit diesem Werkzeug starten oder im Modus umschalten
    void startEraseMode(EraseTool tool);
    // A: Seitenverhältnis-Dialog an der Mausposition
    void openRatioDialog();

    // Ein Strich in Koordinaten des angezeigten (aufbereiteten) Bildes
    struct EraseStroke
    {
        QPolygonF points;
        qreal     radius  = 0;
        bool      restore = false;   // true = Wiederherstellen statt Radieren
    };
    using EraseMask = QList<EraseStroke>;
    // Striche der Reihe nach zeichnen: Radieren macht durchsichtig,
    // Wiederherstellen übernimmt die Pixel von original, dessen linke obere
    // Ecke in Strich-Koordinaten bei originalPos liegt
    static void paintStrokes(QPainter &p, const EraseMask &mask,
                             const QPixmap &original, const QPointF &originalPos);

    // Liefert zum Quellindex das Original und die Lage des aufbereiteten
    // Bildes darin (für die Vorschau des Wiederherstellens)
    using OriginalProvider = std::function<QPixmap(int index, QPoint *preparedOrigin)>;
    void setOriginalProvider(OriginalProvider provider) { m_originalProvider = std::move(provider); }

    // Pause: neue Striche gelten nur für das angezeigte Bild,
    // Wiedergabe: für alle Bilder des Bereichs
    void setErasePerFrame(bool on) { m_erasePerFrame = on; }
protected:
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev) override;
    void wheelEvent(QWheelEvent *ev) override;
    void paintEvent(QPaintEvent *ev) override;
private:
    void showRatioDialog(const QPoint &globalPos);
    void restoreBeforeClick();
    // Radierer: R oder W startet, Esc fragt nach, wie es weitergeht
    void beginErase(EraseTool tool);
    // Neuen Strich mit dem aktuellen Werkzeug an der Radierer-Position beginnen
    void startStroke();
    void endErase();
    // Masken an VideoWidget übergeben und den Modus verlassen
    void applyAndEndErase();
    // Esc im Radiermodus: Popup-Menü, wie es weitergeht
    void showEraseMenu(const QPoint &globalPos);
    // Maske, die für das angezeigte Bild gilt (alle Bilder + nur dieses Bild)
    EraseMask currentMask() const;
    // Mausposition im Label halten und als Radierer-Mitte übernehmen
    QPointF clampToWidget(const QPointF &pos);
    // Lage des eingepassten Bildes im Label
    QRect imageRect() const;
    QPointF toImage(const QPointF &widgetPos) const;
    struct ImagePlus
    {
        ImagePlus(){}
        ImagePlus(const QPixmap &p, int i, int c, int d)
            : image(p)
            , index(i)
            , count(c)
            , delay(d)
        {}
        QPixmap image;
        int index;
        int count;
        int delay;
    } imagePlus;
    QRubberBand *rubberBand = nullptr;
    QPoint origin;
    // Linke Taste gedrückt, Zuschnitt beginnt erst nach kurzer Mausbewegung
    bool   m_cropPending = false;
    // Zuschnitt aufziehen: alten Stand merken und das unveränderte Bild zeigen
    void beginCropDrag();
    QRect m_imageCropRect;
    QRect m_cropBeforeClick;
    QSize m_padBeforeClick;
    QRect m_newSel;
    QSize m_padRatio;
    PadFill m_padFill = PadFill::Auto;

    // Radierer; Striche und Radius in Bildkoordinaten
    bool                   m_erasing       = false;
    bool                   m_erasePerFrame = false;
    EraseTool              m_eraseTool     = EraseTool::Erase;
    OriginalProvider       m_originalProvider;
    // Ziel des laufenden Strichs: kNoStroke, kGlobalStroke oder Quellindex
    static constexpr int   kNoStroke       = -2;
    static constexpr int   kGlobalStroke   = -1;
    int                    m_strokeTarget  = kNoStroke;
    QPointF                m_eraserPos;                 // Mitte im Label
    qreal                  m_eraseRadius   = 0;
    EraseMask              m_globalMask;                // gilt für alle Bilder des Bereichs
    QHash<int, EraseMask>  m_frameMasks;                // Quellindex → nur dieses Bild
signals:
    void sendPic(Label *l);
    void rightClicked();
    void cropChanged();
    // Radiermodus betreten/verlassen oder Werkzeug gewechselt
    void eraseModeChanged();
    // Radierung ausführen: global auf alle Bilder des Bereichs, perFrame je Quellindex
    void eraseApplied(const Label::EraseMask &global, const QHash<int, Label::EraseMask> &perFrame);
};


#endif // LABEL_H
