#ifndef AV_FORMAT_CONTEXT_WRAPPER_H
#define AV_FORMAT_CONTEXT_WRAPPER_H

#include <memory>
#include "FFmpegHeaders.h"

/**
 * @file AvFormatContextWrapper.h
 * @brief AVFormatContext RAII 定义。
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
        void operator()(AVFormatContext *context) const
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
