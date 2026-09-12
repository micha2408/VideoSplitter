#pragma once

#include <QDialog>
#include <QPixmap>
#include <QSize>
#include <QWidget>

class QComboBox;
class QLabel;
class QRadioButton;
class QSlider;
class QSpinBox;

// Vorschau eines einzelnen Sprite-Sheet-Frames. Der Frame wird im aktuell
// eingestellten Seitenverhaeltnis gezeichnet; an der kurzen Seite laesst sich
// der Rahmen mit der Maus ziehen, um dieses Verhaeltnis zu aendern.
class SpriteFramePreview : public QWidget
{
    Q_OBJECT

public:
    explicit SpriteFramePreview(QWidget *parent = nullptr);

    void setCell(const QPixmap &cell);
    void setFrameSize(const QSize &size);

signals:
    // Neue Laenge der kurzen Seite, waehrend am Rahmen gezogen wird
    void shortSideDragged(int shortSide);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    static constexpr int kMargin      = 14;   // Rand um den Rahmen
    static constexpr int kGrabTolerance = 7;  // Greifzone der Ziehkante

    bool  isLandscape() const;
    int   longExtent() const;                 // gezeichnete Laenge der langen Seite
    QRect frameRect() const;                  // gezeichnetes Rechteck im Widget
    bool  overHandle(const QPoint &pos) const;
    void  applyDrag(const QPoint &pos);

    QPixmap m_cell;
    QSize   m_size     = QSize(1024, 576);
    bool    m_dragging = false;
};

// Nachfrage-Dialog fuer Sprite-Sheets ohne Groessenangabe im Dateinamen
// (Altbestand mit nur vier Parametern). Die lange Seite wird aus festen
// Vorgaben gewaehlt, die kurze Seite ist frei von 1 bis zur langen Seite.
class SpriteImportDialog : public QDialog
{
    Q_OBJECT

public:
    SpriteImportDialog(const QPixmap &cell, const QSize &suggestion,
                       int frameCount, QWidget *parent = nullptr);

    QSize frameSize() const;

    // Feste Vorgaben fuer die lange Seite
    static const QList<int> &longSideChoices();

private:
    void rebuildShortRange();
    void refresh();                 // Vorschau, Schieber und Ergebniszeile nachziehen
    void setShortSide(int value);

    SpriteFramePreview *m_preview     = nullptr;
    QRadioButton       *m_rbLandscape = nullptr;
    QRadioButton       *m_rbPortrait  = nullptr;
    QComboBox          *m_longBox     = nullptr;
    QSlider            *m_shortSlider = nullptr;
    QSpinBox           *m_shortSpin   = nullptr;
    QLabel             *m_result      = nullptr;

    bool m_landscape = true;
    int  m_longSide  = 1024;
    int  m_shortSide = 576;
    bool m_updating  = false;   // Schutz gegen Signal-Rueckkopplung
};
