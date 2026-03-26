#ifndef AV_CODEC_CONTEXT_WRAPPER_H
#define AV_CODEC_CONTEXT_WRAPPER_H

#include <memory>
#include "FFmpegHeaders.h"

/**
 * @file AvCodecContextWrapper.h
 * @brief AVCodecContext RAII 定义。
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
        void operator()(AVCodecContext *context) const
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
