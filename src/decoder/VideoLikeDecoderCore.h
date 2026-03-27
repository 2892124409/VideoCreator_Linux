#ifndef VIDEO_LIKE_DECODER_CORE_H
#define VIDEO_LIKE_DECODER_CORE_H

#include <cstdint>
#include <string>

#include "ffmpeg_utils/AvFrameWrapper.h"
#include "ffmpeg_utils/AvPacketWrapper.h"
#include "ffmpeg_utils/FFmpegHeaders.h"

/**
 * @file VideoLikeDecoderCore.h
 * @brief 提供图片/视频共用的视频流解码核心逻辑。
 */

namespace VideoCreator
{
/**
 * @brief 可复用的视频流解码核心。
 *
 * @details
 * 该类统一封装“视频类输入”（普通视频文件 + 静态图片）的解码公共流程：
 * - 打开输入并探测流信息（demux 阶段）；
 * - 找到最佳视频流并创建/打开对应解码器；
 * - 使用 avcodec_send_packet / avcodec_receive_frame 循环取帧；
 * - 对外提供宽高、像素格式、时基、时长、帧率等元信息读取接口。
 *
 * 设计目标是让 ImageDecoder / VideoDecoder 复用同一套底层解码逻辑，
 * 避免重复维护相同的 FFmpeg 开流和解码代码。
 */
class VideoLikeDecoderCore
{
  public:
    /**
     * @brief 构造函数，初始化为“未打开”状态。
     * @details
     * AV_NOPTS_VALUE 表示无效时间戳
     */
    VideoLikeDecoderCore()
        : m_formatContext(nullptr)
        , m_codecContext(nullptr)
        , m_videoStreamIndex(-1)
        , m_timeBase{1, 1}
        , m_duration(AV_NOPTS_VALUE)
        , m_frameRate(0.0)
    {
    }

    /**
     * @brief 析构函数，自动释放所有 FFmpeg 资源。
     */
    ~VideoLikeDecoderCore()
    {
        cleanup();
    }

    VideoLikeDecoderCore(const VideoLikeDecoderCore&) = delete;
    VideoLikeDecoderCore& operator=(const VideoLikeDecoderCore&) = delete;

    /**
     * @brief 打开输入文件并初始化视频解码器。
     * @param filePath 视频/图片路径。
     * @param errorString 错误信息输出。
     * @return 成功返回 true。
     * @details
     * 主要步骤：
     * 1) avformat_open_input 打开容器；
     * 2) avformat_find_stream_info 读取流元数据；
     * 3) av_find_best_stream 定位视频流；
     * 4) avcodec_find_decoder + avcodec_alloc_context3 创建解码器；
     * 5) avcodec_parameters_to_context / avcodec_open2 完成解码器初始化；
     * 6) 缓存时间基、时长、帧率供上层查询。
     */
    bool open(const std::string& filePath, std::string& errorString)
    {
        cleanup();

        /**
         * @brief 打开输入媒体并创建格式上下文。
         * @param[in,out] ps 传入 `&m_formatContext`；成功后由 FFmpeg 分配并填充。
         * @param[in] url 传入 `filePath.c_str()`，即媒体文件路径。
         * @param[in] fmt 传入 `nullptr`，表示自动探测容器格式。
         * @param[in] options 传入 `nullptr`，表示使用默认打开参数。
         * @retval 0 打开成功。
         * @retval <0 打开失败（负的 AVERROR 错误码）。
         */
        if (avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, nullptr) < 0)
        {
            errorString = "无法打开视频文件: " + filePath;
            return false;
        }

        /**
         * @brief 读取并分析流信息（时长、码流参数、流列表等）。
         * @param[in,out] ic 传入 `m_formatContext`，即已打开的输入上下文。
         * @param[in] options 传入 `nullptr`，表示不设置额外探测选项。
         * @retval >=0 成功，流信息可用。
         * @retval <0 失败，无法继续定位视频流。
         */
        if (avformat_find_stream_info(m_formatContext, nullptr) < 0)
        {
            errorString = "无法获取视频流信息";
            cleanup();
            return false;
        }

