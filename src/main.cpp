#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName("MeetingController");
    QCoreApplication::setApplicationName("MeetingController");
    QCoreApplication::setApplicationVersion("0.4.0");
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));

    MainWindow window;
    window.show();

    return app.exec();
}
