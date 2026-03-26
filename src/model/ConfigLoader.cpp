#include "ConfigLoader.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

/**
 * @file ConfigLoader.cpp
 * @brief 实现 JSON 配置解析与媒体时长探测逻辑。
 */

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace VideoCreator
{
    namespace
    {
        /**
         * @brief 生成小写副本，便于大小写无关比较。
         */
        std::string toLowerCopy(const std::string &value)
        {
            std::string lowered = value;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return lowered;
        }

        /**
         * @brief 判断文件是否可读。
         */
        bool isReadableFile(const std::string &path)
        {
            std::ifstream file(path.c_str(), std::ios::binary);
            return file.good();
        }

        /**
         * @brief 将 FFmpeg 错误码转换为可读字符串。
         */
        std::string formatFFmpegError(int ret)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            return std::string(errbuf);
        }
    } // namespace

    /**
     * @brief 从 JSON 文件加载配置。
     */
    bool ConfigLoader::loadFromFile(const std::string &filePath, ProjectConfig &config)
    {
        std::ifstream file(filePath.c_str(), std::ios::binary);
        if (!file)
        {
            m_errorString = "无法打开配置文件: " + filePath;
            return false;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        return loadFromString(buffer.str(), config);
    }

    /**
     * @brief 从 JSON 文本加载配置并填充 ProjectConfig。
     *
     * 处理流程：
     * 1. 清空错误状态和时长缓存；
     * 2. 解析根 JSON；
     * 3. 顺序解析 project/scenes/global_effects；
     * 4. 场景解析阶段补齐默认时长策略。
     */
    bool ConfigLoader::loadFromString(const std::string &jsonString, ProjectConfig &config)
    {
        m_errorString.clear();
        m_audioDurationCache.clear();
        m_videoDurationCache.clear();

        Json root;
        try
        {
            root = Json::parse(jsonString);
        }
        catch (const std::exception &ex)
        {
            m_errorString = std::string("JSON解析错误: ") + ex.what();
            return false;
        }

        if (!root.is_object())
        {
            m_errorString = "JSON根元素不是对象";
            return false;
        }

        if (root.contains("project") && root["project"].is_object())
        {
            if (!parseProjectConfig(root["project"], config.project))
            {
                return false;
            }
        }

        if (root.contains("scenes") && root["scenes"].is_array())
        {
            // 重新编号场景顺序，确保缺省场景也有稳定 ID。
            config.scenes.clear();
            int sceneId = 1;
            for (const auto &sceneValue : root["scenes"])
            {
                if (!sceneValue.is_object())
                {
                    continue;
                }

                SceneConfig scene;
                scene.id = sceneId;
                if (!parseSceneConfig(sceneValue, scene))
                {
                    return false;
                }
                config.scenes.push_back(scene);
                sceneId++;
            }
        }

        if (root.contains("global_effects") && root["global_effects"].is_object())
        {
            if (!parseGlobalEffectsConfig(root["global_effects"], config.global_effects))
            {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief 解析项目级字段。
     */
    bool ConfigLoader::parseProjectConfig(const Json &json, ProjectInfoConfig &project)
    {
        if (json.contains("name") && json["name"].is_string())
        {
            project.name = json["name"].get<std::string>();
        }

        if (json.contains("output_path") && json["output_path"].is_string())
        {
            project.output_path = json["output_path"].get<std::string>();
        }

        if (json.contains("width") && json["width"].is_number())
        {
            project.width = json["width"].get<int>();
        }

        if (json.contains("height") && json["height"].is_number())
        {
            project.height = json["height"].get<int>();
        }

        if (json.contains("fps") && json["fps"].is_number())
        {
            project.fps = json["fps"].get<int>();
        }

        if (json.contains("background_color") && json["background_color"].is_string())
        {
            project.background_color = json["background_color"].get<std::string>();
        }

        return true;
    }

    /**
     * @brief 解析单场景并计算最终 duration。
     *
     * 优先级规则：
     * 1. JSON 显式给定 duration 时直接采用；
     * 2. image_scene 优先按音频时长驱动；
     * 3. video_scene 优先按视频时长，其次音频时长；
     * 4. 无法探测时统一回退到 5 秒。
     */
    bool ConfigLoader::parseSceneConfig(const Json &json, SceneConfig &scene)
    {
        if (json.contains("id") && json["id"].is_number())
        {
            scene.id = json["id"].get<int>();
        }

        if (json.contains("type") && json["type"].is_string())
        {
            scene.type = stringToSceneType(json["type"].get<std::string>());
        }

        if (json.contains("resources") && json["resources"].is_object())
        {
            if (!parseResourcesConfig(json["resources"], scene.resources))
            {
                return false;
            }
        }

        if (json.contains("effects") && json["effects"].is_object())
        {
            if (!parseEffectsConfig(json["effects"], scene.effects))
            {
                return false;
            }
        }

        if (json.contains("transition_type") && json["transition_type"].is_string())
        {
            scene.transition_type = stringToTransitionType(json["transition_type"].get<std::string>());
        }

        if (json.contains("from_scene") && json["from_scene"].is_number())
        {
            scene.from_scene = json["from_scene"].get<int>();
        }

        if (json.contains("to_scene") && json["to_scene"].is_number())
        {
            scene.to_scene = json["to_scene"].get<int>();
        }

        if (json.contains("duration") && json["duration"].is_number())
        {
            // 显式时长拥有最高优先级，不再进行自动推断。
            scene.duration = json["duration"].get<double>();
            return true;
        }

        // 汇总主音频与附加音轨时长，取最长轨道驱动场景时长。
        double audioDrivenDuration = -1.0;
        bool hasAudioResource = false;
        auto updateAudioDuration = [&](const std::string &path) {
            if (path.empty())
            {
                return;
            }
            hasAudioResource = true;
            const double duration = getAudioDuration(path);
            if (duration > audioDrivenDuration)
            {
                audioDrivenDuration = duration;
            }
        };

        updateAudioDuration(scene.resources.audio.path);
        for (const auto &layerConfig : scene.resources.audio_layers)
        {
            updateAudioDuration(layerConfig.path);
        }

        if (scene.type == SceneType::IMAGE_SCENE && audioDrivenDuration > 0)
        {
            scene.duration = audioDrivenDuration;
            std::cerr << "[ConfigLoader] Scene duration synced to audio length: " << audioDrivenDuration << " seconds\n";
        }
        else if (scene.type == SceneType::VIDEO_SCENE && !scene.resources.video.path.empty())
        {
            const double videoDuration = getVideoDuration(scene.resources.video.path);
            if (videoDuration > 0)
            {
                scene.duration = videoDuration;
                std::cerr << "[ConfigLoader] Scene duration synced to video length: " << videoDuration << " seconds\n";
            }
            else if (audioDrivenDuration > 0)
            {
                scene.duration = audioDrivenDuration;
                std::cerr << "[ConfigLoader] Scene duration uses audio length for video scene: " << audioDrivenDuration << " seconds\n";
            }
            else
            {
                scene.duration = 5.0;
                std::cerr << "[ConfigLoader] Failed to get video duration, fallback to 5 seconds.\n";
            }
        }
        else if (scene.type == SceneType::IMAGE_SCENE)
        {
            scene.duration = 5.0;
            if (hasAudioResource)
            {
                std::cerr << "[ConfigLoader] Failed to get audio duration, fallback to 5 seconds.\n";
            }
            else
            {
                std::cerr << "[ConfigLoader] Scene has no audio, fallback to 5 seconds.\n";
            }
        }
        else if (scene.type == SceneType::VIDEO_SCENE)
        {
            if (audioDrivenDuration > 0)
            {
                scene.duration = audioDrivenDuration;
                std::cerr << "[ConfigLoader] Scene duration uses audio length for video scene: " << audioDrivenDuration << " seconds\n";
            }
            else
            {
                scene.duration = 5.0;
                std::cerr << "[ConfigLoader] Video scene missing resources, fallback to 5 seconds.\n";
            }
        }

        return true;
    }

    /**
     * @brief 解析 resources 节点。
     */
    bool ConfigLoader::parseResourcesConfig(const Json &json, ResourcesConfig &resources)
    {
        if (json.contains("image") && json["image"].is_object())
        {
            if (!parseImageConfig(json["image"], resources.image))
            {
                return false;
            }
        }

        if (json.contains("video") && json["video"].is_object())
        {
            if (!parseVideoConfig(json["video"], resources.video))
            {
                return false;
            }
        }

        if (json.contains("audio") && json["audio"].is_object())
        {
            if (!parseAudioConfig(json["audio"], resources.audio))
            {
                return false;
            }
        }

        if (json.contains("audio_layers") && json["audio_layers"].is_array())
        {
            resources.audio_layers.clear();
            for (const auto &audioValue : json["audio_layers"])
            {
                if (!audioValue.is_object())
                {
                    continue;
                }
                AudioConfig layerConfig;
                if (parseAudioConfig(audioValue, layerConfig))
                {
                    resources.audio_layers.push_back(layerConfig);
                }
            }
        }

        return true;
    }

    /**
     * @brief 解析 image 节点。
     */
    bool ConfigLoader::parseImageConfig(const Json &json, ImageConfig &image)
    {
        if (json.contains("path") && json["path"].is_string())
        {
            image.path = json["path"].get<std::string>();
        }

        if (json.contains("position") && json["position"].is_object())
        {
            const Json &position = json["position"];
            if (position.contains("x") && position["x"].is_number())
            {
                image.x = position["x"].get<int>();
            }
            if (position.contains("y") && position["y"].is_number())
            {
                image.y = position["y"].get<int>();
            }
        }

        if (json.contains("scale") && json["scale"].is_number())
        {
            image.scale = json["scale"].get<double>();
        }

        if (json.contains("rotation") && json["rotation"].is_number())
        {
            image.rotation = json["rotation"].get<double>();
        }

        return true;
    }

    /**
     * @brief 解析 audio 节点。
     */
    bool ConfigLoader::parseAudioConfig(const Json &json, AudioConfig &audio)
    {
        if (json.contains("path") && json["path"].is_string())
        {
            audio.path = json["path"].get<std::string>();
        }

        if (json.contains("volume") && json["volume"].is_number())
        {
            audio.volume = json["volume"].get<double>();
        }

        if (json.contains("start_offset") && json["start_offset"].is_number())
        {
            audio.start_offset = json["start_offset"].get<double>();
        }

        return true;
    }

    /**
     * @brief 解析 video 节点。
     */
    bool ConfigLoader::parseVideoConfig(const Json &json, VideoConfig &video)
    {
        if (json.contains("path") && json["path"].is_string())
        {
            video.path = json["path"].get<std::string>();
        }

        if (json.contains("trim_start") && json["trim_start"].is_number())
        {
            video.trim_start = json["trim_start"].get<double>();
        }

        if (json.contains("trim_end") && json["trim_end"].is_number())
        {
            video.trim_end = json["trim_end"].get<double>();
        }

        if (json.contains("use_audio") && json["use_audio"].is_boolean())
        {
            video.use_audio = json["use_audio"].get<bool>();
        }

        return true;
    }

    /**
     * @brief 解析 effects 节点。
     */
    bool ConfigLoader::parseEffectsConfig(const Json &json, EffectsConfig &effects)
    {
        if (json.contains("ken_burns") && json["ken_burns"].is_object())
        {
            if (!parseKenBurnsEffect(json["ken_burns"], effects.ken_burns))
            {
                return false;
            }
        }

        if (json.contains("volume_mix") && json["volume_mix"].is_object())
        {
            if (!parseVolumeMixEffect(json["volume_mix"], effects.volume_mix))
            {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief 解析 ken_burns 节点。
     */
    bool ConfigLoader::parseKenBurnsEffect(const Json &json, KenBurnsEffect &effect)
    {
        if (json.contains("enabled") && json["enabled"].is_boolean())
        {
            effect.enabled = json["enabled"].get<bool>();
        }

        if (json.contains("preset") && json["preset"].is_string())
        {
            effect.preset = json["preset"].get<std::string>();
        }

        if (json.contains("start_scale") && json["start_scale"].is_number())
        {
            effect.start_scale = json["start_scale"].get<double>();
        }

        if (json.contains("end_scale") && json["end_scale"].is_number())
        {
            effect.end_scale = json["end_scale"].get<double>();
        }

        if (json.contains("start_x") && json["start_x"].is_number())
        {
            effect.start_x = json["start_x"].get<int>();
        }

        if (json.contains("start_y") && json["start_y"].is_number())
        {
            effect.start_y = json["start_y"].get<int>();
        }

        if (json.contains("end_x") && json["end_x"].is_number())
        {
            effect.end_x = json["end_x"].get<int>();
        }

        if (json.contains("end_y") && json["end_y"].is_number())
        {
            effect.end_y = json["end_y"].get<int>();
        }

        return true;
    }

    /**
     * @brief 解析 volume_mix 节点。
     */
    bool ConfigLoader::parseVolumeMixEffect(const Json &json, VolumeMixEffect &effect)
    {
        if (json.contains("enabled") && json["enabled"].is_boolean())
        {
            effect.enabled = json["enabled"].get<bool>();
        }

        if (json.contains("fade_in") && json["fade_in"].is_number())
        {
            effect.fade_in = json["fade_in"].get<double>();
        }

        if (json.contains("fade_out") && json["fade_out"].is_number())
        {
            effect.fade_out = json["fade_out"].get<double>();
        }

        return true;
    }

    /**
     * @brief 解析 global_effects 节点。
     */
    bool ConfigLoader::parseGlobalEffectsConfig(const Json &json, GlobalEffectsConfig &global_effects)
    {
        if (json.contains("audio_normalization") && json["audio_normalization"].is_object())
        {
            if (!parseAudioNormalizationConfig(json["audio_normalization"], global_effects.audio_normalization))
            {
                return false;
            }
        }

        if (json.contains("video_encoding") && json["video_encoding"].is_object())
        {
            if (!parseVideoEncodingConfig(json["video_encoding"], global_effects.video_encoding))
            {
                return false;
            }
        }

        if (json.contains("audio_encoding") && json["audio_encoding"].is_object())
        {
            if (!parseAudioEncodingConfig(json["audio_encoding"], global_effects.audio_encoding))
            {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief 解析 audio_normalization 节点。
     */
    bool ConfigLoader::parseAudioNormalizationConfig(const Json &json, AudioNormalizationConfig &config)
    {
        if (json.contains("enabled") && json["enabled"].is_boolean())
        {
            config.enabled = json["enabled"].get<bool>();
        }

        if (json.contains("target_level") && json["target_level"].is_number())
        {
            config.target_level = json["target_level"].get<double>();
        }

        return true;
    }

    /**
     * @brief 解析 video_encoding 节点。
     */
    bool ConfigLoader::parseVideoEncodingConfig(const Json &json, VideoEncodingConfig &config)
    {
        if (json.contains("codec") && json["codec"].is_string())
        {
            config.codec = json["codec"].get<std::string>();
        }

        if (json.contains("bitrate") && json["bitrate"].is_string())
        {
            config.bitrate = json["bitrate"].get<std::string>();
        }

        if (json.contains("preset") && json["preset"].is_string())
        {
            config.preset = json["preset"].get<std::string>();
        }

        if (json.contains("crf") && json["crf"].is_number())
        {
            config.crf = json["crf"].get<int>();
        }

        return true;
    }

    /**
     * @brief 解析 audio_encoding 节点。
     */
    bool ConfigLoader::parseAudioEncodingConfig(const Json &json, AudioEncodingConfig &config)
    {
        if (json.contains("codec") && json["codec"].is_string())
        {
            config.codec = json["codec"].get<std::string>();
        }

        if (json.contains("bitrate") && json["bitrate"].is_string())
        {
            config.bitrate = json["bitrate"].get<std::string>();
        }

        if (json.contains("channels") && json["channels"].is_number())
        {
            config.channels = json["channels"].get<int>();
        }

        return true;
    }

    /**
     * @brief 将场景类型字符串转换为枚举。
     */
    SceneType ConfigLoader::stringToSceneType(const std::string &typeStr)
    {
        const std::string lowered = toLowerCopy(typeStr);
        if (lowered == "image_scene")
        {
            return SceneType::IMAGE_SCENE;
        }
        if (lowered == "video_scene")
        {
            return SceneType::VIDEO_SCENE;
        }
        if (lowered == "transition")
        {
            return SceneType::TRANSITION;
        }
        return SceneType::IMAGE_SCENE;
    }

    /**
     * @brief 将转场类型字符串转换为枚举。
     */
    TransitionType ConfigLoader::stringToTransitionType(const std::string &typeStr)
    {
        const std::string lowered = toLowerCopy(typeStr);
        if (lowered == "crossfade")
        {
            return TransitionType::CROSSFADE;
        }
        if (lowered == "wipe")
        {
            return TransitionType::WIPE;
        }
        if (lowered == "slide")
        {
            return TransitionType::SLIDE;
        }
        return TransitionType::CROSSFADE;
    }

    /**
     * @brief 获取音频时长（含缓存）。
     */
    double ConfigLoader::getAudioDuration(const std::string &audioPath)
    {
        const std::string key = normalizedPath(audioPath);
        if (key.empty())
        {
            std::cerr << "[ConfigLoader] Audio path is empty\n";
            return -1.0;
        }

        const auto cachedIt = m_audioDurationCache.find(key);
        if (cachedIt != m_audioDurationCache.end())
        {
            return cachedIt->second;
        }

        const double duration = probeAudioDuration(key);
        m_audioDurationCache[key] = duration;
        return duration;
    }

    /**
     * @brief 获取视频时长（含缓存）。
     */
    double ConfigLoader::getVideoDuration(const std::string &videoPath)
    {
        const std::string key = normalizedPath(videoPath);
        if (key.empty())
        {
            std::cerr << "[ConfigLoader] Video path is empty\n";
            return -1.0;
        }

        const auto cachedIt = m_videoDurationCache.find(key);
        if (cachedIt != m_videoDurationCache.end())
        {
            return cachedIt->second;
        }

        const double duration = probeVideoDuration(key);
        m_videoDurationCache[key] = duration;
        return duration;
    }

    /**
     * @brief 使用 FFmpeg 探测音频文件总时长。
     *
     * 优先采用容器级 duration，容器缺失时回退到音频流 duration。
     */
    double ConfigLoader::probeAudioDuration(const std::string &audioPath)
    {
        if (audioPath.empty())
        {
            return -1.0;
        }

        if (!isReadableFile(audioPath))
        {
            std::cerr << "[ConfigLoader] Audio file not found: " << audioPath << "\n";
            return -1.0;
        }

        AVFormatContext *formatCtx = nullptr;
        int ret = avformat_open_input(&formatCtx, audioPath.c_str(), nullptr, nullptr);
        if (ret < 0)
        {
            std::cerr << "[ConfigLoader] Failed to open audio file: " << audioPath
                      << ", error: " << formatFFmpegError(ret) << "\n";
            return -1.0;
        }

        ret = avformat_find_stream_info(formatCtx, nullptr);
        if (ret < 0)
        {
            std::cerr << "[ConfigLoader] Failed to read audio stream info: " << audioPath
                      << ", error: " << formatFFmpegError(ret) << "\n";
            avformat_close_input(&formatCtx);
            return -1.0;
        }

        int audioStreamIndex = -1;
        for (unsigned int i = 0; i < formatCtx->nb_streams; i++)
        {
            if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                audioStreamIndex = static_cast<int>(i);
                break;
            }
        }

        if (audioStreamIndex == -1)
        {
            std::cerr << "[ConfigLoader] No audio stream in file: " << audioPath << "\n";
            avformat_close_input(&formatCtx);
            return -1.0;
        }

        double duration = 0.0;
        if (formatCtx->duration != AV_NOPTS_VALUE)
        {
            // 容器级时长精度更稳定，优先使用。
            duration = formatCtx->duration / static_cast<double>(AV_TIME_BASE);
        }
        else if (formatCtx->streams[audioStreamIndex]->duration != AV_NOPTS_VALUE)
        {
            // 对某些容器，只有流级时长可用。
            AVStream *stream = formatCtx->streams[audioStreamIndex];
            duration = stream->duration * av_q2d(stream->time_base);
        }

        avformat_close_input(&formatCtx);
        return duration > 0 ? duration : -1.0;
    }

    /**
     * @brief 使用 FFmpeg 探测视频文件总时长。
     */
    double ConfigLoader::probeVideoDuration(const std::string &videoPath)
    {
        if (videoPath.empty())
        {
            return -1.0;
        }

        if (!isReadableFile(videoPath))
        {
            std::cerr << "[ConfigLoader] Video file not found: " << videoPath << "\n";
            return -1.0;
        }

        AVFormatContext *formatCtx = nullptr;
        int ret = avformat_open_input(&formatCtx, videoPath.c_str(), nullptr, nullptr);
        if (ret < 0)
        {
            std::cerr << "[ConfigLoader] Failed to open video file: " << videoPath
                      << ", error: " << formatFFmpegError(ret) << "\n";
            return -1.0;
        }

        ret = avformat_find_stream_info(formatCtx, nullptr);
        if (ret < 0)
        {
            std::cerr << "[ConfigLoader] Failed to read video stream info: " << videoPath
                      << ", error: " << formatFFmpegError(ret) << "\n";
            avformat_close_input(&formatCtx);
            return -1.0;
        }

        int videoStreamIndex = -1;
        for (unsigned int i = 0; i < formatCtx->nb_streams; i++)
        {
            if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                videoStreamIndex = static_cast<int>(i);
                break;
            }
        }

        if (videoStreamIndex == -1)
        {
            std::cerr << "[ConfigLoader] No video stream in file: " << videoPath << "\n";
            avformat_close_input(&formatCtx);
            return -1.0;
        }

        double duration = 0.0;
        if (formatCtx->duration != AV_NOPTS_VALUE)
        {
            // 优先读取容器总时长。
            duration = formatCtx->duration / static_cast<double>(AV_TIME_BASE);
        }
        else if (formatCtx->streams[videoStreamIndex]->duration != AV_NOPTS_VALUE)
        {
            // 备用路径：读取视频流时长。
            AVStream *stream = formatCtx->streams[videoStreamIndex];
            duration = stream->duration * av_q2d(stream->time_base);
        }

        avformat_close_input(&formatCtx);
        return duration > 0 ? duration : -1.0;
    }

    /**
     * @brief 规范化路径，提升缓存命中率。
     */
    std::string ConfigLoader::normalizedPath(const std::string &path) const
    {
        if (path.empty())
        {
            return std::string();
        }

        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');

        // Collapse repeated slashes to improve cache hit rate.
        std::string compact;
        compact.reserve(normalized.size());
        char prev = '\0';
        for (char ch : normalized)
        {
            if (ch == '/' && prev == '/')
            {
                continue;
            }
            compact.push_back(ch);
            prev = ch;
        }
        return compact;
    }

} // namespace VideoCreator
