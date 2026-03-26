#ifndef AV_FRAME_WRAPPER_H
#define AV_FRAME_WRAPPER_H

#include <memory>

#include "FFmpegHeaders.h"

/**
 * @file AvFrameWrapper.h
 * @brief AVFrame 的 RAII 包装与构造辅助函数。
 */

namespace FFmpegUtils
{
    /**
     * @brief AVFrame 自定义删除器。
     */
    struct AvFrameDeleter
    {
        /**
         * @brief 释放 AVFrame。
         * @param frame 待释放帧指针。
         */
        void operator()(AVFrame *frame) const
        {
            if (frame)
            {
                av_frame_free(&frame);
            }
        }
    };

    /**
     * @brief AVFrame 智能指针别名。
     */
    using AvFramePtr = std::unique_ptr<AVFrame, AvFrameDeleter>;

    /**
     * @brief 创建空 AVFrame。
     * @return 新建帧指针，分配失败返回空。
     */
    inline AvFramePtr createAvFrame()
    {
        AVFrame *frame = av_frame_alloc();
        return AvFramePtr(frame);
    }

    /**
     * @brief 复制 AVFrame 引用。
     * @param src 源帧。
     * @return 新帧指针；失败返回空。
     */
    inline AvFramePtr copyAvFrame(const AVFrame *src)
    {
        if (!src)
        {
            return nullptr;
        }

        AvFramePtr dst = createAvFrame();
        if (!dst)
        {
            return nullptr;
        }

        if (av_frame_ref(dst.get(), src) < 0)
        {
            return nullptr;
        }

        return dst;
    }

    /**
     * @brief 创建指定尺寸和像素格式的视频帧。
     * @param width 宽度。
     * @param height 高度。
     * @param format 像素格式。
     * @return 创建后的帧指针；失败返回空。
     */
    inline AvFramePtr createAvFrame(int width, int height, AVPixelFormat format)
    {
        AvFramePtr frame = createAvFrame();
        if (!frame)
        {
            return nullptr;
        }

        frame->width = width;
        frame->height = height;
        frame->format = format;

        if (av_frame_get_buffer(frame.get(), 32) < 0)
        {
            return nullptr;
        }

        return frame;
    }

    /**
     * @brief 创建音频帧（仅设置核心参数并分配 buffer）。
     * @param nb_samples 采样数。
     * @param format 采样格式。
     * @param channels 声道数（当前参数保留，依赖 FFmpeg frame channel layout 设置）。
     * @param sample_rate 采样率。
     * @return 创建后的帧指针；失败返回空。
     */
    inline AvFramePtr createAudioFrame(int nb_samples, AVSampleFormat format, int channels, int sample_rate)
    {
        (void)channels;

        AvFramePtr frame = createAvFrame();
        if (!frame)
        {
            return nullptr;
        }

        frame->nb_samples = nb_samples;
        frame->format = format;
        frame->sample_rate = sample_rate;

        if (av_frame_get_buffer(frame.get(), 0) < 0)
        {
            return nullptr;
        }

        return frame;
    }

} // namespace FFmpegUtils

#endif // AV_FRAME_WRAPPER_H
