#ifndef SPLITVIEW_H
#define SPLITVIEW_H

#include <QMainWindow>
#include <QMap>
#include <QPixmap>
#include <QTimer>

class Label;

namespace Ui {
class SplitView;
}

class SplitView : public QMainWindow
{
    Q_OBJECT

public:
    explicit SplitView(Label *l);
    ~SplitView();

private:
    Ui::SplitView *ui;
    QMap<int,QPixmap> bigMap;
    QList<QPixmap> previewList;
    QTimer previewTimer;
    int delay;
    bool fillingMap;
    int sizePic;
    int speed;
    void paintGrid();
    QPixmap composeGrid(int first, int count, int step);
    struct Save
    {
        QString slName; // ".png/.txt"
        QString slScript;
    } save;

public slots:
    void sendPic(Label *l);
    void sendSize(int sizePic);

private slots:
    void on_actionget_pictures_triggered();
    void on_actionprictures_to_grid_triggered();
    void on_actionshow_gif_triggered();
    void updateGif();
    void previewGif();
    void on_sortSlider_valueChanged(int value);
    void lowerValueChanged(int value);
    void upperValueChanged(int value);
};

#endif // SPLITVIEW_H
