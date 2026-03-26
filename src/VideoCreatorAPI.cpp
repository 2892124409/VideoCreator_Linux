#include "VideoCreatorAPI.h"

#include <mutex>

#include "model/ConfigLoader.h"
#include "engine/RenderEngine.h"
#include "ffmpeg_utils/FFmpegHeaders.h"

/**
 * @file VideoCreatorAPI.cpp
 * @brief 实现对外渲染 API。
 */

namespace VideoCreator
{
    namespace
    {
        /**
         * @brief 确保 FFmpeg 网络模块只初始化一次。
         */
        bool ensureFFmpegInitialized(std::string *error)
        {
            static std::once_flag initFlag;
            static int initResult = 0;
            static std::string initError;

            std::call_once(initFlag, [&]() {
                initResult = avformat_network_init();
                if (initResult < 0)
                {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                    av_strerror(initResult, errbuf, sizeof(errbuf));
                    initError = std::string("FFmpeg network init failed: ") + errbuf;
                }
            });

            if (initResult < 0)
            {
                if (error)
                {
                    *error = initError;
                }
                return false;
            }
            return true;
        }

        /**
         * @brief 使用已解析配置执行渲染。
         */
        bool renderWithConfig(const ProjectConfig &config, std::string *error)
        {
            RenderEngine engine;
            if (!engine.initialize(config))
            {
                if (error)
                {
                    *error = engine.errorString();
                }
                return false;
            }
            if (!engine.render())
            {
                if (error)
                {
                    *error = engine.errorString();
                }
                return false;
            }
            return true;
        }
    } // namespace

    /**
     * @brief 从 JSON 文件渲染视频。
     */
    bool RenderFromJson(const std::string &config_path, std::string *error)
    {
        if (!ensureFFmpegInitialized(error))
        {
            return false;
        }

        ConfigLoader loader;
        ProjectConfig config;
        if (!loader.loadFromFile(config_path, config))
        {
            if (error)
            {
                *error = loader.errorString();
            }
            return false;
        }

        return renderWithConfig(config, error);
    }

    /**
     * @brief 从 JSON 字符串渲染视频。
     */
    bool RenderFromJsonString(const std::string &json_string, std::string *error)
    {
        if (!ensureFFmpegInitialized(error))
        {
            return false;
        }

        ConfigLoader loader;
        ProjectConfig config;
        if (!loader.loadFromString(json_string, config))
        {
            if (error)
            {
                *error = loader.errorString();
            }
            return false;
        }

        return renderWithConfig(config, error);
    }
} // namespace VideoCreator
