#include <QApplication>
#include "VideoWidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    VideoWidget w;
    w.show();
    return app.exec();
}
