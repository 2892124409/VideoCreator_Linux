#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "ProjectConfig.h"

/**
 * @file ConfigLoader.h
 * @brief 提供 JSON -> ProjectConfig 的加载与解析能力。
 */

namespace VideoCreator
{
    /**
     * @brief 项目配置加载器。
     *
     * 支持从 JSON 文件和 JSON 字符串加载配置，并在解析阶段完成
     * 场景时长推断（音频/视频探测）和基础字段校验。
     */
    class ConfigLoader
    {
    public:
        /**
         * @brief 默认构造函数。
         */
        ConfigLoader() = default;

        /**
         * @brief 从 JSON 文件加载配置。
         * @param filePath JSON 文件路径。
         * @param config 输出配置对象。
         * @return 加载成功返回 true，否则返回 false。
         */
        bool loadFromFile(const std::string &filePath, ProjectConfig &config);

        /**
         * @brief 从 JSON 字符串加载配置。
         * @param jsonString JSON 文本。
         * @param config 输出配置对象。
         * @return 加载成功返回 true，否则返回 false。
         */
        bool loadFromString(const std::string &jsonString, ProjectConfig &config);

        /**
         * @brief 获取最近一次失败原因。
         * @return 错误信息字符串引用。
         */
        const std::string &errorString() const { return m_errorString; }

    private:
        using Json = nlohmann::json;

        /// 最近一次解析错误信息。
        std::string m_errorString;
        /// 音频时长缓存（key 为归一化路径）。
        std::unordered_map<std::string, double> m_audioDurationCache;
        /// 视频时长缓存（key 为归一化路径）。
        std::unordered_map<std::string, double> m_videoDurationCache;

        /**
         * @brief 解析 project 节点。
         * @param json project JSON 对象。
         * @param project 输出项目参数。
         * @return 解析成功返回 true。
         */
        bool parseProjectConfig(const Json &json, ProjectInfoConfig &project);

        /**
         * @brief 解析单个场景节点。
         * @param json 场景 JSON 对象。
         * @param scene 输出场景配置。
         * @return 解析成功返回 true。
         */
        bool parseSceneConfig(const Json &json, SceneConfig &scene);

        /**
         * @brief 解析资源配置。
         * @param json resources JSON 对象。
         * @param resources 输出资源配置。
         * @return 解析成功返回 true。
         */
        bool parseResourcesConfig(const Json &json, ResourcesConfig &resources);

        /**
         * @brief 解析视频资源配置。
         * @param json video JSON 对象。
         * @param video 输出视频配置。
         * @return 解析成功返回 true。
         */
        bool parseVideoConfig(const Json &json, VideoConfig &video);

        /**
         * @brief 解析图片资源配置。
         * @param json image JSON 对象。
         * @param image 输出图片配置。
         * @return 解析成功返回 true。
         */
        bool parseImageConfig(const Json &json, ImageConfig &image);

        /**
         * @brief 解析音频资源配置。
         * @param json audio JSON 对象。
         * @param audio 输出音频配置。
         * @return 解析成功返回 true。
         */
        bool parseAudioConfig(const Json &json, AudioConfig &audio);

        /**
         * @brief 解析场景特效配置。
         * @param json effects JSON 对象。
         * @param effects 输出特效配置。
         * @return 解析成功返回 true。
         */
        bool parseEffectsConfig(const Json &json, EffectsConfig &effects);

        /**
         * @brief 解析 Ken Burns 特效参数。
         * @param json ken_burns JSON 对象。
         * @param effect 输出效果配置。
         * @return 解析成功返回 true。
         */
        bool parseKenBurnsEffect(const Json &json, KenBurnsEffect &effect);

        /**
         * @brief 解析音量混合特效参数。
         * @param json volume_mix JSON 对象。
         * @param effect 输出效果配置。
         * @return 解析成功返回 true。
         */
        bool parseVolumeMixEffect(const Json &json, VolumeMixEffect &effect);

        /**
         * @brief 解析全局效果配置。
         * @param json global_effects JSON 对象。
         * @param global_effects 输出全局配置。
         * @return 解析成功返回 true。
         */
        bool parseGlobalEffectsConfig(const Json &json, GlobalEffectsConfig &global_effects);

        /**
         * @brief 解析音频标准化参数。
         * @param json audio_normalization JSON 对象。
         * @param config 输出配置。
         * @return 解析成功返回 true。
         */
        bool parseAudioNormalizationConfig(const Json &json, AudioNormalizationConfig &config);

        /**
         * @brief 解析视频编码参数。
         * @param json video_encoding JSON 对象。
         * @param config 输出配置。
         * @return 解析成功返回 true。
         */
        bool parseVideoEncodingConfig(const Json &json, VideoEncodingConfig &config);

        /**
         * @brief 解析音频编码参数。
         * @param json audio_encoding JSON 对象。
         * @param config 输出配置。
         * @return 解析成功返回 true。
         */
        bool parseAudioEncodingConfig(const Json &json, AudioEncodingConfig &config);

        /**
         * @brief 获取音频时长（秒），带缓存。
         * @param audioPath 音频文件路径。
         * @return 时长（秒），失败时返回负值。
         */
        double getAudioDuration(const std::string &audioPath);

        /**
         * @brief 获取视频时长（秒），带缓存。
         * @param videoPath 视频文件路径。
         * @return 时长（秒），失败时返回负值。
         */
        double getVideoDuration(const std::string &videoPath);

        /**
         * @brief 通过 FFmpeg 探测音频时长。
         * @param normalizedPath 归一化路径。
         * @return 时长（秒），失败时返回负值。
         */
        double probeAudioDuration(const std::string &normalizedPath);

        /**
         * @brief 通过 FFmpeg 探测视频时长。
         * @param normalizedPath 归一化路径。
         * @return 时长（秒），失败时返回负值。
         */
        double probeVideoDuration(const std::string &normalizedPath);

        /**
         * @brief 归一化路径用于缓存键。
         * @param path 原始路径。
         * @return 归一化后的路径。
         */
        std::string normalizedPath(const std::string &path) const;

        /**
         * @brief 字符串转场景枚举。
         * @param typeStr 场景类型字符串。
         * @return 转换后的 SceneType。
         */
        SceneType stringToSceneType(const std::string &typeStr);

        /**
         * @brief 字符串转转场枚举。
         * @param typeStr 转场类型字符串。
         * @return 转换后的 TransitionType。
         */
        TransitionType stringToTransitionType(const std::string &typeStr);
    };

} // namespace VideoCreator

#endif // CONFIG_LOADER_H
