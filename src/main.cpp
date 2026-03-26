#include <iostream>
#include <string>
#include "model/ProjectConfig.h"
#include "model/ConfigLoader.h"
#include "engine/RenderEngine.h"
#include "ffmpeg_utils/FFmpegHeaders.h"

/**
 * @file main.cpp
 * @brief 项目示例入口，展示配置加载与渲染流程。
 */

using namespace VideoCreator;

/**
 * @brief 控制台演示器，用于演示最小可运行渲染流程。
 */
class VideoCreatorDemo
{
public:
    /**
     * @brief 运行演示流程。
     *
     * 优先从 `test_config.json` 加载配置，失败时回退到内置示例配置。
     */
    void runDemo()
    {
        std::cout << "=== VideoCreatorCpp Demo ===\n";
        std::cout << "Version: 1.0\n";
        std::cout << "============================\n";

        avformat_network_init();

        std::cout << "\nMethod 1: Load from config file...\n";
        ConfigLoader loader;
        ProjectConfig config;

#ifdef PROJECT_SOURCE_DIR
        std::string config_path = std::string(PROJECT_SOURCE_DIR) + "/test_config.json";
        std::cout << "Loading config from source dir: " << config_path << "\n";
#else
        std::string config_path = "test_config.json";
        std::cout << "Loading config from current dir: " << config_path << "\n";
#endif

        if (loader.loadFromFile(config_path, config))
        {
            std::cout << "Config loaded successfully.\n";
            printProjectInfo(config);

            RenderEngine engine;
            if (engine.initialize(config))
            {
                std::cout << "\nStart rendering...\n";
                if (engine.render())
                {
                    std::cout << "Render succeeded.\n";
                    std::cout << "Output file: " << config.project.output_path << "\n";
                }
                else
                {
                    std::cerr << "Render failed: " << engine.errorString() << "\n";
                }
            }
            else
            {
                std::cerr << "Engine initialize failed: " << engine.errorString() << "\n";
            }
        }
        else
        {
            std::cerr << "Config load failed: " << loader.errorString() << "\n";
            std::cout << "\nMethod 2: Build demo config...\n";
            createDemoConfig(config);
            printProjectInfo(config);

            RenderEngine engine;
            if (engine.initialize(config))
            {
                std::cout << "\nStart demo rendering...\n";
                if (engine.render())
                {
                    std::cout << "Demo render succeeded.\n";
                    std::cout << "Output file: " << config.project.output_path << "\n";
                }
                else
                {
                    std::cerr << "Demo render failed: " << engine.errorString() << "\n";
                }
            }
            else
            {
                std::cerr << "Engine initialize failed: " << engine.errorString() << "\n";
            }
        }

        std::cout << "\nDemo finished.\n";
    }

private:
    /**
     * @brief 打印项目摘要信息。
     */
    void printProjectInfo(const ProjectConfig &config)
    {
        std::cout << "Project info:\n";
        std::cout << "  Name: " << config.project.name << "\n";
        std::cout << "  Output: " << config.project.output_path << "\n";
        std::cout << "  Resolution: " << config.project.width << "x" << config.project.height << "\n";
        std::cout << "  FPS: " << config.project.fps << "\n";
        std::cout << "  Scene count: " << config.scenes.size() << "\n";

        for (size_t i = 0; i < config.scenes.size(); ++i)
        {
            const auto &scene = config.scenes[i];
            std::cout << "  Scene " << (i + 1) << ": " << scene.id
                      << " (" << scene.duration << " sec)\n";
        }
    }

    /**
     * @brief 构造一个内置演示配置。
     */
    void createDemoConfig(ProjectConfig &config)
    {
        config.project.name = "Demo Project";
        config.project.output_path = "output/demo_video.mp4";
        config.project.width = 1280;
        config.project.height = 720;
        config.project.fps = 30;

        SceneConfig scene1;
        scene1.id = 1;
        scene1.type = SceneType::IMAGE_SCENE;
        scene1.duration = 3.0;
        scene1.resources.image.path = "assets/demo_background.jpg";
        scene1.effects.ken_burns.enabled = false;
        scene1.effects.volume_mix.enabled = false;

        SceneConfig scene2;
        scene2.id = 2;
        scene2.type = SceneType::IMAGE_SCENE;
        scene2.duration = 5.0;
        scene2.resources.image.path = "assets/demo_content.jpg";
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
        scene3.effects.ken_burns.enabled = false;
        scene3.effects.volume_mix.enabled = false;

        config.scenes.clear();
        config.scenes.push_back(scene1);
        config.scenes.push_back(scene2);
        config.scenes.push_back(scene3);
    }
};

/**
 * @brief 程序入口。
 */
int main()
{
    VideoCreatorDemo demo;
    demo.runDemo();
    return 0;
}
