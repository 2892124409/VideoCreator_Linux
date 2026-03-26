#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <string>
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "ffmpeg_utils/AvFrameWrapper.h"

/**
 * @file VideoDecoder.h
 * @brief 声明视频解码器，提供逐帧解码与缩放转换能力。
 */

namespace VideoCreator
{
    /**
     * @brief 视频解码器。
     */
    class VideoDecoder
    {
    public:
        /**
         * @brief 构造函数。
         */
        VideoDecoder();

        /**
         * @brief 析构函数。
         */
        ~VideoDecoder();

        /**
         * @brief 打开视频文件并初始化解码器。
         * @param filePath 视频路径。
         * @return 成功返回 true。
         */
        bool open(const std::string &filePath);

        /**
         * @brief 解码下一帧原始画面。
         * @param frame 输出帧。
         * @return >0 成功，0 表示 EOF，<0 表示错误。
         */
        int decodeFrame(FFmpegUtils::AvFramePtr &frame);

        /**
         * @brief 将帧缩放/转换为目标尺寸和像素格式。
         * @param frame 输入帧。
         * @param targetWidth 目标宽度。
         * @param targetHeight 目标高度。
         * @param targetFormat 目标像素格式。
         * @return 转换后的帧；失败返回空指针。
         */
        FFmpegUtils::AvFramePtr scaleFrame(const AVFrame *frame, int targetWidth, int targetHeight, AVPixelFormat targetFormat = AV_PIX_FMT_YUV420P);

        /**
         * @brief 获取视频时长（秒）。
         * @return 时长秒数；不可用时返回 0。
         */
        double getDuration() const;

        /**
         * @brief 获取平均帧率。
         * @return 帧率值。
         */
        double getFrameRate() const { return m_frameRate; }

        /**
         * @brief 关闭解码器并释放资源。
         */
        void close();

        /**
         * @brief 获取最近错误信息。
         * @return 错误字符串副本。
         */
        std::string getErrorString() const { return m_errorString; }

        /**
         * @brief 获取源视频宽度。
         * @return 宽度像素值。
         */
        int sourceWidth() const { return m_codecContext ? m_codecContext->width : 0; }

        /**
         * @brief 获取源视频高度。
         * @return 高度像素值。
         */
        int sourceHeight() const { return m_codecContext ? m_codecContext->height : 0; }

        /**
         * @brief 获取源视频像素格式。
         * @return AVPixelFormat。
         */
        AVPixelFormat sourceFormat() const { return m_codecContext ? m_codecContext->pix_fmt : AV_PIX_FMT_NONE; }

    private:
        /// 输入封装上下文。
        AVFormatContext *m_formatContext;
        /// 视频解码器上下文。
        AVCodecContext *m_codecContext;
        /// 缩放上下文。
        SwsContext *m_swsContext;
        /// 视频流索引。
        int m_videoStreamIndex;
        /// 视频流时间基。
        AVRational m_timeBase;
        /// 估算帧率。
        double m_frameRate;
        /// 视频时长（时间基单位）。
        int64_t m_duration;

        /// 最近错误信息。
        std::string m_errorString;

        /**
         * @brief 释放所有 FFmpeg 资源。
         */
        void cleanup();
    };

} // namespace VideoCreator

#endif // VIDEO_DECODER_H
