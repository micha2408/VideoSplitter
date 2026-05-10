#include <QApplication>
#include "VideoWidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("michaelSW");
    QCoreApplication::setApplicationName("VideoConverter");
    VideoWidget w;
    w.show();
    return app.exec();
}