        /**
         * @brief 在输入中选择“最合适”的视频流索引。
         * @param[in] ic 传入 `m_formatContext`。
         * @param[in] type 传入 `AVMEDIA_TYPE_VIDEO`，只查找视频流。
         * @param[in] wanted_stream_nb 传入 `-1`，表示无指定首选流。
         * @param[in] related_stream 传入 `-1`，表示无关联流约束。
         * @param[out] decoder_ret 传入 `nullptr`，此处不直接返回解码器指针。
         * @param[in] flags 传入 `0`，使用默认策略。
         * @retval >=0 成功，返回视频流索引。
         * @retval <0 失败，未找到可用视频流。
         */
        m_videoStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (m_videoStreamIndex < 0)
        {
            errorString = "未找到视频流";
            cleanup();
            return false;
        }

        // 取出上一步选中的最合适的流(视频流)
        AVStream* videoStream = m_formatContext->streams[m_videoStreamIndex];

        /**
         * @brief 根据 codec_id 查找对应解码器实现。
         * @param[in] id 传入 `videoStream->codecpar->codec_id`。
         * @return 成功返回 `const AVCodec*`；失败返回 `nullptr`。
         */
        const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
        if (!codec)
        {
            errorString = "未找到视频解码器";
            cleanup();
            return false;
        }

        /**
         * @brief 为指定解码器分配解码上下文。
         * @param[in] codec 传入上一步查到的 `codec`。
         * @return 成功返回 `AVCodecContext*`；失败返回 `nullptr`。
         */
        m_codecContext = avcodec_alloc_context3(codec);
        if (!m_codecContext)
        {
            errorString = "无法创建视频解码器上下文";
            cleanup();
            return false;
        }

        /**
         * @brief 将流参数复制到解码上下文。
         * @param[out] codec 传入 `m_codecContext`，用于接收参数。
         * @param[in] par 传入 `videoStream->codecpar`(解码前就能知道的元数据)
         * @retval >=0 复制成功。
         * @retval <0 复制失败。
         */
        if (avcodec_parameters_to_context(m_codecContext, videoStream->codecpar) < 0)
        {
            errorString = "无法复制视频参数";
            cleanup();
            return false;
        }

        /**
         * @brief 打开解码器，使 `m_codecContext` 进入可解码状态。
         * @param[in,out] avctx 传入 `m_codecContext`。
         * @param[in] codec 传入 `codec`（也可传 nullptr 让 FFmpeg 从上下文推导）。
         * @param[in] options 传入 `nullptr`，使用默认解码器参数。
         * @retval >=0 打开成功。
         * @retval <0 打开失败。
         */
        if (avcodec_open2(m_codecContext, codec, nullptr) < 0)
        {
            errorString = "无法打开视频解码器";
            cleanup();
            return false;
        }

        // 保存这路视频流的时间基(时间单位)
        m_timeBase = videoStream->time_base;

        // 优先使用视频流自身时长,否则降级到容器总时长(文件整体时长/所有流中结束最晚的一路)
        m_duration = (videoStream->duration != AV_NOPTS_VALUE) ? videoStream->duration : m_formatContext->duration;

        /**
         * @brief 估算视频帧率（优先于直接读取 avg_frame_rate）。
         * @param[in] fmt_ctx 传入 `m_formatContext`。
         * @param[in] stream 传入 `videoStream`。
         * @param[in] frame 传入 `nullptr`，表示不基于指定帧推断。
         * @return 返回 `AVRational`；若 `num/den <= 0` 说明估算不可用。
         */
        AVRational guess = av_guess_frame_rate(m_formatContext, videoStream, nullptr);

        /**
         * @brief 将 AVRational 转成 double。
         * @param[in] a 传入分数形式值（如帧率或时间基倒数）。
         * @return 返回 `a.num / a.den` 的双精度值。
         */
        if (guess.num > 0 && guess.den > 0)
        {
            m_frameRate = av_q2d(guess);
        }
        else if (videoStream->avg_frame_rate.num > 0 && videoStream->avg_frame_rate.den > 0)
        {
            m_frameRate = av_q2d(videoStream->avg_frame_rate);
        }
        else
        {
            m_frameRate = 0.0;
        }

