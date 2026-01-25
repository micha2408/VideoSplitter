#include "RangeSlider.h"
#include "qdebug.h"
#include <QStyle>
#include <QStyleOptionSlider>
#include <QMouseEvent>
#include <QPainter>

RangeSlider::RangeSlider(QWidget* parent, Qt::Orientation ori)
    : QSlider(ori, parent)
{
    setMinimum(0);
    setMaximum(100);
    setTickPosition(QSlider::NoTicks);
}

void RangeSlider::setLowerValue(int v) {
    v = qBound(minimum(), v, m_upper);
    if (v != m_lower) {
        m_lower = v;
        emit lowerValueChanged(v);
        emit rangeChanged(m_lower, m_upper);
        update();
    }
}

void RangeSlider::setUpperValue(int v) {
    v = qBound(m_lower, v, maximum());
    if (v != m_upper) {
        m_upper = v;
        emit upperValueChanged(v);
        emit rangeChanged(m_lower, m_upper);
        update();
    }
}

int RangeSlider::pick(const QPoint& pt) const {
    return orientation() == Qt::Horizontal ? pt.x() : pt.y();
}

int RangeSlider::pixelPosToRangeValue(int pos) const {
    QStyleOptionSlider opt;
    initStyleOption(&opt);

    QRect gr = style()->subControlRect(QStyle::CC_Slider, &opt,
                                       QStyle::SC_SliderGroove, this);
    QRect sr = style()->subControlRect(QStyle::CC_Slider, &opt,
                                       QStyle::SC_SliderHandle, this);

    int sliderMin = gr.left();
    int sliderMax = gr.right() - sr.width() + 1;

    double normalized = double(pos - sliderMin) / double(sliderMax - sliderMin);
    return minimum() + normalized * (maximum() - minimum());
}

QRect RangeSlider::handleRect(int value) const {
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    opt.sliderPosition = value;
    opt.sliderValue = value;
    return style()->subControlRect(QStyle::CC_Slider, &opt,
                                   QStyle::SC_SliderHandle, this);
}

void RangeSlider::paintEvent(QPaintEvent*) {
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    QPainter p(this);

    // Groove
    opt.subControls = QStyle::SC_SliderGroove;
    style()->drawComplexControl(QStyle::CC_Slider, &opt, &p, this);

    // Lower handle
    opt.subControls = QStyle::SC_SliderHandle;
    opt.sliderPosition = m_lower;
    style()->drawComplexControl(QStyle::CC_Slider, &opt, &p, this);

    // Upper handle
    opt.sliderPosition = m_upper;
    style()->drawComplexControl(QStyle::CC_Slider, &opt, &p, this);
}

void RangeSlider::mousePressEvent(QMouseEvent* ev) {
    int pos = pick(ev->pos());

    QRect lowerRect = handleRect(m_lower);
    QRect upperRect = handleRect(m_upper);
    int middle = (lowerRect.right() + upperRect.left()) / 2;
    if(ev->pos().x() < lowerRect.left())
    {
        if(lowerValue()>0) setLowerValue(lowerValue()-1);
    } else
    {
        if(ev->pos().x() > lowerRect.right())
        {
            if(ev->pos().x() < middle)
            {
                if((lowerValue()+1)<upperValue()) setLowerValue(lowerValue()+1);
            } else
            {
                if(ev->pos().x() < upperRect.left())
                {
                    if((upperValue()-1)>lowerValue()) setUpperValue(upperValue()-1);
                } else
                {
                    if(ev->pos().x() < upperRect.right())
                    {
                        if(upperValue()<maximum()) setUpperValue(upperValue()-1);
                    }
                }
            }
        }
    }
    if(ev->pos().x() > upperRect.right())
    {
        if(upperValue()+1<maximum()) setUpperValue(upperValue()+1);
    }

    if (lowerRect.contains(ev->pos()))
        m_activeHandle = LowerHandle;
    else if (upperRect.contains(ev->pos()))
        m_activeHandle = UpperHandle;
    else
        m_activeHandle = NoHandle;
    qDebug() << m_activeHandle;
    QSlider::mousePressEvent(ev);
}

void RangeSlider::mouseMoveEvent(QMouseEvent* ev) {
    if (m_activeHandle == NoHandle) {
        QSlider::mouseMoveEvent(ev);
        return;
    }

    int pos = pick(ev->pos());
    int val = pixelPosToRangeValue(pos);

    if (m_activeHandle == LowerHandle)
        setLowerValue(val);
    else if (m_activeHandle == UpperHandle)
        setUpperValue(val);
}

void RangeSlider::mouseReleaseEvent(QMouseEvent* ev) {
    m_activeHandle = NoHandle;
    QSlider::mouseReleaseEvent(ev);
}
