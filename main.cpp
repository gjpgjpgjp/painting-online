#include "mainwindow.h"
#include "RoomServer.h"
#include <QApplication>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char *argv[])
{
    // 检查是否是服务端模式
    bool isServerMode = (argc > 1 && QString(argv[1]) == "-server");

    if (isServerMode) {
        // 服务端：无GUI
        QCoreApplication app(argc, argv);

        quint16 port = 12345;
        if (argc > 2) port = QString(argv[2]).toUShort();

        RoomServer server(port, &app);

        if (!server.isListening()) {
            qCritical() << "Failed to start server on port" << port;
            return 1;
        }

        qInfo() << "Server running on port" << port;
        return app.exec();
    }

    // 客户端：原有GUI模式
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}