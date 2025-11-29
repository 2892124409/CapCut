#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/qqml.h>
#include "filemanager.h"
#include "mediacontroller.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // 注册FileManager类型到QML
    qmlRegisterType<FileManager>("media", 1, 0, "FileManager");
    
    
    // 注册MediaController类型到QML
    qmlRegisterType<MediaController>("media", 1, 0, "MediaController");

    QQmlApplicationEngine engine;
    const QUrl url("qrc:/qt/qml/media/Main.qml");
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
                         if (!obj && url == objUrl)
                             QCoreApplication::exit(-1);
                     }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
