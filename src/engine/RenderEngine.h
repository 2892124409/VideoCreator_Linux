#ifndef RENDER_ENGINE_H
#define RENDER_ENGINE_H

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <future>
#include "model/ProjectConfig.h"
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "ffmpeg_utils/AvFrameWrapper.h"
#include "ffmpeg_utils/AvFormatContextWrapper.h"
#include "ffmpeg_utils/AvCodecContextWrapper.h"

/**
 * @file RenderEngine.h
 * @brief 声明视频渲染主引擎。
 */

namespace VideoCreator
{
    /**
     * @brief 渲染引擎，负责将 ProjectConfig 转换为最终视频文件。
     */
    class RenderEngine
    {
    public:
        /**
         * @brief 构造渲染引擎。
         */
        RenderEngine();

        /**
         * @brief 析构渲染引擎并释放 FFmpeg 资源。
         */
        ~RenderEngine();

        /**
         * @brief 初始化渲染上下文和编码器。
         * @param config 项目配置。
         * @return 初始化成功返回 true。
         */
        bool initialize(const ProjectConfig &config);

        /**
         * @brief 执行完整渲染流程。
         * @return 渲染成功返回 true。
         */
        bool render();

        /**
         * @brief 获取当前渲染进度（0-100）。
         * @return 进度百分比。
         */
        int progress() const { return m_progress; }

        /**
         * @brief 获取最近错误信息。
         * @return 错误字符串副本。
         */
        std::string errorString() const { return m_errorString; }

    private:
        /// 当前项目配置快照。
        ProjectConfig m_config;
        /// 当前进度百分比。
        int m_progress;
        /// 最近错误信息。
        std::string m_errorString;
        /// 用于进度估算的项目总帧数。
        double m_totalProjectFrames;
        /// 上一次输出到控制台的进度值。
        int m_lastReportedProgress;

        /**
         * @brief 创建输出容器上下文并打开输出文件。
         * @return 成功返回 true。
         */
        bool createOutputContext();

        /**
         * @brief 创建并配置视频编码流。
         * @return 成功返回 true。
         */
        bool createVideoStream();

        /**
         * @brief 创建并配置音频编码流。
         * @return 成功返回 true。
         */
        bool createAudioStream();

        /**
         * @brief 渲染单个普通场景（图片或视频）。
         * @param scene 场景配置。
         * @return 成功返回 true。
         */
        bool renderScene(const SceneConfig &scene);

        /**
         * @brief 渲染两个场景之间的转场。
         * @param transitionScene 转场场景配置。
         * @param fromScene 前一场景。
         * @param toScene 后一场景。
         * @return 成功返回 true。
         */
        bool renderTransition(const SceneConfig &transitionScene, const SceneConfig &fromScene, const SceneConfig &toScene);

        /**
         * @brief 生成转场期间的音频淡入淡出/混音。
         * @param fromScene 起始场景。
         * @param toScene 目标场景。
         * @param duration_seconds 转场持续时长（秒）。
         * @return 成功返回 true。
         */
        bool renderAudioTransition(const SceneConfig &fromScene, const SceneConfig &toScene, double duration_seconds);

        /**
         * @brief 从视频场景提取首帧或末帧并缩放到输出分辨率。
         * @param scene 视频场景。
         * @param fetchLastFrame 为 true 时提取末帧，否则提取首帧。
         * @return 提取成功返回帧对象，否则返回空指针。
         */
        FFmpegUtils::AvFramePtr extractVideoSceneFrame(const SceneConfig &scene, bool fetchLastFrame);

        /**
         * @brief 缓存场景首帧（用于后续转场复用）。
         * @param scene 场景信息。
         * @param frame 待缓存帧。
         */
        void cacheSceneFirstFrame(const SceneConfig &scene, const AVFrame *frame);

        /**
         * @brief 缓存场景末帧（用于后续转场复用）。
         * @param scene 场景信息。
         * @param frame 待缓存帧。
         */
        void cacheSceneLastFrame(const SceneConfig &scene, const AVFrame *frame);

