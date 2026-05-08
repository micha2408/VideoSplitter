#include <QApplication>
#include "VideoWidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("MichaelsSW");
    QCoreApplication::setApplicationName("VideoGrabber");
    VideoWidget w;
    w.show();
    return app.exec();
}
