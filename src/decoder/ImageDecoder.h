#ifndef IMAGE_DECODER_H
#define IMAGE_DECODER_H

#include <string>
#include <memory>
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "ffmpeg_utils/AvFrameWrapper.h"

/**
 * @file ImageDecoder.h
 * @brief 声明图片解码器，负责读取单张图片并转换为 AVFrame。
 */

namespace VideoCreator
{
    /**
     * @brief 图片解码器。
     */
    class ImageDecoder
    {
    public:
        /**
         * @brief 构造函数。
         */
        ImageDecoder();

        /**
         * @brief 析构函数。
         */
        ~ImageDecoder();

        /**
         * @brief 打开图片文件并初始化解码上下文。
         * @param filePath 图片路径。
         * @return 成功返回 true。
         */
        bool open(const std::string &filePath);

        /**
         * @brief 解码图片为帧。
         * @return 解码后的帧；失败返回空指针。
         */
        FFmpegUtils::AvFramePtr decode();

        /**
         * @brief 解码并缓存图片，后续调用返回缓存副本。
         * @return 图片帧副本；失败返回空指针。
         */
        FFmpegUtils::AvFramePtr decodeAndCache();

        /**
         * @brief 将输入帧缩放/转换为指定尺寸和像素格式。
         * @param frame 输入帧。
         * @param targetWidth 目标宽度。
         * @param targetHeight 目标高度。
         * @param targetFormat 目标像素格式。
         * @return 缩放后的新帧；失败返回空指针。
         */
        FFmpegUtils::AvFramePtr scaleToSize(FFmpegUtils::AvFramePtr& frame, int targetWidth, int targetHeight, AVPixelFormat targetFormat = AV_PIX_FMT_YUV420P);

        /**
         * @brief 获取源图宽度。
         * @return 宽度像素值。
         */
        int getWidth() const { return m_width; }

        /**
         * @brief 获取源图高度。
         * @return 高度像素值。
         */
        int getHeight() const { return m_height; }

        /**
         * @brief 获取源图像素格式。
         * @return AVPixelFormat。
         */
        AVPixelFormat getPixelFormat() const { return m_pixelFormat; }

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
        /// 输入封装上下文。
        AVFormatContext *m_formatContext;
        /// 编码器上下文。
        AVCodecContext *m_codecContext;
        /// 视频流索引。
        int m_videoStreamIndex;
        /// 缩放上下文。
        SwsContext *m_swsContext;

        /// 图片宽度。
        int m_width;
        /// 图片高度。
        int m_height;
        /// 图片像素格式。
        AVPixelFormat m_pixelFormat;

        /// 最近错误信息。
        std::string m_errorString;
        /// 缓存帧。
        FFmpegUtils::AvFramePtr m_cachedFrame;

        /**
         * @brief 释放所有 FFmpeg 资源。
         */
        void cleanup();
    };

} // namespace VideoCreator

#endif // IMAGE_DECODER_H
