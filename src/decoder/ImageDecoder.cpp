#include "ImageDecoder.h"
#include <iostream>

/**
 * @file ImageDecoder.cpp
 * @brief 实现图片解码与缩放转换逻辑。
 */

namespace VideoCreator
{
/**
 * @brief 构造函数，初始化成员为空状态。
 */
ImageDecoder::ImageDecoder()
    : m_width(0)
    , m_height(0)
    , m_pixelFormat(AV_PIX_FMT_NONE)
    , m_cachedFrame(nullptr)
{
}

ImageDecoder::~ImageDecoder()
{
    cleanup();
}

/**
 * @brief 打开图片并初始化 FFmpeg 解码器。
 */
bool ImageDecoder::open(const std::string& filePath)
{
    std::cerr << "打开图片文件: " << filePath.c_str();

    if (!m_decoderCore.open(filePath, m_errorString))
    {
        std::cerr << "打开图片文件失败: " << m_errorString.c_str();
        cleanup();
        return false;
    }

    // 保存图片信息（图片在 FFmpeg 里本质上是单帧视频流）
    m_width = m_decoderCore.width();
    m_height = m_decoderCore.height();
    m_pixelFormat = m_decoderCore.pixelFormat();

    std::cerr << "图片解码器初始化成功 - 尺寸: " << m_width << "x" << m_height << " 格式: " << m_pixelFormat;

    return true;
}

/**
 * @brief 解码图片帧。
 *
 * 对静态图片通常只会得到一帧，函数同时处理 EOF 刷新逻辑，
 * 以兼容不同 demuxer/decoder 的行为差异。
 */
FFmpegUtils::AvFramePtr ImageDecoder::decode()
{
    if (!m_decoderCore.isOpen())
    {
        m_errorString = "解码器未打开";
        return nullptr;
    }

    FFmpegUtils::AvFramePtr frame;
    int decodeResult = m_decoderCore.decodeFrame(frame, m_errorString);
    if (decodeResult > 0)
    {
        std::cerr << "成功解码图片帧 - 尺寸: " << frame->width << "x" << frame->height << " 格式: " << frame->format;
        return frame;
    }

    if (decodeResult == 0)
    {
        std::cerr << "图片解码器刷新完成，无更多帧";
        return nullptr;
    }

    return nullptr;
}

/**
 * @brief 读取并缓存首帧，后续返回缓存副本。
 */
FFmpegUtils::AvFramePtr ImageDecoder::decodeAndCache()
{
    // 缓存第一帧，避免重复解码
    if (m_cachedFrame)
    {
        std::cerr << "使用缓存的图片帧";
        return FFmpegUtils::copyAvFrame(m_cachedFrame.get());
    }

    m_cachedFrame = decode();
    return FFmpegUtils::copyAvFrame(m_cachedFrame.get());
}

/**
 * @brief 关闭解码器。
 */
void ImageDecoder::close()
{
    cleanup();
}

/**
 * @brief 缩放并转换像素格式，同时显式控制色彩空间参数。
 *
 * 该函数会将输入帧色彩元数据映射到 swscale 转换参数，避免
 * 由于隐式推断导致的偏色问题。
 */
FFmpegUtils::AvFramePtr ImageDecoder::scaleToSize(
    FFmpegUtils::AvFramePtr& frame, int targetWidth, int targetHeight, AVPixelFormat targetFormat)
{
    return m_frameScaler.scale(frame.get(), targetWidth, targetHeight, targetFormat, m_errorString);
}

/**
 * @brief 清理内部 FFmpeg 资源和缓存状态。
 */
void ImageDecoder::cleanup()
{
    m_cachedFrame.reset(); // 清除缓存

    m_frameScaler.reset();
    m_decoderCore.close();

    m_width = 0;
    m_height = 0;
    m_pixelFormat = AV_PIX_FMT_NONE;
}
} // namespace VideoCreator
