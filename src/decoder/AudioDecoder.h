#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <string>
#include <memory>
#include <vector>
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "ffmpeg_utils/AvFrameWrapper.h"
#include "model/ProjectConfig.h"

/**
 * @file AudioDecoder.h
 * @brief 声明音频解码器，支持重采样、音量和淡入淡出处理。
 */

namespace VideoCreator
{
    /**
     * @brief 音频解码器。
     */
    class AudioDecoder
    {
    public:
        /**
         * @brief 构造函数。
         */
        AudioDecoder();

        /**
         * @brief 析构函数。
         */
        ~AudioDecoder();

        /**
         * @brief 打开音频文件并初始化解码器/重采样器。
         * @param filePath 音频文件路径。
         * @return 成功返回 true。
         */
        bool open(const std::string &filePath);

        /**
         * @brief 根据场景配置应用音量和淡入淡出效果。
         * @param sceneConfig 场景配置。
         * @return 成功返回 true。
         */
        bool applyVolumeEffect(const SceneConfig& sceneConfig);

        /**
         * @brief 手动指定音量包络参数并初始化滤镜图。
         * @param baseVolume 基础音量倍率。
         * @param effect 可选音量特效（为空则仅应用基础音量）。
         * @param trackDurationSeconds 轨道时长（秒）。
         * @return 成功返回 true。
         */
        bool applyVolumeEffect(double baseVolume, const VolumeMixEffect* effect, double trackDurationSeconds);

        /**
         * @brief 解码下一帧音频并输出标准化 PCM 帧。
         * @param frame 输出帧。
         * @return >0 成功，0 表示 EOF，<0 表示错误。
         */
        int decodeFrame(FFmpegUtils::AvFramePtr &frame);

        /**
         * @brief 跳转到指定时间戳并刷新解码缓冲。
         * @param timestamp 时间戳（秒）。
         * @return 成功返回 true。
         */
        bool seek(double timestamp);

        /**
         * @brief 获取输出采样格式。
         * @return AVSampleFormat。
         */
        AVSampleFormat getSampleFormat() const { return m_sampleFormat; }

        /**
         * @brief 获取音频时长（秒）。
         * @return 时长秒数；不可用时返回 0。
         */
        double getDuration() const;

        /**
         * @brief 关闭解码器并释放资源。
         */
        void close();

        /**
         * @brief 获取最近错误信息。
         * @return 错误字符串副本。
         */
        std::string getErrorString() const { return m_errorString; }

    private:
        /**
         * @brief 初始化音频滤镜图。
         * @param baseVolume 基础音量倍率。
         * @param effect 可选音量特效。
         * @param trackDurationSeconds 轨道时长（秒）。
         * @return 成功返回 true。
         */
        bool initFilterGraph(double baseVolume, const VolumeMixEffect* effect, double trackDurationSeconds);

        /// 输入封装上下文。
        AVFormatContext *m_formatContext;
        /// 音频解码器上下文。
        AVCodecContext *m_codecContext;
        /// 音频流索引。
        int m_audioStreamIndex;
        /// 重采样器上下文。
        struct SwrContext *m_swrCtx;
        
        /// 音频滤镜图。
        AVFilterGraph *m_filterGraph;
        /// 滤镜输入节点。
        AVFilterContext *m_bufferSrcCtx;
        /// 滤镜输出节点。
        AVFilterContext *m_bufferSinkCtx;
        /// 当前是否启用音量效果处理。
        bool m_effectsEnabled = false;

        /// 输出采样率。
        int m_sampleRate;
        /// 输出声道数。
        int m_channels;
        /// 输出采样格式。
        AVSampleFormat m_sampleFormat;
        /// 音频总时长（时间基单位）。
        int64_t m_duration;

        /// 最近错误信息。
        std::string m_errorString;

        /**
         * @brief 释放 FFmpeg 资源。
         */
        void cleanup();
    };

} // namespace VideoCreator

#endif // AUDIO_DECODER_H
