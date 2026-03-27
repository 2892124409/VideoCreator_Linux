#ifndef EFFECT_PROCESSOR_H
#define EFFECT_PROCESSOR_H

#include <memory>
#include <string>

#include "ffmpeg_utils/AvFrameWrapper.h"
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "model/ProjectConfig.h"

/**
 * @file EffectProcessor.h
 * @brief 声明基于 FFmpeg filter graph 的特效处理器。
 */

namespace VideoCreator
{
/**
 * @brief 特效处理器。
 *
 * 支持两类流式处理序列：
 * 1. Ken Burns 动画序列；
 * 2. 场景转场序列。
 */
class EffectProcessor
{
  public:
    /**
     * @brief 构造函数。
     */
    EffectProcessor();

    /**
     * @brief 析构函数。
     */
    ~EffectProcessor();

    /**
     * @brief 初始化处理器输出参数。
     * @param width 目标宽度。
     * @param height 目标高度。
     * @param format 目标像素格式。
     * @param fps 目标帧率。
     * @return 初始化成功返回 true。
     */
    bool initialize(int width, int height, AVPixelFormat format, int fps);

    /**
     * @brief 启动 Ken Burns 序列。
     * @param effect Ken Burns 参数。
     * @param inputImage 输入图片帧。
     * @param total_frames 序列总帧数。
     * @return 启动成功返回 true。
     */
    bool startKenBurnsSequence(const KenBurnsEffect& effect, const AVFrame* inputImage, int total_frames);

    /**
     * @brief 拉取下一帧 Ken Burns 输出帧。
     * @param outFrame 输出帧。
     * @return 获取成功返回 true。
     */
    bool fetchKenBurnsFrame(FFmpegUtils::AvFramePtr& outFrame);

    /**
     * @brief 启动转场序列。
     * @param type 转场类型。
     * @param fromFrame 起始帧。
     * @param toFrame 目标帧。
     * @param duration_frames 转场帧数。
     * @return 启动成功返回 true。
     */
    bool startTransitionSequence(
        TransitionType type, const AVFrame* fromFrame, const AVFrame* toFrame, int duration_frames);

    /**
     * @brief 拉取下一帧转场输出帧。
     * @param outFrame 输出帧。
     * @return 获取成功返回 true。
     */
    bool fetchTransitionFrame(FFmpegUtils::AvFramePtr& outFrame);

    /**
     * @brief 获取最近错误信息。
     * @return 错误字符串副本。
     */
    std::string getErrorString() const
    {
        return m_errorString;
    }

    /**
     * @brief 关闭处理器并释放 filter graph。
     */
    void close();

  private:
    /**
     * @brief 当前运行的序列类型。
     */
    enum class SequenceType
    {
        None,
        KenBurns,
        Transition
    };

    /// Filter graph 根对象。
    AVFilterGraph* m_filterGraph;
    /// 主输入源。
    AVFilterContext* m_buffersrcContext;
    /// 转场第二输入源。
    AVFilterContext* m_buffersrcContext2;
    /// 输出节点。
    AVFilterContext* m_buffersinkContext;

    /// 输出宽度。
    int m_width;
    /// 输出高度。
    int m_height;
    /// 输出像素格式。
    AVPixelFormat m_pixelFormat;
    /// 输出帧率。
    int m_fps;
    /// 最近错误信息。
    mutable std::string m_errorString;

    /// 当前序列类型。
    SequenceType m_sequenceType;
    /// 期望输出总帧数。
    int m_expectedFrames;
    /// 已输出帧数。
    int m_generatedFrames;

    /**
     * @brief 初始化单输入滤镜图。
     * @param filterDescription FFmpeg filter 描述串。
     * @return 初始化成功返回 true。
     */
    bool initFilterGraph(const std::string& filterDescription);

    /**
     * @brief 初始化双输入转场滤镜图。
     * @param filter_description FFmpeg filter 描述串。
     * @return 初始化成功返回 true。
     */
    bool initTransitionFilterGraph(const std::string& filter_description);

    /**
     * @brief 从滤镜图输出端拉取一帧。
     * @param outFrame 输出帧。
     * @return 成功返回 true。
     */
    bool retrieveFrame(FFmpegUtils::AvFramePtr& outFrame);

    /**
     * @brief 为输出帧补齐色彩元数据。
     * @param frame 目标帧。
     */
    void stampFrameColorInfo(AVFrame* frame) const;

    /**
     * @brief 重置序列状态计数。
     */
    void resetSequenceState();

    /**
     * @brief 清理 FFmpeg 资源。
     */
    void cleanup();
};

} // namespace VideoCreator

#endif // EFFECT_PROCESSOR_H
