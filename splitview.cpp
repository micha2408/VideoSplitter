#include "splitview.h"
#include "ui_splitview.h"
#include "VideoWidget.h"
#include "math.h"
#include <QPainter>
#include <QPixmap>
#include <Label.h>
#include <QTimer>

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
}

void SplitView::sendSize(int sp)
{
    ui->rangeSlider->blockSignals(true);
    ui->sortSlider->blockSignals(true);
    sizePic=sp;
    map.clear();
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
        if(map.size()==0) delay=0;
        delay+=ip.delay;
        map[ip.index]=ip.image;
        if(map.size()==ip.count)
        {
            delay/=map.size(); // mittlere verzögerung
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
//    QObject::disconnect(this);
//    disconnect(ui->rangeSlider,nullptr,nullptr,nullptr);
    delete ui;
}

void SplitView::on_actionget_pictures_triggered()
{
    map.clear();
    fillingMap = true;
}


void SplitView::on_actionprictures_to_grid_triggered()
{
}

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
    {
        for(int iRow=0; iRow<height; iRow+=height1)
        {
            if(nr<=last)
            {
                p.drawPixmap(iCol,iRow,map[nr]);
                p.drawRect(QRect(iCol,iRow,width1-1,height1-1));
                nr+=ui->sortSlider->value();
            }
        }
    }
    p.end();
    ui->label->setPixmap(bigMap.scaled(sizePic,sizePic));
}


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

void SplitView::updateGif()
{
    if(fillingMap) return;
    int index=ui->rangeSlider->property("index").toInt();
    ui->label->setPixmap(map[index]);
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
