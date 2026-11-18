#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName("MeetingController");
    QCoreApplication::setApplicationName("MeetingController");

    MainWindow window;
    window.show();

    return app.exec();
}
