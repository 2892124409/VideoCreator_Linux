#ifndef AV_CODEC_CONTEXT_WRAPPER_H
#define AV_CODEC_CONTEXT_WRAPPER_H

#include "FFmpegHeaders.h"
#include <memory>

/**
 * @file AvCodecContextWrapper.h
 * @brief AVCodecContext RAII 定义。
 * @details AVCodecContext 是 FFmpeg 中“编解码器实例的运行上下文”。
 * 它同时承载：
 * 1) 编解码参数（分辨率/像素格式/采样率/码率等），
 * 2) 运行时状态（内部缓冲、参考帧、时间戳重排等），
 * 3) 编码器/解码器调用入口（send/receive 流程依赖该上下文）。
 */

namespace FFmpegUtils
{

/**
 * @brief AVCodecContext 自定义删除器。
 */
struct AvCodecContextDeleter
{
    /**
     * @brief 释放 AVCodecContext。
     * @param context 待释放上下文。
     */
    void operator()(AVCodecContext* context) const
    {
        if (context)
        {
            avcodec_free_context(&context);
        }
    }
};

/**
 * @brief AVCodecContext 智能指针别名。
 */
using AvCodecContextPtr = std::unique_ptr<AVCodecContext, AvCodecContextDeleter>;

} // namespace FFmpegUtils

#endif // AV_CODEC_CONTEXT_WRAPPER_H