        return true;
    }

    /**
     * @brief 解码下一帧视频。
     * @param frame 输出帧。
     * @param errorString 错误信息输出。
     * @return >0 成功，0 EOF，<0 失败。
     * @details
     * 该函数实现 FFmpeg 推荐的“send/receive 拉取式”循环：
     * - 先调用 avcodec_receive_frame 尝试直接取出已解码帧；
     * - 若返回 EAGAIN，再从容器读取 packet 并 send 给解码器；
     * - 遇到输入结束时发送空 packet 触发 flush；
     * - 最终返回一帧、EOF 或错误码。
     */
    int decodeFrame(FFmpegUtils::AvFramePtr& frame, std::string& errorString)
    {
        if (!m_formatContext || !m_codecContext || m_videoStreamIndex < 0)
        {
            errorString = "视频解码器未初始化";
            return -1;
        }

        // 用来装还未解码的压缩内容
        FFmpegUtils::AvPacketPtr packet = FFmpegUtils::createAvPacket();
        // 接收解码后的原始帧
        FFmpegUtils::AvFramePtr rawFrame = FFmpegUtils::createAvFrame();
        int ret = 0;

        while (true)
        {
            /**
             * @brief 从解码器取出一帧已解码数据。
             * @param[in,out] avctx 传入 `m_codecContext`（已打开的解码器上下文）。
             * @param[out] frame 传入 `rawFrame.get()`，用于接收解码输出。
             * @retval 0 成功取到一帧。
             * @retval AVERROR(EAGAIN) 需要先继续送入更多 packet。
             * @retval AVERROR_EOF 解码器已完全结束，无更多帧。
             * @retval <0 其他负值表示错误。
             */
            ret = avcodec_receive_frame(m_codecContext, rawFrame.get());
            if (ret == 0)
            {
                frame = FFmpegUtils::copyAvFrame(rawFrame.get());
                return frame ? 1 : -1;
            }
            if (ret == AVERROR_EOF)
            {
                return 0;
            }
            if (ret != AVERROR(EAGAIN))
            {
                errorString = "从视频解码器获取帧失败";
                return -1;
            }

            while (true)
            {
                /**
                 * @brief 从容器读取下一个压缩 packet。
                 * @param[in] s 传入 `m_formatContext`（输入容器上下文）。
                 * @param[out] pkt 传入 `packet.get()`，用于接收读取到的数据包。
                 * @retval >=0 成功读到一个 packet。
                 * @retval <0 到达输入末尾或读取失败（需结合上下文处理）。
                 */
                ret = av_read_frame(m_formatContext, packet.get());
                if (ret < 0)
                {
                    /**
                     * @brief 向解码器发送“空包”以触发 flush。
                     * @param[in,out] avctx 传入 `m_codecContext`。
                     * @param[in] avpkt 传入 `nullptr`，表示输入结束，要求解码器吐出缓存帧。
                     * @retval >=0 发送成功。
                     * @retval AVERROR(EAGAIN) 当前状态暂不可继续发送。
                     * @retval <0 发送失败。
                     */
                    avcodec_send_packet(m_codecContext, nullptr);
                    break;
                }

                if (packet->stream_index == m_videoStreamIndex)
                {
                    /**
                     * @brief 向视频解码器发送一个压缩包。
                     * @param[in,out] avctx 传入 `m_codecContext`。
                     * @param[in] avpkt 传入 `packet.get()`。
                     * @retval >=0 发送成功。
                     * @retval AVERROR(EAGAIN) 需先 receive 再继续 send。
                     * @retval AVERROR_EOF 解码器已结束，不再接受输入。
                     * @retval <0 其他负值表示发送失败。
                     */
                    ret = avcodec_send_packet(m_codecContext, packet.get());

                    /**
                     * @brief 释放 packet 内部引用的数据，但保留 packet 对象本身可复用。
                     * @param[in,out] pkt 传入 `packet.get()`。
                     * @return 无返回值（void）。
                     */
                    av_packet_unref(packet.get());
                    if (ret < 0)
                    {
                        errorString = "发送视频包失败";
                        return -1;
                    }
                    break;
                }

                /**
                 * @brief 非目标流（如音频/字幕）也要及时 unref，避免 packet 累积占用内存。
                 * @param[in,out] pkt 传入 `packet.get()`。
                 * @return 无返回值（void）。
                 */
                av_packet_unref(packet.get());
            }
        }
    }

