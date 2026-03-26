#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

#include <string>
#include <vector>

/**
 * @file ProjectConfig.h
 * @brief 定义 VideoCreator 配置模型（项目、场景、资源、特效和编码参数）。
 */

namespace VideoCreator
{

    /**
     * @brief 场景类型。
     */
    enum class SceneType
    {
        IMAGE_SCENE,
        VIDEO_SCENE,
        TRANSITION
    };

    /**
     * @brief 转场类型。
     */
    enum class TransitionType
    {
        CROSSFADE,
        WIPE,
        SLIDE
    };

    /**
     * @brief 将转场枚举转换为可读字符串。
     * @param type 转场类型。
     * @return 转场名称字符串。
     */
    inline std::string transitionTypeToString(TransitionType type)
    {
        switch (type)
        {
        case TransitionType::CROSSFADE:
            return "CROSSFADE";
        case TransitionType::WIPE:
            return "WIPE";
        case TransitionType::SLIDE:
            return "SLIDE";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * @brief 图片资源配置。
     */
    struct ImageConfig
    {
        std::string path;      ///< 图片文件路径。
        int x = 0;             ///< X 坐标。
        int y = 0;             ///< Y 坐标。
        double scale = 1.0;    ///< 缩放比例。
        double rotation = 0.0; ///< 旋转角度（度）。
    };

    /**
     * @brief 音频资源配置。
     */
    struct AudioConfig
    {
        std::string path;          ///< 音频文件路径。
        double volume = 1.0;       ///< 音量倍率（1.0 为原始音量）。
        double start_offset = 0.0; ///< 相对场景起始的延迟秒数。
    };

    /**
     * @brief 视频资源配置。
     */
    struct VideoConfig
    {
        std::string path;        ///< 视频文件路径。
        double trim_start = 0.0; ///< 起始裁剪偏移（秒）。
        double trim_end = -1.0;  ///< 结束时间（秒），-1 表示使用原始全长。
        bool use_audio = true;   ///< 是否复用原视频音轨。
    };

    /**
     * @brief 场景资源集合。
     */
    struct ResourcesConfig
    {
        ImageConfig image;                 ///< 图片资源。
        AudioConfig audio;                 ///< 主音频资源。
        VideoConfig video;                 ///< 视频资源。
        std::vector<AudioConfig> audio_layers; ///< 附加混音轨道（BGM/SFX/配音层）。
    };

    /**
     * @brief Ken Burns 推拉摇移效果配置。
     */
    struct KenBurnsEffect
    {
        bool enabled = false;     ///< 是否启用效果。
        std::string preset;       ///< 预设名称（例如 zoom_in / pan_left）。
        double start_scale = 1.0; ///< 起始缩放倍率。
        double end_scale = 1.0;   ///< 结束缩放倍率。
        int start_x = 0;          ///< 起始 X 偏移。
        int start_y = 0;          ///< 起始 Y 偏移。
        int end_x = 0;            ///< 结束 X 偏移。
        int end_y = 0;            ///< 结束 Y 偏移。
    };

    /**
     * @brief 音量包络效果配置（淡入/淡出）。
     */
    struct VolumeMixEffect
    {
        bool enabled = false;  ///< 是否启用。
        double fade_in = 0.0;  ///< 淡入时长（秒）。
        double fade_out = 0.0; ///< 淡出时长（秒）。
    };

    /**
     * @brief 场景特效配置。
     */
    struct EffectsConfig
    {
        KenBurnsEffect ken_burns;   ///< Ken Burns 视觉效果。
        VolumeMixEffect volume_mix; ///< 音量混合效果。
    };

    /**
     * @brief 单个场景配置。
     */
    struct SceneConfig
    {
        int id = 0;                              ///< 场景 ID。
        SceneType type = SceneType::IMAGE_SCENE; ///< 场景类型。
        double duration = 0.0;                   ///< 场景持续时间（秒）。
        ResourcesConfig resources;               ///< 场景资源。
        EffectsConfig effects;                   ///< 场景特效。

        TransitionType transition_type = TransitionType::CROSSFADE; ///< 转场类型。
        int from_scene = 0; ///< 转场起始场景 ID（可选，当前主流程通常按顺序推断）。
        int to_scene = 0;   ///< 转场目标场景 ID（可选，当前主流程通常按顺序推断）。
    };

    /**
     * @brief 音频标准化配置。
     */
    struct AudioNormalizationConfig
    {
        bool enabled = false;        ///< 是否启用。
        double target_level = -16.0; ///< 目标响度级别（dB）。
    };

    /**
     * @brief 视频编码参数。
     */
    struct VideoEncodingConfig
    {
        std::string codec = "libx264"; ///< 编码器名称。
        std::string bitrate = "5000k"; ///< 目标码率。
        std::string preset = "medium"; ///< 编码预设。
        int crf = 23;                  ///< 恒定质量参数。
    };

    /**
     * @brief 音频编码参数。
     */
    struct AudioEncodingConfig
    {
        std::string codec = "aac";    ///< 编码器名称（例如 aac / libmp3lame）。
        std::string bitrate = "192k"; ///< 目标码率。
        int channels = 2;             ///< 输出声道数。
    };

    /**
     * @brief 全局效果和编码配置。
     */
    struct GlobalEffectsConfig
    {
        AudioNormalizationConfig audio_normalization; ///< 音频标准化设置。
        VideoEncodingConfig video_encoding;           ///< 视频编码设置。
        AudioEncodingConfig audio_encoding;           ///< 音频编码设置。
    };

    /**
     * @brief 项目级基础参数。
     */
    struct ProjectInfoConfig
    {
        std::string name;                         ///< 项目名称。
        std::string output_path;                  ///< 输出文件路径。
        int width = 1920;                         ///< 输出视频宽度。
        int height = 1080;                        ///< 输出视频高度。
        int fps = 30;                             ///< 输出帧率。
        std::string background_color = "#000000"; ///< 默认背景色。
    };

    /**
     * @brief 顶层项目配置对象。
     */
    struct ProjectConfig
    {
        ProjectInfoConfig project;          ///< 项目基础参数。
        std::vector<SceneConfig> scenes;    ///< 场景序列。
        GlobalEffectsConfig global_effects; ///< 全局效果与编码参数。

        /**
         * @brief 构造默认项目配置。
         */
        ProjectConfig()
        {
            project.width = 1920;
            project.height = 1080;
            project.fps = 30;
            project.background_color = "#000000";
        }
    };

} // namespace VideoCreator

#endif // PROJECT_CONFIG_H
