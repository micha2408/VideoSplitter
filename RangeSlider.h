#pragma once
#include <QSlider>

class RangeSlider : public QSlider {
    Q_OBJECT

public:
    explicit RangeSlider(QWidget* parent = nullptr, Qt::Orientation orientation = Qt::Horizontal);

    int lowerValue() const { return m_lower; }
    int upperValue() const { return m_upper; }

public slots:
    void setLowerValue(int v);
    void setUpperValue(int v);

signals:
    void lowerValueChanged(int);
    void upperValueChanged(int);
    void rangeChanged(int lower, int upper);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;

private:
    enum HandleType { NoHandle, LowerHandle, UpperHandle };
    HandleType m_activeHandle = NoHandle;

    int m_lower = 0;
    int m_upper = 100;

    int pick(const QPoint& pt) const;
    int pixelPosToRangeValue(int pos) const;
    QRect handleRect(int value) const;
};
