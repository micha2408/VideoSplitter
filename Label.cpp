#include "Label.h"
#include <QMouseEvent>
#include <QResizeEvent>
#include <QDebug>
#include <QPainter>

//#include <QTimer>

void Label::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton) return;
    origin = ev->pos();
    if (ev->modifiers() & Qt::ControlModifier) return;
    if (!rubberBand)
        rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    rubberBand->setGeometry(QRect(origin, QSize()));
    rubberBand->show();
}

void Label::mouseMoveEvent(QMouseEvent *ev)
{
    if (ev->modifiers() & Qt::ControlModifier)
    {
        setAdjustion(origin - ev->pos());
        return;
    }
    if (rubberBand)
        rubberBand->setGeometry(QRect(origin, ev->pos()).normalized());
}

void Label::mouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::RightButton)
    {
        emit rightClicked();
        return;
    }
    if (ev->modifiers() & Qt::ControlModifier)
    {
        // STRG ist gedrückt
        qDebug() << origin << ev->pos();
        return;
    }
    if (!rubberBand) return;
    rubberBand->hide();
    QRect newSel = QRect(origin, ev->pos()).normalized();
    if(newSel.width()<8)
    {
        resetSelection();
        return;
    }
    if(newSel.height()<8)
    {
        resetSelection();
        return;
    }
    QSize s;
    scale(s.rwidth(), s.rheight());
    const QRectF sel = QRectF(newSel).adjusted(-s.width(), -s.height(), -s.width(), -s.height());
    saList << SelAdj(newSel.adjusted(-s.width(), -s.height(), -s.width(), -s.height()));

    // Store crop in image pixel coordinates (immune to resize)
    if (!imagePlus.image.isNull()) {
        const QSize scaledSz = imagePlus.image.size().scaled(size(), Qt::KeepAspectRatio);
        if (!scaledSz.isEmpty()) {
            const double sw = static_cast<double>(imagePlus.image.width())  / scaledSz.width();
            const double sh = static_cast<double>(imagePlus.image.height()) / scaledSz.height();
            m_imageCropRect = QRect(
                static_cast<int>(sel.x()      * sw),
                static_cast<int>(sel.y()      * sh),
                static_cast<int>(sel.width()  * sw),
                static_cast<int>(sel.height() * sh)
            ).intersected(imagePlus.image.rect());
        }
    }
    update();
}

const QPixmap &Label::scale(int &x, int &y)
{
    static QPixmap scaled;
    scaled = imagePlus.image;
    if (saList.isEmpty()) {
        saList << SelAdj(scaled.rect());
    } else {
        // Keep saList[0] in sync with the actual image dimensions
        saList[0].selection = scaled.rect();
    }
    for (auto sa : saList) {
        scaled = scaled.copy(sa.selection.toRect());
        scaled = scaled.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    x = (width() - scaled.width()) / 2;
    y = (height() - scaled.height()) / 2;
    return scaled;
}

QRectF Label::normalizeRect(const QRectF &small, const QRectF &big)
{
    return QRectF(
        small.x()      / big.width(),
        small.y()      / big.height(),
        small.width()  / big.width(),
        small.height() / big.height()
        );
}

QRectF Label::scaleRect(const QRectF &norm, const QRectF &big)
{
    return QRectF(
        norm.x()      * big.width(),
        norm.y()      * big.height(),
        norm.width()  * big.width(),
        norm.height() * big.height()
        );
}

void Label::resizeEvent(QResizeEvent *ev)
{
    QRectF bigRect(0, 0, ev->oldSize().width(), ev->oldSize().height());
    QRectF newBigRect(0, 0, ev->size().width(), ev->size().height());

    for(auto &sa : saList)
    {
        QRectF smallRect=sa.selection; // soll proportional mitskalieren
        QRectF smallNorm = normalizeRect(smallRect, bigRect);
        QRectF newSmall = scaleRect(smallNorm, newBigRect);
        sa.selection=newSmall;
    }
}

void Label::paintEvent(QPaintEvent *e)
{
    if(imagePlus.image.isNull())
    {
        QLabel::paintEvent(e);
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    int x=0;
    int y=0;
    const QPixmap &scaled=scale(x,y);
    p.fillRect(rect(),QColor(Qt::blue));
    p.drawPixmap(x, y, scaled);
    p.setPen(QPen(Qt::red, 2));
    p.drawRect(x, y, scaled.width(), scaled.height());


}
