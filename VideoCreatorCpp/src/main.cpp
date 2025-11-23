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

        if (loader.loadFromFile("config.json", config))
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
                    qDebug() << "输出文件:" << QString::fromStdString(config.output.output_path);
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
                    qDebug() << "输出文件:" << QString::fromStdString(config.output.output_path);
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
        qDebug() << "  项目名称:" << QString::fromStdString(config.project_name);
        qDebug() << "  输出文件:" << QString::fromStdString(config.output.output_path);
        qDebug() << "  分辨率:" << config.output.width << "x" << config.output.height;
        qDebug() << "  帧率:" << config.output.frame_rate;
        qDebug() << "  视频码率:" << config.output.video_bitrate << "bps";
        qDebug() << "  音频码率:" << config.output.audio_bitrate << "bps";
        qDebug() << "  场景数量:" << config.scenes.size();

        for (size_t i = 0; i < config.scenes.size(); ++i)
        {
            const auto &scene = config.scenes[i];
            qDebug() << "  场景" << (i + 1) << ":" << QString::fromStdString(scene.name)
                     << "(" << scene.duration << "秒)";
            qDebug() << "    资源数量:" << scene.resources.size();
            qDebug() << "    特效数量:" << scene.effects.size();
        }
    }

    void createDemoConfig(ProjectConfig &config)
    {
        config.project_name = "演示视频项目";
        config.output.output_path = "output/demo_video.mp4";
        config.output.width = 1280;
        config.output.height = 720;
        config.output.frame_rate = 30;
        config.output.video_bitrate = 2000000; // 2Mbps
        config.output.audio_bitrate = 128000;  // 128kbps
        config.output.video_codec = "libx264";
        config.output.audio_codec = "aac";

        // 创建演示场景
        SceneConfig scene1;
        scene1.name = "开场动画";
        scene1.duration = 3.0;

        ResourceConfig resource1;
        resource1.path = "assets/demo_background.jpg";
        resource1.type = ResourceType::IMAGE;
        resource1.start_time = 0.0;
        resource1.duration = 3.0;
        resource1.volume = 1.0;
        scene1.resources.push_back(resource1);

        EffectConfig effect1;
        effect1.type = EffectType::FADE_IN;
        effect1.start_time = 0.0;
        effect1.duration = 1.0;
        scene1.effects.push_back(effect1);

        SceneConfig scene2;
        scene2.name = "主要内容";
        scene2.duration = 5.0;

        ResourceConfig resource2;
        resource2.path = "assets/demo_content.jpg";
        resource2.type = ResourceType::IMAGE;
        resource2.start_time = 0.0;
        resource2.duration = 5.0;
        resource2.volume = 1.0;
        scene2.resources.push_back(resource2);

        EffectConfig effect2;
        effect2.type = EffectType::ZOOM;
        effect2.start_time = 1.0;
        effect2.duration = 3.0;
        effect2.params = "zoom=1.2";
        scene2.effects.push_back(effect2);

        SceneConfig scene3;
        scene3.name = "结束动画";
        scene3.duration = 2.0;

        ResourceConfig resource3;
        resource3.path = "assets/demo_ending.jpg";
        resource3.type = ResourceType::IMAGE;
        resource3.start_time = 0.0;
        resource3.duration = 2.0;
        resource3.volume = 1.0;
        scene3.resources.push_back(resource3);

        EffectConfig effect3;
        effect3.type = EffectType::FADE_OUT;
        effect3.start_time = 1.0;
        effect3.duration = 1.0;
        scene3.effects.push_back(effect3);

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
