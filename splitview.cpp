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
    ui->sliderFirst->blockSignals(true);
    ui->sliderLast->blockSignals(true);
}

void SplitView::sendSize(int sp)
{
    ui->sliderFirst->blockSignals(true);
    ui->sliderLast->blockSignals(true);
    sizePic=sp;
    map.clear();
    fillingMap = true;

}

void SplitView::sendPic(Label *l)
{
    Label::ImagePlus &ip=l->imagePlus;
    if(fillingMap)
    {
        ui->sliderFirst->setRange(0,ip.count-1);
        ui->sliderLast->setRange(0,ip.count-1);
        ui->sliderFirst->setValue(ip.index);
        ui->sliderLast->setValue(ip.index);
        map[ip.index]=ip.image;
        delay[ip.index]=ip.delay;
        if(map.size()==ip.count)
        {
            fillingMap = false;
            ui->sliderFirst->setValue(0);
            ui->sliderLast->setValue(map.size()-1);
            ui->sliderFirst->blockSignals(false);
            ui->sliderLast->blockSignals(false);
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
    map.clear();
    fillingMap = true;
}


void SplitView::on_actionprictures_to_grid_triggered()
{
}

void SplitView::paintGrid()
{
    int first=ui->sliderFirst->value();
    int last=ui->sliderLast->value();
    if(first>last) qSwap(first,last);
    int width1 = map[first].width();
    int height1 = map[first].height();
    int area=width1*height1*(last-first+1);
    int width=0;
    int height=area;
    while(width<height)
    {
        width += width1;
        height = area/width;
    }

    if(width*height<area)
    {
        if((width + width1)*height < width*(height+height1))
        {
            width += width1;
        } else
        {
            height += height1;
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
                nr++;
            }
        }
    }
    p.end();
    ui->label->setPixmap(bigMap.scaled(sizePic,sizePic));
}


void SplitView::on_sliderFirst_valueChanged(int value)
{
    if(value>ui->sliderLast->value())
    {
        ui->sliderFirst->setValue(ui->sliderLast->value());
    } else
    {
        paintGrid();
    }
}


void SplitView::on_sliderLast_valueChanged(int value)
{
    if(value<ui->sliderFirst->value())
    {
        ui->sliderLast->setValue(ui->sliderFirst->value());
    } else
    {
        paintGrid();
    }
}

void SplitView::updateGif()
{
    int index=ui->label->property("index").toInt();
    ++index;

    if(index>ui->sliderLast->value()) index=ui->sliderFirst->value();
    ui->label->setProperty("index",index);
    ui->label->setPixmap(map[index]);
    QTimer::singleShot(delay[ui->sliderFirst->value()],this,&SplitView::updateGif);
}

void SplitView::on_actionshow_gif_triggered()
{
    ui->label->setPixmap(map[ui->sliderFirst->value()]);
    ui->label->setProperty("index",ui->sliderFirst->value());
    QTimer::singleShot(delay[ui->sliderFirst->value()],this,&SplitView::updateGif);

}

