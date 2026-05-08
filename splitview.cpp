
#include "splitview.h"
#include "ui_splitview.h"
#include "VideoWidget.h"
#include "math.h"
#include <QPainter>
#include <QPixmap>
#include "Label.h"
#include <QTimer>
#include <QDateTime>
#include <QRegularExpression>
#include <QFileDialog>

SplitView::SplitView(Label *l)
    : QMainWindow(nullptr)
    , ui(new Ui::SplitView)
    , fillingMap(false)
    , sizePic()
    , speed()
{
    ui->setupUi(this);
    connect(l,&Label::sendPic,this,&SplitView::sendPic,Qt::QueuedConnection);
    connect(l,&Label::sendSize,this,&SplitView::sendSize,Qt::QueuedConnection);
    connect(ui->rangeSlider,&RangeSlider::lowerValueChanged,this,&SplitView::lowerValueChanged);
    connect(ui->rangeSlider,&RangeSlider::upperValueChanged,this,&SplitView::upperValueChanged);
    ui->rangeSlider->blockSignals(true);
    connect(&previewTimer,&QTimer::timeout,this,&SplitView::previewGif);

}

void SplitView::sendSize(int sp)
{
    ui->rangeSlider->blockSignals(true);
    ui->sortSlider->blockSignals(true);
    sizePic=sp;
    bigMap.clear();
    fillingMap = true;

}

void SplitView::sendPic(Label *l)
{
    Label::ImagePlus &ip=l->imagePlus;
    if(fillingMap)
    {
        ui->sortSlider->setRange(1,ip.count/2);
        ui->sortSlider->setValue(1);
        ui->rangeSlider->setRange(0,ip.count-1);
        ui->rangeSlider->setLowerValue(ip.index);
        ui->rangeSlider->setUpperValue(ip.count-1);
        if(bigMap.size()==0) delay=0;
        delay+=ip.delay;
        bigMap[ip.index]=ip.image;
        qDebug() << "map index " << ip.index << " size " << bigMap.size() << " count " << ip.count;
        if(bigMap.size()==ip.count)
        {
            delay/=bigMap.size(); // mittlere verzögerung
            ui->rangeSlider->setLowerValue(0);
            ui->rangeSlider->blockSignals(false);
            ui->sortSlider->blockSignals(false);
            fillingMap = false;
            paintGrid();
        }
    }
}

SplitView::~SplitView()
{
    delete ui;
}

void SplitView::on_actionget_pictures_triggered()
{
    bigMap.clear();
    fillingMap = true;
}


void SplitView::on_actionprictures_to_grid_triggered()
{
    ui->label->pixmap(Qt::ReturnByValue).save(save.slName+".png");
}

QPixmap SplitView::composeGrid(int first, int count, int step)
{
    qDebug() << "compose";
    previewTimer.stop();
    previewList.clear();
    count /= step;

    int cols = std::ceil(std::sqrt(count));
    int rows = std::ceil(double(count) / cols);

    int cellW = sizePic / cols;
    int cellH = sizePic / rows;

    QPixmap result(sizePic, sizePic);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (int i = 0; i < count; ++i) {
        int row = i / cols;
        int col = i % cols;

        QRect cell(col * cellW, row * cellH, cellW, cellH);

        previewList << bigMap[first+i*step].scaled(
            cell.size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
            );

        // zentrieren in der Zelle
        QPoint pos(
            cell.x() + (cellW - previewList.last().width()) / 2,
            cell.y() + (cellH - previewList.last().height()) / 2
            );

        p.drawPixmap(pos, previewList.last());
    }
    QSettings settings;
    save.slName = settings.value("video").toString().remove(QRegularExpression("\\..*$"))
                + QString("_%1x%2x%3x%4.png")
                      .arg(rows)
                      .arg(cols)
                      .arg(count)
                        .arg(1000/(delay*step));

    previewTimer.start(delay*step);

    return result;
}

