#include "RangeSlider.h"
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

// Pixelposition der linken Griffkante → Wert, gerundet wie in QSlider
int RangeSlider::pixelPosToRangeValue(int pos) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);

    const QRect gr = style()->subControlRect(QStyle::CC_Slider, &opt,
                                             QStyle::SC_SliderGroove, this);
    const QRect sr = style()->subControlRect(QStyle::CC_Slider, &opt,
                                             QStyle::SC_SliderHandle, this);
    const bool horizontal = orientation() == Qt::Horizontal;
    const int  length     = horizontal ? sr.width() : sr.height();
    const int  sliderMin  = horizontal ? gr.x() : gr.y();
    const int  sliderMax  = (horizontal ? gr.right() : gr.bottom()) - length + 1;

    return QStyle::sliderValueFromPosition(minimum(), maximum(), pos - sliderMin,
                                           sliderMax - sliderMin, opt.upsideDown);
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

// Klick genau auf einen Griff: Griff ziehen und seine Position melden.
// Links vom unteren Griff: unterer Griff 1 nach links, rechts vom oberen Griff:
// oberer Griff 1 nach rechts. Dazwischen rückt der näher liegende Griff um 1
// auf den Klick zu.
void RangeSlider::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton)
    {
        ev->ignore();
        return;
    }
    ev->accept();

    const QPoint pt        = ev->position().toPoint();
    const int    pos       = pick(pt);
    const QRect  lowerRect = handleRect(m_lower);
    const QRect  upperRect = handleRect(m_upper);
    const bool   onLower   = lowerRect.contains(pt);
    const bool   onUpper   = upperRect.contains(pt);
    m_activeHandle = NoHandle;

    if (onLower || onUpper)
    {
        if (onLower && onUpper)
        {
            // Griffe liegen übereinander: Richtung der ersten Bewegung entscheidet
            m_activeHandle = BothHandles;
            m_clickOffset  = pos - pick(lowerRect.topLeft());
            m_pressPos     = pos;
        }
        else
        {
            m_activeHandle = onLower ? LowerHandle : UpperHandle;
            m_clickOffset  = pos - pick((onLower ? lowerRect : upperRect).topLeft());
        }
        emit handlePressed(onLower ? m_lower : m_upper);
        return;
    }

    const int lowerStart = pick(lowerRect.topLeft());
    const int upperEnd   = pick(upperRect.bottomRight());
    if (pos < lowerStart)
    {
        setLowerValue(m_lower - 1);
    }
    else if (pos > upperEnd)
    {
        setUpperValue(m_upper + 1);
    }
    else
    {
        const int lowerCenter = pick(lowerRect.center());
        const int upperCenter = pick(upperRect.center());
        if (pos - lowerCenter <= upperCenter - pos)
            setLowerValue(m_lower + 1);
        else
            setUpperValue(m_upper - 1);
    }
}

void RangeSlider::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_activeHandle == NoHandle)
    {
        ev->ignore();
        return;
    }

    const int pos = pick(ev->position().toPoint());
    if (m_activeHandle == BothHandles)
    {
        if (pos == m_pressPos) return;
        m_activeHandle = pos < m_pressPos ? LowerHandle : UpperHandle;
    }

    const int val = pixelPosToRangeValue(pos - m_clickOffset);
    if (m_activeHandle == LowerHandle)
        setLowerValue(val);
    else
        setUpperValue(val);
}

void RangeSlider::mouseReleaseEvent(QMouseEvent* ev)
{
    m_activeHandle = NoHandle;
    ev->accept();
}
