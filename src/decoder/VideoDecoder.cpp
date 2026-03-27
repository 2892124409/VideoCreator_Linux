#include "VideoDecoder.h"

/**
 * @file VideoDecoder.cpp
 * @brief 实现视频解码与逐帧缩放转换。
 */

namespace VideoCreator
{
    /**
     * @brief 构造函数。
     */
    VideoDecoder::VideoDecoder()
    {
    }

    VideoDecoder::~VideoDecoder()
    {
        cleanup();
    }

    /**
     * @brief 打开视频文件并初始化解码上下文。
     */
    bool VideoDecoder::open(const std::string &filePath)
    {
        if (!m_decoderCore.open(filePath, m_errorString))
        {
            cleanup();
            return false;
        }

        return true;
    }

    /**
     * @brief 解码下一帧视频。
     *
     * 解码流程会在 `receive_frame` 与 `send_packet` 之间循环，
     * 直到拿到一帧、到达 EOF 或出现错误。
     */
    int VideoDecoder::decodeFrame(FFmpegUtils::AvFramePtr &frame)
    {
        return m_decoderCore.decodeFrame(frame, m_errorString);
    }

    /**
     * @brief 缩放并转换帧格式，同时保留色彩空间信息。
     */
    FFmpegUtils::AvFramePtr VideoDecoder::scaleFrame(const AVFrame *frame, int targetWidth, int targetHeight, AVPixelFormat targetFormat)
    {
        return m_frameScaler.scale(frame, targetWidth, targetHeight, targetFormat, m_errorString);
    }

    /**
     * @brief 获取视频时长（秒）。
     */
    double VideoDecoder::getDuration() const
    {
        if (!m_decoderCore.isOpen())
        {
            return 0.0;
        }

        const int64_t duration = m_decoderCore.duration();
        if (duration != AV_NOPTS_VALUE)
        {
            return duration * av_q2d(m_decoderCore.timeBase());
        }

        return 0.0;
    }

    /**
     * @brief 关闭解码器。
     */
    void VideoDecoder::close()
    {
        cleanup();
    }

    /**
     * @brief 清理 FFmpeg 资源。
     */
    void VideoDecoder::cleanup()
    {
        m_frameScaler.reset();
        m_decoderCore.close();
    }

} // namespace VideoCreator
