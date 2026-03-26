#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "ProjectConfig.h"

namespace VideoCreator
{

    class ConfigLoader
    {
    public:
        ConfigLoader() = default;

        // 从JSON文件加载配置
        bool loadFromFile(const std::string &filePath, ProjectConfig &config);

        // 从JSON字符串加载配置
        bool loadFromString(const std::string &jsonString, ProjectConfig &config);

        // 获取错误信息
        const std::string &errorString() const { return m_errorString; }

    private:
        using Json = nlohmann::json;

        std::string m_errorString;
        std::unordered_map<std::string, double> m_audioDurationCache;
        std::unordered_map<std::string, double> m_videoDurationCache;

        // 解析项目配置
        bool parseProjectConfig(const Json &json, ProjectInfoConfig &project);

        // 解析场景配置
        bool parseSceneConfig(const Json &json, SceneConfig &scene);

        // 解析资源配置
        bool parseResourcesConfig(const Json &json, ResourcesConfig &resources);

        bool parseVideoConfig(const Json &json, VideoConfig &video);

        // 解析图片配置
        bool parseImageConfig(const Json &json, ImageConfig &image);

        // 解析音频配置
        bool parseAudioConfig(const Json &json, AudioConfig &audio);

        // 解析特效配置
        bool parseEffectsConfig(const Json &json, EffectsConfig &effects);

        // 解析Ken Burns特效
        bool parseKenBurnsEffect(const Json &json, KenBurnsEffect &effect);

        // 解析音量混合特效
        bool parseVolumeMixEffect(const Json &json, VolumeMixEffect &effect);

        // 解析全局效果配置
        bool parseGlobalEffectsConfig(const Json &json, GlobalEffectsConfig &global_effects);

        // 解析音频标准化配置
        bool parseAudioNormalizationConfig(const Json &json, AudioNormalizationConfig &config);

        // 解析视频编码配置
        bool parseVideoEncodingConfig(const Json &json, VideoEncodingConfig &config);

        // 解析音频编码配置
        bool parseAudioEncodingConfig(const Json &json, AudioEncodingConfig &config);

        // 获取音频文件时长（秒）
        double getAudioDuration(const std::string &audioPath);
        double getVideoDuration(const std::string &videoPath);
        double probeAudioDuration(const std::string &normalizedPath);
        double probeVideoDuration(const std::string &normalizedPath);
        std::string normalizedPath(const std::string &path) const;

        // 字符串到枚举转换
        SceneType stringToSceneType(const std::string &typeStr);
        TransitionType stringToTransitionType(const std::string &typeStr);
    };

} // namespace VideoCreator

#endif // CONFIG_LOADER_H
