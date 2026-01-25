#ifndef LABEL_H
#define LABEL_H

#include <QRect>
#include <QObject>
#include <QLabel>
#include <splitview.h>
#include <QRubberBand>

class Label : public QLabel
{
    Q_OBJECT
public:
    Label(const QString &text, QWidget *parent) : QLabel(text,parent)
    {}
    const QRect &getSelection(){ return saList.last().selection; }
    const QPoint &getAdjustion(){ return saList.last().adjustion; }
    void setSelection(const QRect r){ saList.last().selection=r; }
    void setAdjustion(const QPoint a){ saList.last().adjustion=a; }
    void resetSelection(){ if(saList.size()) saList.removeLast();}
    void setImage(const QPixmap &p, int i, int c, int d) { imagePlus=ImagePlus(p,i,c,d); }
protected:
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void paintEvent(QPaintEvent *ev) override;
    friend SplitView;
private:
    QRubberBand *rubberBand = nullptr;
    QPoint origin;
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
    struct SelAdj
    {
        SelAdj() : selection(), adjustion() {}
        QRect selection;
        QPoint adjustion;
    };
    QList<SelAdj> saList;
signals:
    void sendPic(Label *l);
    void sendSize(int sizePic);
};


#endif // LABEL_H
