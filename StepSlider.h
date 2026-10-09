#pragma once
#include <QSlider>

// QSlider, bei dem ein Klick neben den Griff den Wert nur um einen Schritt
// in Richtung Mauszeiger verschiebt. Der Griff selbst bleibt ziehbar.
class StepSlider : public QSlider
{
    Q_OBJECT

public:
    explicit StepSlider(Qt::Orientation orientation, QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* ev) override;
};