#define NEW
#ifdef NEW
void SplitView::paintGrid()
{
    int first=ui->rangeSlider->lowerValue();
    int last=ui->rangeSlider->upperValue();
    int step=ui->sortSlider->value();
    ui->label->setPixmap(composeGrid(first,last-first+1,step));
}
#else
void SplitView::paintGrid()
{
    int first=ui->rangeSlider->lowerValue();
    ui->labelLower->setText(QString::number(first));
    int last=ui->rangeSlider->upperValue();
    ui->labelUpper->setText(QString::number(last));
    int maxSort=(last-first+1)/2;
    ui->sortSlider->setRange(1,maxSort);
    int indexSort = ui->sortSlider->value();
    if(indexSort>maxSort)
    {
        indexSort = maxSort;
        ui->sortSlider->setValue(indexSort);
    }
    ui->labelSort->setText(QString("%1/%2").arg(indexSort).arg(maxSort));
    int width1 = map[first].width();
    int height1 = map[first].height();
    int area=0;
    for(int i=first; i<(last-first+1); i+=ui->sortSlider->value())
    {
        area += width1*height1;
    }
    int width=width1;
    int height=height1;
    while(width*height<area)
    {
        if(width<height)
        {
            width+=width1;
            qDebug() << "w:" << width << height << width * height << area;
        } else
        {
            height+=height1;
            qDebug() << "h:" << width << height << width * height << area;
        }
    }

    setWindowTitle(QString("Grid w=%1*h=%2 count=%3 (%4..%5)").arg(width/width1).arg(height/height1).arg(last-first+1).arg(first).arg(last));
    QPixmap bigMap(width,height);

    QPainter p(&bigMap);
    QPen pen(Qt::red);
    p.setPen(pen);
    p.fillRect(bigMap.rect(),Qt::transparent);
    int nr=first;
    for(int iCol=0; iCol<width; iCol+=width1)
    {   // row und col irgnedwie tauschen :)
        for(int iRow=0; iRow<height; iRow+=height1)
        {
            if(nr<=last)
            {
                p.drawPixmap(iCol,iRow,map[nr]);
                nr+=ui->sortSlider->value();
            }
        }
    }
    p.end();
    ui->label->setPixmap(bigMap.scaled(sizePic,sizePic));

    previewMap.clear();
    float gifWidth=float(sizePic) * float(width1) / float(width);
    float gifHeight=float(sizePic) * float(height1) / float(height);
    nr=first;
    for(int iCol=0; iCol<sizePic; iCol+=int(gifWidth))
    {
        for(int iRow=0; iRow<sizePic; iRow+=int(gifHeight))
        {
            if(nr<=last)
            {
                previewMap[nr] = ui->label->pixmap(Qt::ReturnByValue).copy(iCol,iRow,gifWidth,gifHeight);
                p.drawPixmap(iCol,iRow,map[nr]);
                nr+=ui->sortSlider->value();
            }
        }
    }
    previewIndex=0;
    previewGif();


}

#endif

void SplitView::lowerValueChanged(int value)
{
    if(value>=ui->rangeSlider->upperValue())
    {
        ui->rangeSlider->setLowerValue(ui->rangeSlider->value()-1);
    } else
    {
        paintGrid();
    }
}


void SplitView::upperValueChanged(int value)
{
    if(value<=ui->rangeSlider->lowerValue())
    {
        ui->rangeSlider->setUpperValue(ui->rangeSlider->value()+1);
    } else
    {
        paintGrid();
    }
}

void SplitView::previewGif()
{
    if(previewList.size())
    {
        QPixmap pm=previewList.takeFirst();
        ui->labelPreview->setPixmap(pm);
        previewList << pm;
    }
}

void SplitView::updateGif()
{
    if(fillingMap) return;
    int index=ui->rangeSlider->property("index").toInt();
    ui->label->setPixmap(bigMap[index]);
    QTimer::singleShot(delay*ui->sortSlider->value(),this,&SplitView::updateGif);
    index += ui->sortSlider->value();
    if(index>ui->rangeSlider->upperValue())
    {   // wieder vorn anfangen
        index=ui->rangeSlider->lowerValue();
    }
    ui->rangeSlider->setProperty("index",index); // index sichern
}

void SplitView::on_actionshow_gif_triggered()
{
    // starten mit dem ersten value
    ui->rangeSlider->setProperty("index",ui->rangeSlider->lowerValue()); // index sichern
    updateGif();
}


void SplitView::on_sortSlider_valueChanged(int value)
{
    ui->labelSort->setText(QString("%1/%2").arg(value).arg(ui->sortSlider->maximum()));
    paintGrid();

}
