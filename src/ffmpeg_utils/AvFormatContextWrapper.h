#ifndef AV_FORMAT_CONTEXT_WRAPPER_H
#define AV_FORMAT_CONTEXT_WRAPPER_H

#include "FFmpegHeaders.h"
#include <memory>

/**
 * @file AvFormatContextWrapper.h
 * @brief AVFormatContext RAII 定义。
 * @details AVFormatContext 是 FFmpeg 的“容器层上下文”，负责管理：
 * 1) 输入/输出容器状态（如 mp4/mkv/flv），
 * 2) 文件或网络 I/O 句柄（pb），
 * 3) 流信息集合（视频流/音频流/字幕流等）。
 * 本封装用于确保作用域结束时按正确顺序释放资源：
 * 先关闭 I/O（avio_closep），再释放上下文（avformat_free_context）。
 */

namespace FFmpegUtils
{

/**
 * @brief AVFormatContext 自定义删除器。
 */
struct AvFormatContextDeleter
{
    /**
     * @brief 关闭并释放 AVFormatContext。
     * @param context 待释放上下文。
     */
    void operator()(AVFormatContext* context) const
    {
        if (context)
        {
            if (context->pb)
            {
                avio_closep(&context->pb);
            }
            avformat_free_context(context);
        }
    }
};

/**
 * @brief AVFormatContext 智能指针别名（主要用于输出上下文）。
 */
using AvFormatContextPtr = std::unique_ptr<AVFormatContext, AvFormatContextDeleter>;

} // namespace FFmpegUtils

#endif // AV_FORMAT_CONTEXT_WRAPPER_H
