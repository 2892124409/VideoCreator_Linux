#ifndef FRAME_SCALER_H
#define FRAME_SCALER_H

#include <string>

#include "ffmpeg_utils/AvFrameWrapper.h"
#include "ffmpeg_utils/FFmpegHeaders.h"

/**
 * @file FrameScaler.h
 * @brief 公共帧缩放与色彩空间转换工具。
 */

namespace VideoCreator
{
/**
 * @brief 封装 swscale 逻辑，供 ImageDecoder/VideoDecoder 复用。
 */
class FrameScaler
{
  public:
    FrameScaler()
        : m_swsContext(nullptr)
    {
    }

    ~FrameScaler()
    {
        reset();
    }

    FrameScaler(const FrameScaler&) = delete;
    FrameScaler& operator=(const FrameScaler&) = delete;

    FrameScaler(FrameScaler&& other) noexcept
        : m_swsContext(other.m_swsContext)
    {
        other.m_swsContext = nullptr;
    }

    FrameScaler& operator=(FrameScaler&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_swsContext = other.m_swsContext;
            other.m_swsContext = nullptr;
        }
        return *this;
    }

    /**
     * @brief 缩放/转换输入帧。
     * @param frame 输入帧。
     * @param targetWidth 目标宽度。
     * @param targetHeight 目标高度。
     * @param targetFormat 目标像素格式。
     * @param errorString 错误信息输出。
     * @return 转换后的帧；失败返回空。
     */
    FFmpegUtils::AvFramePtr scale(
        const AVFrame* frame, int targetWidth, int targetHeight, AVPixelFormat targetFormat, std::string& errorString)
    {
        if (!frame)
        {
            errorString = "输入帧为空";
            return nullptr;
        }

        /**
         * @brief 获取可复用的 swscale 上下文（不存在则创建，参数变化则重建）。
         * @param[in] context 传入旧的 `m_swsContext`，用于缓存复用。
         * @param[in] srcW 源宽度，传入 `frame->width`。
         * @param[in] srcH 源高度，传入 `frame->height`。
         * @param[in] srcFormat 源像素格式，传入 `static_cast<AVPixelFormat>(frame->format)`。
         * @param[in] dstW 目标宽度，传入 `targetWidth`。
         * @param[in] dstH 目标高度，传入 `targetHeight`。
         * @param[in] dstFormat 目标像素格式，传入 `targetFormat`。
         * @param[in] flags 缩放算法标志，传入 `SWS_BILINEAR`。
         * @param[in] srcFilter 源滤镜，传入 `nullptr`（默认）。
         * @param[in] dstFilter 目标滤镜，传入 `nullptr`（默认）。
         * @param[in] param 额外参数，传入 `nullptr`（默认）。
         * @return 成功返回可用 `SwsContext*`；失败返回 `nullptr`。
         */
        m_swsContext = sws_getCachedContext(m_swsContext,
            frame->width,
            frame->height,
            static_cast<AVPixelFormat>(frame->format),
            targetWidth,
            targetHeight,
            targetFormat,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr);
        if (!m_swsContext)
        {
            errorString = "无法创建缩放上下文";
            return nullptr;
        }

        int srcRange = (frame->color_range == AVCOL_RANGE_MPEG) ? 0 : 1;
        int dstRange = 0;
        int colorspace = frame->colorspace;
        if (colorspace == AVCOL_SPC_UNSPECIFIED)
        {
            colorspace = (frame->height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        }

        /**
         * @brief 根据色彩空间获取 YUV<->RGB 变换矩阵系数。
         * @param[in] colorspace 传入推断后的 `colorspace`（如 BT.709 / SMPTE170M）。
         * @return 成功返回 `const int*` 系数表；失败返回 `nullptr`。
         */
        const int* coeffs = sws_getCoefficients(colorspace);
        if (!coeffs)
        {
            /**
             * @brief 获取兜底色彩矩阵系数（720p 以上优先 709，否则 601）。
             * @param[in] colorspace 传入 `SWS_CS_ITU709` 或 `SWS_CS_ITU601`。
             * @return 成功返回 `const int*`；失败返回 `nullptr`。
             */
            coeffs = sws_getCoefficients((frame->height >= 720) ? SWS_CS_ITU709 : SWS_CS_ITU601);
        }

        /**
         * @brief 设置 swscale 的色彩空间与色彩范围转换细节。
         * @param[in,out] c 传入 `m_swsContext`。
         * @param[in] inv_table 源色彩矩阵，传入 `coeffs`。
         * @param[in] srcRange 源范围，0=limited(MPEG), 1=full(JPEG)。
         * @param[in] table 目标色彩矩阵，传入 `coeffs`（本项目保持同矩阵）。
         * @param[in] dstRange 目标范围，这里固定 0（limited range）。
         * @param[in] brightness 亮度调节，传入 0（不调节）。
         * @param[in] contrast 对比度调节，传入 0（不调节）。
         * @param[in] saturation 饱和度调节，传入 0（不调节）。
         * @retval >=0 设置成功。
         * @retval <0 设置失败。
         */
        if (sws_setColorspaceDetails(m_swsContext, coeffs, srcRange, coeffs, dstRange, 0, 0, 0) < 0)
        {
            errorString = "设置缩放色彩空间参数失败";
            return nullptr;
        }

        FFmpegUtils::AvFramePtr scaledFrame = FFmpegUtils::createAvFrame(targetWidth, targetHeight, targetFormat);
        if (!scaledFrame)
        {
            errorString = "无法创建缩放后的帧";
            return nullptr;
        }

        /**
         * @brief 执行实际缩放/像素格式转换。
         * @param[in] c 传入 `m_swsContext`。
         * @param[in] srcSlice 源平面指针数组，传入 `frame->data`。
         * @param[in] srcStride 源行跨度数组，传入 `frame->linesize`。
         * @param[in] srcSliceY 起始扫描行，传入 0。
         * @param[in] srcSliceH 参与转换的源高度，传入 `frame->height`。
         * @param[out] dst 目标平面指针数组，传入 `scaledFrame->data`。
         * @param[out] dstStride 目标行跨度数组，传入 `scaledFrame->linesize`。
         * @return 成功返回输出的行数（>0）；失败返回 <=0。
         */
        int result = sws_scale(
            m_swsContext, frame->data, frame->linesize, 0, frame->height, scaledFrame->data, scaledFrame->linesize);
        if (result <= 0)
        {
            errorString = "缩放失败";
            return nullptr;
        }

        const bool useBt709 = frame->height >= 720;
        scaledFrame->colorspace = static_cast<AVColorSpace>(colorspace);
        scaledFrame->color_range = AVCOL_RANGE_MPEG;
        scaledFrame->color_primaries = useBt709 ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
        scaledFrame->color_trc = useBt709 ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M;
        scaledFrame->sample_aspect_ratio = AVRational{1, 1};

        return scaledFrame;
    }

    /**
     * @brief 释放内部 sws 上下文。
     */
    void reset()
    {
        if (m_swsContext)
        {
            sws_freeContext(m_swsContext);
            m_swsContext = nullptr;
        }
    }

  private:
    SwsContext* m_swsContext;
};

} // namespace VideoCreator

#endif // FRAME_SCALER_H
