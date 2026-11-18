#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName("MeetingController");
    QCoreApplication::setApplicationName("MeetingController");
    QCoreApplication::setApplicationVersion("0.4.0");

    MainWindow window;
    window.show();

    return app.exec();
}
