#ifndef AV_PACKET_WRAPPER_H
#define AV_PACKET_WRAPPER_H

#include <memory>

#include "FFmpegHeaders.h"

/**
 * @file AvPacketWrapper.h
 * @brief AVPacket 的 RAII 包装与复制辅助函数。
 */

namespace FFmpegUtils
{
    /**
     * @brief AVPacket 自定义删除器。
     */
    struct AvPacketDeleter
    {
        /**
         * @brief 释放 AVPacket。
         * @param packet 待释放包指针。
         */
        void operator()(AVPacket *packet) const
        {
            if (packet)
            {
                av_packet_free(&packet);
            }
        }
    };

    /**
     * @brief AVPacket 智能指针别名。
     */
    using AvPacketPtr = std::unique_ptr<AVPacket, AvPacketDeleter>;

    /**
     * @brief 创建空 AVPacket。
     * @return 新包指针，分配失败返回空。
     */
    inline AvPacketPtr createAvPacket()
    {
        AVPacket *packet = av_packet_alloc();
        return AvPacketPtr(packet);
    }

    /**
     * @brief 复制 AVPacket 引用。
     * @param src 源包。
     * @return 新包指针；失败返回空。
     */
    inline AvPacketPtr copyAvPacket(const AVPacket *src)
    {
        if (!src)
        {
            return nullptr;
        }

        AVPacket *dst = av_packet_alloc();
        if (!dst)
        {
            return nullptr;
        }

        if (av_packet_ref(dst, src) < 0)
        {
            av_packet_free(&dst);
            return nullptr;
        }

        return AvPacketPtr(dst);
    }

} // namespace FFmpegUtils

#endif // AV_PACKET_WRAPPER_H
