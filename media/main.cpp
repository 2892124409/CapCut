#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // 强制 Qt Quick 使用 OpenGL 图形后端，确保后续手写 GL 渲染可用
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

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
