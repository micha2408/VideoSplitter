#ifndef LABEL_H
#define LABEL_H

#include <QRect>
#include <QObject>
#include <QLabel>
#include <QRubberBand>

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

    QRect cropRectInImageCoords() const
    {
        return m_cropState != CropState::None ? QRect() : m_imageCropRect;
    }
    const QPixmap &scale(int &x, int &y);
    void setDefaults()
    {
        rubberBand = nullptr;
        origin=QPoint();
        m_cropState = CropState::None;
        m_imageCropRect=QRect();
        m_newSel=QRect();
        m_lastPos=QPointF();
        imagePlus = ImagePlus();
    }
protected:
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev) override;
    void paintEvent(QPaintEvent *ev) override;
private:
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
    enum class CropState { None, Preview, Dragging };
    QRubberBand *rubberBand = nullptr;
    QPoint origin;
    CropState m_cropState = CropState::None;
    QRect m_imageCropRect;
    QRect m_newSel;
    QPointF m_lastPos;
    bool m_didDrag = false;
signals:
    void sendPic(Label *l);
    void rightClicked();
    void cropChanged();
};


#endif // LABEL_H
