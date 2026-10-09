#include "StepSlider.h"
#include <QStyle>
#include <QStyleOptionSlider>
#include <QMouseEvent>

StepSlider::StepSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent)
{
}

void StepSlider::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton)
    {
        QSlider::mousePressEvent(ev);
        return;
    }

    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt,
                                                 QStyle::SC_SliderHandle, this);
    const QPoint pt = ev->position().toPoint();

    // Griff getroffen: normales Ziehen
    if (handle.contains(pt))
    {
        QSlider::mousePressEvent(ev);
        return;
    }

    ev->accept();
    const bool horizontal = orientation() == Qt::Horizontal;
    const int  pos        = horizontal ? pt.x() : pt.y();
    const int  center     = horizontal ? handle.center().x() : handle.center().y();
    // Bildschirmrichtung → Wertrichtung (vertikal und upsideDown kehren um)
    bool towardsMax = pos > center;
    if (opt.upsideDown)
        towardsMax = !towardsMax;

    triggerAction(towardsMax ? SliderSingleStepAdd : SliderSingleStepSub);
}
