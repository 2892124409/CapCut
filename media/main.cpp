#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDebug>
#include <QtQml/qqml.h>
#include "filemanager.h"
#include "mediacontroller.h"

// ==========================================
// 引入 FFmpeg 头文件
// ==========================================
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

// ==========================================
// 定义一个简单的测试函数
// ==========================================
void printVideoInfo(const char* filepath) {
    qDebug() << "\n================ FFmpeg Test Start ================";
    qDebug() << "Testing file:" << filepath;

    // 1. 创建一个上下文结构体 (用于存储文件信息)
    AVFormatContext* fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        qDebug() << "Error: Could not allocate context.";
        return;
    }

    // 2. 打开文件 (相当于给文件把脉)
    if (avformat_open_input(&fmt_ctx, filepath, nullptr, nullptr) < 0) {
        qDebug() << "Error: Could not open file. (Check your path!)";
        return;
    }

    // 3. 读取流信息 (寻找视频流和音频流)
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        qDebug() << "Error: Could not find stream info.";
        avformat_close_input(&fmt_ctx);
        return;
    }

    // 4. 打印结果
    qDebug() << "Success! FFmpeg read the file.";
    // 格式名称 (例如 mov,mp4,m4a,3gp,3g2,mj2)
    qDebug() << "Format:" << fmt_ctx->iformat->name;
    // 时长 (单位是微秒，我们转换成秒)
    qDebug() << "Duration:" << fmt_ctx->duration / AV_TIME_BASE << "seconds";
    // 比特率
    qDebug() << "Bitrate:" << fmt_ctx->bit_rate / 1000 << "kbps";

    // 5. 收尾清理
    avformat_close_input(&fmt_ctx);
    qDebug() << "================ FFmpeg Test End ==================\n";
}

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