        /**
         * @brief 获取缓存中的场景首帧/末帧副本。
         * @param scene 场景信息。
         * @param lastFrame true 表示取末帧，false 表示取首帧。
         * @return 帧副本；无缓存时返回空指针。
         */
        FFmpegUtils::AvFramePtr getCachedSceneFrame(const SceneConfig &scene, bool lastFrame);

        /**
         * @brief 确保可复用混音帧容量满足需求。
         * @param samplesNeeded 期望采样数。
         * @return 成功返回 true。
         */
        bool ensureReusableAudioFrame(int samplesNeeded);

        /**
         * @brief 为视频场景首帧预取任务建队。
         */
        void scheduleVideoPrefetchTasks();

        /**
         * @brief 获取并落地某个场景的预取结果。
         * @param scene 场景信息。
         */
        void resolveScenePrefetch(const SceneConfig &scene);

        /**
         * @brief 将场景帧写入缓存映射。
         * @param cache 目标缓存。
         * @param scene 场景信息。
         * @param frame 帧对象。
         */
        void storeSceneFrame(std::unordered_map<int, FFmpegUtils::AvFramePtr> &cache, const SceneConfig &scene, FFmpegUtils::AvFramePtr frame);

        /**
         * @brief 生成测试帧（调试/演示用途）。
         * @param frameIndex 帧索引。
         * @param width 目标宽度。
         * @param height 目标高度。
         * @return 生成的帧。
         */
        FFmpegUtils::AvFramePtr generateTestFrame(int frameIndex, int width, int height);

        /**
         * @brief 从音频 FIFO 读取固定帧大小并推送到编码器。
         * @return 成功返回 true。
         */
        bool sendBufferedAudioFrames();

        /**
         * @brief 冲洗音频 FIFO 和编码器。
         * @return 成功返回 true。
         */
        bool flushAudio();

        /**
         * @brief 更新并输出渲染进度。
         */
        void updateAndReportProgress();

        /**
         * @brief 冲洗编码器剩余包并写入复用器。
         * @param codecCtx 编码器上下文。
         * @param stream 对应输出流。
         * @return 成功返回 true。
         */
        bool flushEncoder(AVCodecContext *codecCtx, AVStream *stream);

        /// 输出封装上下文。
        FFmpegUtils::AvFormatContextPtr m_outputContext;
        /// 视频编码器上下文。
        FFmpegUtils::AvCodecContextPtr m_videoCodecContext;
        /// 音频编码器上下文。
        FFmpegUtils::AvCodecContextPtr m_audioCodecContext;
        /// 视频输出流指针。
        AVStream *m_videoStream;
        /// 音频输出流指针。
        AVStream *m_audioStream;
        /// 音频 FIFO 缓冲。
        AVAudioFifo *m_audioFifo;
        /// 已输出视频帧计数。
        int m_frameCount;
        /// 已输出音频采样计数。
        int64_t m_audioSamplesCount;

        /// 是否启用音频转场混音（默认关闭，保留实现）。
        bool m_enableAudioTransition;
        /// 场景首帧缓存。
        std::unordered_map<int, FFmpegUtils::AvFramePtr> m_sceneFirstFrames;
        /// 场景末帧缓存。
        std::unordered_map<int, FFmpegUtils::AvFramePtr> m_sceneLastFrames;
        /// 混音左声道临时缓冲。
        std::vector<float> m_mixBufferLeft;
        /// 混音右声道临时缓冲。
        std::vector<float> m_mixBufferRight;
        /// 可复用混音帧，减少频繁分配。
        FFmpegUtils::AvFramePtr m_reusableMixFrame;
        /// 可复用混音帧当前容量（采样数）。
        int m_reusableMixFrameCapacity;
        /// 视频场景首帧预取任务。
        std::unordered_map<int, std::future<FFmpegUtils::AvFramePtr>> m_sceneFirstFramePrefetch;
    };

} // namespace VideoCreator

#endif // RENDER_ENGINE_H
