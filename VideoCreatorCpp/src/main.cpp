#include <QCoreApplication>
#include <QDebug>
#include <string>
#include "model/ProjectConfig.h"
#include "model/ConfigLoader.h"
#include "engine/RenderEngine.h"
#include "ffmpeg_utils/FFmpegHeaders.h"

// 使用命名空间
using namespace VideoCreator;

class VideoCreatorDemo
{
public:
    VideoCreatorDemo() {}

    void runDemo()
    {
        // 使用Qt输出，自动处理编码
        qDebug() << "=== VideoCreatorCpp 演示程序 ===";
        qDebug() << "版本: 1.0";
        qDebug() << "基于FFmpeg的视频创建器";
        qDebug() << "==============================";

        // 初始化FFmpeg
        avformat_network_init();

        // 方法1: 从配置文件加载
        qDebug() << "\n方法1: 从配置文件加载...";
        ConfigLoader loader;
        ProjectConfig config;

#ifdef PROJECT_SOURCE_DIR
        // CMake passes the source dir path, converting backslashes to forward slashes
        std::string config_path = std::string(PROJECT_SOURCE_DIR) + "/test_config.json";
        qDebug() << "从源码目录加载配置文件:" << QString::fromStdString(config_path);
#else
        std::string config_path = "test_config.json";
        qDebug() << "从当前工作目录加载配置文件:" << QString::fromStdString(config_path);
#endif

        if (loader.loadFromFile(QString::fromStdString(config_path), config))
        {
            qDebug() << "配置文件加载成功!";
            printProjectInfo(config);

            // 创建渲染引擎
            RenderEngine engine;
            if (engine.initialize(config))
            {
                qDebug() << "\n开始视频渲染...";
                if (engine.render())
                {
                    qDebug() << "视频渲染成功!";
                    qDebug() << "输出文件:" << QString::fromStdString(config.project.output_path);
                }
                else
                {
                    qDebug() << "视频渲染失败:" << QString::fromStdString(engine.errorString());
                }
            }
            else
            {
                qDebug() << "渲染引擎初始化失败:" << QString::fromStdString(engine.errorString());
            }
        }
        else
        {
            qDebug() << "配置文件加载失败:" << loader.errorString();

            // 方法2: 手动创建配置
            qDebug() << "\n方法2: 创建演示配置...";
            createDemoConfig(config);
            printProjectInfo(config);

            // 创建渲染引擎
            RenderEngine engine;
            if (engine.initialize(config))
            {
                qDebug() << "\n开始演示视频渲染...";
                if (engine.render())
                {
                    qDebug() << "演示视频渲染成功!";
                    qDebug() << "输出文件:" << QString::fromStdString(config.project.output_path);
                }
                else
                {
                    qDebug() << "演示视频渲染失败:" << QString::fromStdString(engine.errorString());
                }
            }
            else
            {
                qDebug() << "渲染引擎初始化失败:" << QString::fromStdString(engine.errorString());
            }
        }

        qDebug() << "\n演示程序完成!";
    }

private:
    void printProjectInfo(const ProjectConfig &config)
    {
        qDebug() << "项目信息:";
        qDebug() << "  项目名称:" << QString::fromStdString(config.project.name);
        qDebug() << "  输出文件:" << QString::fromStdString(config.project.output_path);
        qDebug() << "  分辨率:" << config.project.width << "x" << config.project.height;
        qDebug() << "  帧率:" << config.project.fps;
        qDebug() << "  场景数量:" << config.scenes.size();

        for (size_t i = 0; i < config.scenes.size(); ++i)
        {
            const auto &scene = config.scenes[i];
            qDebug() << "  场景" << (i + 1) << ":" << scene.id
                     << "(" << scene.duration << "秒)";
        }
    }

    void createDemoConfig(ProjectConfig &config)
    {
        config.project.name = "演示视频项目";
        config.project.output_path = "output/demo_video.mp4";
        config.project.width = 1280;
        config.project.height = 720;
        config.project.fps = 30;

        // 创建演示场景
        SceneConfig scene1;
        scene1.id = 1;
        scene1.type = SceneType::IMAGE_SCENE;
        scene1.duration = 3.0;
        scene1.resources.image.path = "assets/demo_background.jpg";
        scene1.resources.audio.path = "";
        scene1.effects.ken_burns.enabled = false;
        scene1.effects.volume_mix.enabled = false;

        SceneConfig scene2;
        scene2.id = 2;
        scene2.type = SceneType::IMAGE_SCENE;
        scene2.duration = 5.0;
        scene2.resources.image.path = "assets/demo_content.jpg";
        scene2.resources.audio.path = "";
        scene2.effects.ken_burns.enabled = true;
        scene2.effects.ken_burns.start_scale = 1.0;
        scene2.effects.ken_burns.end_scale = 1.2;
        scene2.effects.ken_burns.start_x = 0;
        scene2.effects.ken_burns.start_y = 0;
        scene2.effects.ken_burns.end_x = 100;
        scene2.effects.ken_burns.end_y = 50;

        SceneConfig scene3;
        scene3.id = 3;
        scene3.type = SceneType::IMAGE_SCENE;
        scene3.duration = 2.0;
        scene3.resources.image.path = "assets/demo_ending.jpg";
        scene3.resources.audio.path = "";
        scene3.effects.ken_burns.enabled = false;
        scene3.effects.volume_mix.enabled = false;

        config.scenes.push_back(scene1);
        config.scenes.push_back(scene2);
        config.scenes.push_back(scene3);
    }
};

int main(int argc, char *argv[])
{
    // 创建Qt应用程序实例，自动处理编码
    QCoreApplication app(argc, argv);

    // 设置应用程序信息
    app.setApplicationName("VideoCreatorCpp");
    app.setApplicationVersion("1.0");

    VideoCreatorDemo demo;
    demo.runDemo();

    return 0;
}
