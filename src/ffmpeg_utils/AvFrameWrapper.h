#ifndef AV_FRAME_WRAPPER_H
#define AV_FRAME_WRAPPER_H

#include <memory>

#include "FFmpegHeaders.h"

/**
 * @file AvFrameWrapper.h
 * @brief AVFrame 的 RAII 包装与构造辅助函数。
 * @details AVFrame 是 FFmpeg 里一帧数据容器。
 * 视频时：一张图像帧(像素数据 + 宽高 + 像素格式 + 时间戳 + pts)
 * 音频时：一块采样帧(采样数据 + 采样率 + 声道分布 + 样本数)
 * AVPacket 像数据压缩包, AVFrame 像解压后的可处理数据块
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
    void operator()(AVFrame* frame) const
    {
        if (frame)
        {
            av_frame_free(&frame); // 对 AVFrame 指针进行取地址操作，表示指针的地址
        }
    }
};

/**
 * @brief AVFrame 智能指针别名。
 * @details
 * 表示用AvFramePtr代表AVFrame的unique_ptr,并且释放方式用自定义的AvFrameDeleter。因为AVFrame不是通过new直接创建的而是用av_frame_alloc()分配的,所以不能直接用默认的delete释放,而要用自定义的删除器
 */
using AvFramePtr = std::unique_ptr<AVFrame, AvFrameDeleter>;

/**
 * @brief 创建空 AVFrame。
 * @return 新建帧指针，分配失败返回空。
 */
inline AvFramePtr createAvFrame()
{
    // 这里只装了一个对象壳子，没有真正分配内存
    AVFrame* frame = av_frame_alloc();
    return AvFramePtr(frame);
}

/**
 * @brief 复制 AVFrame 引用。
 * @param src 源帧。
 * @return 新帧指针；失败返回空。
 */
inline AvFramePtr copyAvFrame(const AVFrame* src)
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

    // dst.get() 是从 unique_ptr 取裸指针,因为 av_frame_ref 里的参数必须是裸指针
    // av_frame_ref(dst.get(),src) 表示让 ds t引用 src 的帧数据,失败返回值 <0
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

    // 这里设置完width、height、format后才能真正分配内存,32表示内存对齐
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
    // 目前固定双声道,这个操作是防止编译器报警告有未使用参数
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