    /**
     * @brief 关闭并清理资源。
     */
    void close()
    {
        cleanup();
    }

    /**
     * @brief 是否已经成功打开。
     * @return 当 format/codec context 和流索引都有效时返回 true。
     */
    bool isOpen() const
    {
        return m_formatContext != nullptr && m_codecContext != nullptr && m_videoStreamIndex >= 0;
    }

    /**
     * @brief 获取源视频宽度（像素）。
     */
    int width() const
    {
        return m_codecContext ? m_codecContext->width : 0;
    }

    /**
     * @brief 获取源视频高度（像素）。
     */
    int height() const
    {
        return m_codecContext ? m_codecContext->height : 0;
    }

    /**
     * @brief 获取源视频像素格式。
     */
    AVPixelFormat pixelFormat() const
    {
        return m_codecContext ? m_codecContext->pix_fmt : AV_PIX_FMT_NONE;
    }

    /**
     * @brief 获取时间基（time base）。
     * @details 常用于把 duration/pts 从“时间戳单位”换算为秒。
     */
    AVRational timeBase() const
    {
        return m_timeBase;
    }

    /**
     * @brief 获取时长（以 time base 为单位）。
     */
    int64_t duration() const
    {
        return m_duration;
    }

    /**
     * @brief 获取估算帧率。
     */
    double frameRate() const
    {
        return m_frameRate;
    }

  private:
    /**
     * @brief 释放 FFmpeg 资源并重置状态字段。
     */
    void cleanup()
    {
        // 视频解码上下文
        if (m_codecContext)
        {
            avcodec_free_context(&m_codecContext);
            m_codecContext = nullptr;
        }
        // 输入封装上下文
        if (m_formatContext)
        {
            avformat_close_input(&m_formatContext);
            m_formatContext = nullptr;
        }

        m_videoStreamIndex = -1;
        m_timeBase = AVRational{1, 1};
        m_duration = AV_NOPTS_VALUE;
        m_frameRate = 0.0;
    }

  private:
    /**
     * @brief 输入封装上下文（demux 层）。
     * @details
     * 由 avformat_open_input 创建，持有容器/流级别信息。
     * 负责从媒体文件中读取压缩 packet（av_read_frame）。
     */
    AVFormatContext* m_formatContext;

    /**
     * @brief 视频解码器上下文（codec 层）。
     * @details
     * 由 avcodec_alloc_context3 创建并通过 avcodec_open2 打开。
     * 保存解码参数并执行 packet -> frame 的解码过程。
     */
    AVCodecContext* m_codecContext;

    /**
     * @brief 当前输入中的目标视频流索引。
     * @details
     * 由 av_find_best_stream 得到。
     * av_read_frame 读取到的 packet 只有 stream_index 匹配该值时才送入视频解码器。
     */
    int m_videoStreamIndex;

    /**
     * @brief 视频流时间基（单位刻度）。
     * @details
     * 典型用途：seconds = timestamp * av_q2d(m_timeBase)。
     */
    AVRational m_timeBase;

    /**
     * @brief 输入时长（时间戳单位，而不是秒）。
     * @details
     * 优先取 stream->duration；若不可用则回退为 formatContext->duration。
     * 上层可结合 timeBase 转换为秒。
     */
    int64_t m_duration;

    /**
     * @brief 估算帧率（fps）。
     * @details
     * 优先使用 av_guess_frame_rate，失败时回退 avg_frame_rate。
     */
    double m_frameRate;
};

} // namespace VideoCreator

#endif // VIDEO_LIKE_DECODER_CORE_H
