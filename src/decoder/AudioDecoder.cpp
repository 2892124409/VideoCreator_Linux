#include "AudioDecoder.h"
#include "ffmpeg_utils/AvPacketWrapper.h"
#include <cmath>
#include <sstream>

/**
 * @file AudioDecoder.cpp
 * @brief 实现音频解码、重采样及音量滤镜处理。
 */

namespace VideoCreator
{
/**
 * @brief 构造函数，初始化所有句柄为空。
 */
AudioDecoder::AudioDecoder()
    : m_formatContext(nullptr)
    , m_codecContext(nullptr)
    , m_audioStreamIndex(-1)
    , m_swrCtx(nullptr)
    , m_filterGraph(nullptr)
    , m_bufferSrcCtx(nullptr)
    , m_bufferSinkCtx(nullptr)
    , m_effectsEnabled(false)
    , m_sampleRate(0)
    , m_channels(0)
    , m_sampleFormat(AV_SAMPLE_FMT_NONE)
    , m_duration(0)
{
}

AudioDecoder::~AudioDecoder()
{
    cleanup();
}

/**
 * @brief 打开音频输入并初始化解码器与重采样器。
 */
bool AudioDecoder::open(const std::string& filePath)
{
    /**
     * @brief 打开输入媒体并创建格式上下文。
     * @param[in,out] ps 传入 `&m_formatContext`，成功后由 FFmpeg 分配并写回。
     * @param[in] url 传入 `filePath.c_str()`，即音频文件路径。
     * @param[in] fmt 传入 `nullptr`，自动探测容器格式。
     * @param[in] options 传入 `nullptr`，使用默认参数。
     * @retval 0 成功。
     * @retval <0 失败（负的 AVERROR 码）。
     */
    if (avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, nullptr) < 0)
    {
        m_errorString = "无法打开音频文件: " + filePath;
        return false;
    }

    /**
     * @brief 读取容器流信息（流列表、时长、编码参数等）。
     * @param[in,out] ic 传入 `m_formatContext`。
     * @param[in] options 传入 `nullptr`，表示无额外探测选项。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    if (avformat_find_stream_info(m_formatContext, nullptr) < 0)
    {
        m_errorString = "无法获取流信息";
        cleanup();
        return false;
    }

    /**
     * @brief 选择“最合适”的音频流索引。
     * @param[in] ic 传入 `m_formatContext`。
     * @param[in] type 传入 `AVMEDIA_TYPE_AUDIO`，仅查找音频流。
     * @param[in] wanted_stream_nb 传入 `-1`，不指定首选流。
     * @param[in] related_stream 传入 `-1`，不指定关联流。
     * @param[out] decoder_ret 传入 `nullptr`，此处不返回解码器指针。
     * @param[in] flags 传入 `0`，默认策略。
     * @retval >=0 返回音频流索引。
     * @retval <0 未找到可用音频流。
     */
    m_audioStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_audioStreamIndex < 0)
    {
        m_errorString = "未找到音频流";
        cleanup();
        return false;
    }

    // 获取音频流
    AVStream* audioStream = m_formatContext->streams[m_audioStreamIndex];

    /**
     * @brief 根据 codec_id 查找对应音频解码器。
     * @param[in] id 传入 `audioStream->codecpar->codec_id`。
     * @return 成功返回 `const AVCodec*`，失败返回 `nullptr`。
     */
    const AVCodec* codec = avcodec_find_decoder(audioStream->codecpar->codec_id);
    if (!codec)
    {
        m_errorString = "未找到解码器";
        cleanup();
        return false;
    }

    /**
     * @brief 为解码器分配上下文对象。
     * @param[in] codec 传入上一步找到的 `codec`。
     * @return 成功返回 `AVCodecContext*`，失败返回 `nullptr`。
     */
    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext)
    {
        m_errorString = "无法创建解码器上下文";
        cleanup();
        return false;
    }

    /**
     * @brief 将流参数复制到解码器上下文。
     * @param[out] codec 传入 `m_codecContext`，用于接收参数。
     * @param[in] par 传入 `audioStream->codecpar`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    if (avcodec_parameters_to_context(m_codecContext, audioStream->codecpar) < 0)
    {
        m_errorString = "无法复制解码器参数";
        cleanup();
        return false;
    }

    /**
     * @brief 打开音频解码器。
     * @param[in,out] avctx 传入 `m_codecContext`。
     * @param[in] codec 传入 `codec`。
     * @param[in] options 传入 `nullptr`，默认解码参数。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    if (avcodec_open2(m_codecContext, codec, nullptr) < 0)
    {
        m_errorString = "无法打开解码器";
        cleanup();
        return false;
    }

    // 保存音频信息
    m_sampleRate = m_codecContext->sample_rate;
    m_channels = m_codecContext->ch_layout.nb_channels > 0 ? m_codecContext->ch_layout.nb_channels : 2;
    m_sampleFormat = m_codecContext->sample_fmt;
    m_duration = audioStream->duration;

    /**
     * @brief 分配重采样上下文 SwrContext。
     * @return 成功返回 `SwrContext*`，失败返回 `nullptr`。
     */
    m_swrCtx = swr_alloc();
    if (!m_swrCtx)
    {
        m_errorString = "无法分配 SwrContext";
        cleanup();
        return false;
    }

    AVChannelLayout in_ch_layout, out_ch_layout;

    /**
     * @brief 按声道数生成默认声道布局。
     * @param[out] ch_layout 输出布局对象。
     * @param[in] nb_channels 声道数。
     * @return 无返回值（void）。
     */
    av_channel_layout_default(&in_ch_layout, m_codecContext->ch_layout.nb_channels);

    /**
     * @brief 按声道数生成默认声道布局。
     * @param[out] ch_layout 输出布局对象。
     * @param[in] nb_channels 这里传 `2`，目标立体声。
     * @return 无返回值（void）。
     */
    av_channel_layout_default(&out_ch_layout, 2);

    /**
     * @brief 为 SwrContext 设置输入声道布局。
     * @param[in,out] obj 传入 `m_swrCtx`。
     * @param[in] name 传入 `"in_chlayout"`。
     * @param[in] ch_layout 传入 `&in_ch_layout`。
     * @param[in] search_flags 传入 `0`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    av_opt_set_chlayout(m_swrCtx, "in_chlayout", &in_ch_layout, 0);

    /**
     * @brief 为 SwrContext 设置输入采样率。
     * @param[in,out] obj 传入 `m_swrCtx`。
     * @param[in] name 传入 `"in_sample_rate"`。
     * @param[in] val 传入 `m_sampleRate`。
     * @param[in] search_flags 传入 `0`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    av_opt_set_int(m_swrCtx, "in_sample_rate", m_sampleRate, 0);

    /**
     * @brief 为 SwrContext 设置输入采样格式。
     * @param[in,out] obj 传入 `m_swrCtx`。
     * @param[in] name 传入 `"in_sample_fmt"`。
     * @param[in] fmt 传入 `m_sampleFormat`。
     * @param[in] search_flags 传入 `0`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    av_opt_set_sample_fmt(m_swrCtx, "in_sample_fmt", m_sampleFormat, 0);

    /**
     * @brief 为 SwrContext 设置输出声道布局（立体声）。
     * @param[in,out] obj 传入 `m_swrCtx`。
     * @param[in] name 传入 `"out_chlayout"`。
     * @param[in] ch_layout 传入 `&out_ch_layout`。
     * @param[in] search_flags 传入 `0`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    av_opt_set_chlayout(m_swrCtx, "out_chlayout", &out_ch_layout, 0);

    /**
     * @brief 为 SwrContext 设置输出采样率。
     * @param[in,out] obj 传入 `m_swrCtx`。
     * @param[in] name 传入 `"out_sample_rate"`。
     * @param[in] val 传入 `44100`。
     * @param[in] search_flags 传入 `0`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    av_opt_set_int(m_swrCtx, "out_sample_rate", 44100, 0);

    /**
     * @brief 为 SwrContext 设置输出采样格式。
     * @param[in,out] obj 传入 `m_swrCtx`。
     * @param[in] name 传入 `"out_sample_fmt"`。
     * @param[in] fmt 传入 `AV_SAMPLE_FMT_FLTP`。
     * @param[in] search_flags 传入 `0`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    av_opt_set_sample_fmt(m_swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

    /**
     * @brief 释放声道布局结构体内部资源。
     * @param[in,out] ch_layout 传入 `&in_ch_layout`。
     * @return 无返回值（void）。
     */
    av_channel_layout_uninit(&in_ch_layout);

    /**
     * @brief 释放声道布局结构体内部资源。
     * @param[in,out] ch_layout 传入 `&out_ch_layout`。
     * @return 无返回值（void）。
     */
    av_channel_layout_uninit(&out_ch_layout);

    /**
     * @brief 初始化重采样上下文，使参数生效并可执行转换。
     * @param[in,out] s 传入 `m_swrCtx`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    if (swr_init(m_swrCtx) < 0)
    {
        m_errorString = "无法初始化 SwrContext";

        /**
         * @brief 释放重采样上下文并将指针置空。
         * @param[in,out] s 传入 `&m_swrCtx`。
         * @return 无返回值（void）。
         */
        swr_free(&m_swrCtx);
        cleanup();
        return false;
    }

    return true;
}

/**
 * @brief 按场景配置应用音量效果。
 */
bool AudioDecoder::applyVolumeEffect(const SceneConfig& sceneConfig)
{
    double trackDuration = sceneConfig.duration > 0 ? sceneConfig.duration : getDuration();
    const VolumeMixEffect* effect = sceneConfig.effects.volume_mix.enabled ? &sceneConfig.effects.volume_mix : nullptr;
    return applyVolumeEffect(sceneConfig.resources.audio.volume, effect, trackDuration);
}

/**
 * @brief 根据显式参数启用音量滤镜链。
 */
bool AudioDecoder::applyVolumeEffect(double baseVolume, const VolumeMixEffect* effect, double trackDurationSeconds)
{
    const bool effectEnabled = effect && effect->enabled;
    const bool volumeEnabled = std::abs(baseVolume - 1.0) > 1e-3;
    m_effectsEnabled = effectEnabled || volumeEnabled;
    if (m_effectsEnabled)
    {
        return initFilterGraph(baseVolume, effectEnabled ? effect : nullptr, trackDurationSeconds);
    }
    return true;
}

/**
 * @brief 初始化音频滤镜图（afade + volume）。
 *
 * 该滤镜图作用于重采样后的统一格式音频，保证后续混音侧行为一致。
 */
bool AudioDecoder::initFilterGraph(double baseVolume, const VolumeMixEffect* effect, double trackDurationSeconds)
{
    if (m_filterGraph)
    {
        /**
         * @brief 释放现有滤镜图及其内部节点。
         * @param[in,out] graph 传入 `&m_filterGraph`。
         * @return 无返回值（void）。
         */
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
        m_bufferSrcCtx = nullptr;
        m_bufferSinkCtx = nullptr;
    }

    /**
     * @brief 分配新的音频滤镜图对象。
     * @return 成功返回 `AVFilterGraph*`，失败返回 `nullptr`。
     */
    m_filterGraph = avfilter_graph_alloc();
    if (!m_filterGraph)
    {
        m_errorString = "Failed to allocate filter graph";
        return false;
    }

    /**
     * @brief 通过名字获取滤镜定义对象。
     * @param[in] name 这里传 `"abuffer"`，表示音频源滤镜。
     * @return 成功返回 `AVFilter*`，失败返回 `nullptr`。
     */
    const AVFilter* abuffer_src = avfilter_get_by_name("abuffer");

    /**
     * @brief 通过名字获取滤镜定义对象。
     * @param[in] name 这里传 `"abuffersink"`，表示音频输出滤镜。
     * @return 成功返回 `AVFilter*`，失败返回 `nullptr`。
     */
    const AVFilter* abuffer_sink = avfilter_get_by_name("abuffersink");

    AVChannelLayout out_ch_layout;
    int64_t out_sample_rate;

    /**
     * @brief 从 SwrContext 读取输出声道布局参数。
     * @param[in] obj 传入 `m_swrCtx`。
     * @param[in] name 传入 `"out_chlayout"`。
     * @param[in] search_flags 传入 `0`。
     * @param[out] ch_layout 输出到 `&out_ch_layout`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    av_opt_get_chlayout(m_swrCtx, "out_chlayout", 0, &out_ch_layout);

    /**
     * @brief 从 SwrContext 读取输出采样率参数。
     * @param[in] obj 传入 `m_swrCtx`。
     * @param[in] name 传入 `"out_sample_rate"`。
     * @param[in] search_flags 传入 `0`。
     * @param[out] out_val 输出到 `&out_sample_rate`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    av_opt_get_int(m_swrCtx, "out_sample_rate", 0, &out_sample_rate);

    std::stringstream args;

    /**
     * @brief 获取采样格式名称字符串。
     * @param[in] sample_fmt 传入 `AV_SAMPLE_FMT_FLTP`。
     * @return 成功返回格式名字符串指针，失败返回 `nullptr`。
     */
    args << "time_base=1/" << out_sample_rate << ":sample_rate=" << out_sample_rate
         << ":sample_fmt=" << av_get_sample_fmt_name(AV_SAMPLE_FMT_FLTP) << ":channel_layout=" << out_ch_layout.u.mask;

    /**
     * @brief 在滤镜图中创建具体滤镜实例节点。
     * @param[out] filt_ctx 传入 `&m_bufferSrcCtx`，接收创建出的节点。
     * @param[in] filt 传入 `abuffer_src`。
     * @param[in] name 节点名，传 `"in"`。
     * @param[in] args 参数字符串，传入 `args.str().c_str()`。
     * @param[in] opaque 传入 `nullptr`。
     * @param[in] graph_ctx 传入 `m_filterGraph`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    int ret =
        avfilter_graph_create_filter(&m_bufferSrcCtx, abuffer_src, "in", args.str().c_str(), nullptr, m_filterGraph);
    if (ret < 0)
    {
        m_errorString = "Failed to create source filter";

        /**
         * @brief 释放声道布局结构体内部资源。
         * @param[in,out] ch_layout 传入 `&out_ch_layout`。
         * @return 无返回值（void）。
         */
        av_channel_layout_uninit(&out_ch_layout);
        return false;
    }

    /**
     * @brief 释放声道布局结构体内部资源。
     * @param[in,out] ch_layout 传入 `&out_ch_layout`。
     * @return 无返回值（void）。
     */
    av_channel_layout_uninit(&out_ch_layout);

    /**
     * @brief 在滤镜图中创建具体滤镜实例节点。
     * @param[out] filt_ctx 传入 `&m_bufferSinkCtx`，接收创建出的节点。
     * @param[in] filt 传入 `abuffer_sink`。
     * @param[in] name 节点名，传 `"out"`。
     * @param[in] args 传 `nullptr`（无额外参数）。
     * @param[in] opaque 传 `nullptr`。
     * @param[in] graph_ctx 传入 `m_filterGraph`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    ret = avfilter_graph_create_filter(&m_bufferSinkCtx, abuffer_sink, "out", nullptr, nullptr, m_filterGraph);
    if (ret < 0)
    {
        m_errorString = "Failed to create sink filter";
        return false;
    }

    std::stringstream filter_spec;
    if (effect && effect->enabled)
    {
        if (effect->fade_in > 0)
        {
            filter_spec << "afade=t=in:d=" << effect->fade_in << ",";
        }
        if (effect->fade_out > 0)
        {
            double referenceDuration = trackDurationSeconds > 0 ? trackDurationSeconds : getDuration();
            double fadeStart = referenceDuration > effect->fade_out ? (referenceDuration - effect->fade_out) : 0.0;
            filter_spec << "afade=t=out:st=" << fadeStart << ":d=" << effect->fade_out << ",";
        }
    }
    filter_spec << "volume=" << baseVolume;

    /**
     * @brief 分配滤镜连接描述结构（输入/输出端口描述）。
     * @return 成功返回 `AVFilterInOut*`，失败返回 `nullptr`。
     */
    AVFilterInOut* outputs = avfilter_inout_alloc();

    /**
     * @brief 分配滤镜连接描述结构（输入/输出端口描述）。
     * @return 成功返回 `AVFilterInOut*`，失败返回 `nullptr`。
     */
    AVFilterInOut* inputs = avfilter_inout_alloc();
    if (!outputs || !inputs)
    {
        m_errorString = "Failed to allocate filter inout";

        /**
         * @brief 释放 AVFilterInOut 链表。
         * @param[in,out] inout 传入 `&outputs`。
         * @return 无返回值（void）。
         */
        avfilter_inout_free(&outputs);

        /**
         * @brief 释放 AVFilterInOut 链表。
         * @param[in,out] inout 传入 `&inputs`。
         * @return 无返回值（void）。
         */
        avfilter_inout_free(&inputs);
        return false;
    }

    /**
     * @brief 复制 C 字符串并返回堆内存指针。
     * @param[in] s 传入 `"in"`。
     * @return 成功返回新分配字符串；失败返回 `nullptr`。
     */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = m_bufferSrcCtx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    /**
     * @brief 复制 C 字符串并返回堆内存指针。
     * @param[in] s 传入 `"out"`。
     * @return 成功返回新分配字符串；失败返回 `nullptr`。
     */
    inputs->name = av_strdup("out");
    inputs->filter_ctx = m_bufferSinkCtx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    /**
     * @brief 解析滤镜描述字符串并在图中建立节点连接关系。
     * @param[in,out] graph 传入 `m_filterGraph`。
     * @param[in] filters 传入 `filter_spec.str().c_str()`。
     * @param[in,out] inputs 传入 `&inputs`。
     * @param[in,out] outputs 传入 `&outputs`。
     * @param[in] log_ctx 传入 `nullptr`。
     * @retval >=0 解析成功。
     * @retval <0 解析失败。
     */
    ret = avfilter_graph_parse_ptr(m_filterGraph, filter_spec.str().c_str(), &inputs, &outputs, nullptr);
    if (ret < 0)
    {
        m_errorString = "Failed to parse filter chain";

        /**
         * @brief 释放 AVFilterInOut 链表。
         * @param[in,out] inout 传入 `&outputs`。
         * @return 无返回值（void）。
         */
        avfilter_inout_free(&outputs);

        /**
         * @brief 释放 AVFilterInOut 链表。
         * @param[in,out] inout 传入 `&inputs`。
         * @return 无返回值（void）。
         */
        avfilter_inout_free(&inputs);
        return false;
    }

    /**
     * @brief 校验并配置整个滤镜图，使其进入可运行状态。
     * @param[in,out] graphctx 传入 `m_filterGraph`。
     * @param[in] log_ctx 传入 `nullptr`。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    ret = avfilter_graph_config(m_filterGraph, nullptr);
    if (ret < 0)
    {
        m_errorString = "Failed to configure filter graph";
        return false;
    }

    return true;
}

/**
 * @brief 按秒级时间戳 seek 到目标位置。
 */
bool AudioDecoder::seek(double timestamp)
{
    if (!m_formatContext)
        return false;

    /**
     * @brief 将 AVRational 转换为 double。
     * @param[in] a 传入 `m_formatContext->streams[m_audioStreamIndex]->time_base`。
     * @return 返回 `a.num / a.den`。
     */
    int64_t target_ts =
        static_cast<int64_t>(timestamp / av_q2d(m_formatContext->streams[m_audioStreamIndex]->time_base));

    /**
     * @brief 按流时间戳执行 seek。
     * @param[in] s 传入 `m_formatContext`。
     * @param[in] stream_index 传入 `m_audioStreamIndex`。
     * @param[in] timestamp 传入 `target_ts`。
     * @param[in] flags 传入 `AVSEEK_FLAG_BACKWARD`，向后找最近可定位点。
     * @retval >=0 成功。
     * @retval <0 失败。
     */
    return av_seek_frame(m_formatContext, m_audioStreamIndex, target_ts, AVSEEK_FLAG_BACKWARD) >= 0;
}

/**
 * @brief 解码一帧音频并输出 FLTP 44.1kHz 立体声数据。
 *
 * 关键流程：
 * 1. 优先尝试从滤镜图读取（若开启效果）；
 * 2. 从解码器 receive_frame，必要时 read_frame/send_packet 供数；
 * 3. 对原始帧做 swr_convert 重采样；
 * 4. 可选送入滤镜图，再次循环拉取输出。
 */
int AudioDecoder::decodeFrame(FFmpegUtils::AvFramePtr& outFrame)
{
    if (!m_formatContext || !m_codecContext)
    {
        m_errorString = "Decoder not opened";
        return -1;
    }

    auto packet = FFmpegUtils::createAvPacket();
    auto rawFrame = FFmpegUtils::createAvFrame();
    if (!packet || !rawFrame)
    {
        m_errorString = "Failed to allocate decoder resources";
        return -1;
    }

    bool decoderDrained = false;

    while (true)
    {
        if (m_effectsEnabled)
        {
            auto filteredFrame = FFmpegUtils::createAvFrame();
            if (!filteredFrame)
            {
                m_errorString = "Failed to allocate filtered frame";
                return -1;
            }

            /**
             * @brief 从滤镜图 sink 拉取一帧处理后的音频数据。
             * @param[in,out] ctx 传入 `m_bufferSinkCtx`。
             * @param[out] frame 传入 `filteredFrame.get()`。
             * @retval 0 成功取到一帧。
             * @retval AVERROR(EAGAIN) 暂无可取帧，需继续喂数据。
             * @retval AVERROR_EOF 滤镜图输出结束。
             * @retval <0 其他错误。
             */
            int ret = av_buffersink_get_frame(m_bufferSinkCtx, filteredFrame.get());
            if (ret == 0)
            {
                outFrame = std::move(filteredFrame);
                return 1;
            }
            if (ret == AVERROR_EOF && decoderDrained)
            {
                return 0;
            }
            if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            {
                m_errorString = "Failed to pull frame from filter graph";
                return -1;
            }
        }

        /**
         * @brief 从解码器获取一帧已解码 PCM。
         * @param[in,out] avctx 传入 `m_codecContext`。
         * @param[out] frame 传入 `rawFrame.get()`。
         * @retval 0 成功。
         * @retval AVERROR(EAGAIN) 需继续 send packet。
         * @retval AVERROR_EOF 解码结束。
         * @retval <0 其他错误。
         */
        int ret = avcodec_receive_frame(m_codecContext, rawFrame.get());
        if (ret == AVERROR_EOF)
        {
            decoderDrained = true;
            if (m_effectsEnabled)
            {
                /**
                 * @brief 向滤镜图 source 发送空帧，通知输入结束（flush）。
                 * @param[in,out] ctx 传入 `m_bufferSrcCtx`。
                 * @param[in] frame 传入 `nullptr`。
                 * @retval >=0 成功。
                 * @retval <0 失败。
                 */
                if (av_buffersrc_add_frame(m_bufferSrcCtx, nullptr) < 0)
                {
                    m_errorString = "Failed to signal EOF to filter graph";
                    return -1;
                }
                continue;
            }
            return 0;
        }
        if (ret == AVERROR(EAGAIN))
        {
            /**
             * @brief 从容器读取一个压缩 packet。
             * @param[in] s 传入 `m_formatContext`。
             * @param[out] pkt 传入 `packet.get()`。
             * @retval >=0 成功。
             * @retval AVERROR_EOF 输入结束。
             * @retval <0 读取失败。
             */
            ret = av_read_frame(m_formatContext, packet.get());
            if (ret >= 0)
            {
                if (packet->stream_index == m_audioStreamIndex)
                {
                    /**
                     * @brief 向音频解码器发送一个压缩包。
                     * @param[in,out] avctx 传入 `m_codecContext`。
                     * @param[in] avpkt 传入 `packet.get()`。
                     * @retval >=0 成功。
                     * @retval AVERROR(EAGAIN) 需先 receive 再 send。
                     * @retval AVERROR_EOF 解码器已结束。
                     * @retval <0 其他错误。
                     */
                    if (avcodec_send_packet(m_codecContext, packet.get()) < 0)
                    {
                        m_errorString = "Failed to send packet to decoder";
                        return -1;
                    }
                }

                /**
                 * @brief 释放 packet 内部引用数据，保留对象本身复用。
                 * @param[in,out] pkt 传入 `packet.get()`。
                 * @return 无返回值（void）。
                 */
                av_packet_unref(packet.get());
                continue;
            }
            if (ret == AVERROR_EOF)
            {
                /**
                 * @brief 发送空包给解码器，触发解码器 flush。
                 * @param[in,out] avctx 传入 `m_codecContext`。
                 * @param[in] avpkt 传入 `nullptr`。
                 * @retval >=0 成功。
                 * @retval <0 失败。
                 */
                avcodec_send_packet(m_codecContext, nullptr);
                continue;
            }
            char errbuf[AV_ERROR_MAX_STRING_SIZE];

            /**
             * @brief 将 FFmpeg 错误码转成可读字符串。
             * @param[in] errnum 传入 `ret`。
             * @param[out] errbuf 输出缓冲区。
             * @param[in] errbuf_size 缓冲区大小。
             * @retval >=0 成功。
             * @retval <0 失败。
             */
            av_strerror(ret, errbuf, sizeof(errbuf));
            m_errorString = std::string("Failed to read audio data: ") + errbuf;
            return -1;
        }
        if (ret < 0)
        {
            m_errorString = "Failed to receive frame from decoder";
            return -1;
        }

        auto resampled_frame = FFmpegUtils::createAvFrame();
        if (!resampled_frame)
        {
            m_errorString = "Failed to allocate resampled frame";
            return -1;
        }

        AVChannelLayout out_ch_layout;
        int64_t out_sample_rate;
        AVSampleFormat out_sample_fmt;

        /**
         * @brief 读取重采样器的输出声道布局参数。
         * @param[in] obj 传入 `m_swrCtx`。
         * @param[in] name 传入 `"out_chlayout"`。
         * @param[in] search_flags 传入 `0`。
         * @param[out] ch_layout 输出到 `&out_ch_layout`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        av_opt_get_chlayout(m_swrCtx, "out_chlayout", 0, &out_ch_layout);

        /**
         * @brief 读取重采样器的输出采样率参数。
         * @param[in] obj 传入 `m_swrCtx`。
         * @param[in] name 传入 `"out_sample_rate"`。
         * @param[in] search_flags 传入 `0`。
         * @param[out] out_val 输出到 `&out_sample_rate`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        av_opt_get_int(m_swrCtx, "out_sample_rate", 0, &out_sample_rate);

        /**
         * @brief 读取重采样器的输出采样格式参数。
         * @param[in] obj 传入 `m_swrCtx`。
         * @param[in] name 传入 `"out_sample_fmt"`。
         * @param[in] search_flags 传入 `0`。
         * @param[out] sample_fmt 输出到 `&out_sample_fmt`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        av_opt_get_sample_fmt(m_swrCtx, "out_sample_fmt", 0, &out_sample_fmt);

        /**
         * @brief 获取重采样器延迟（以输入采样率为基准的样本数）。
         * @param[in] s 传入 `m_swrCtx`。
         * @param[in] base 传入 `rawFrame->sample_rate`。
         * @return 返回延迟样本数。
         */
        /**
         * @brief 进行整数比例缩放并按规则取整。
         * @param[in] a 这里传 `swr_get_delay(...) + rawFrame->nb_samples`。
         * @param[in] b 这里传 `out_sample_rate`。
         * @param[in] c 这里传 `rawFrame->sample_rate`。
         * @param[in] rnd 这里传 `AV_ROUND_UP`。
         * @return 返回换算后的目标样本数。
         */
        resampled_frame->nb_samples =
            static_cast<int>(av_rescale_rnd(swr_get_delay(m_swrCtx, rawFrame->sample_rate) + rawFrame->nb_samples,
                out_sample_rate,
                rawFrame->sample_rate,
                AV_ROUND_UP));
        resampled_frame->ch_layout = out_ch_layout;
        resampled_frame->format = out_sample_fmt;
        resampled_frame->sample_rate = static_cast<int>(out_sample_rate);

        /**
         * @brief 为音频帧分配数据缓冲区。
         * @param[in,out] frame 传入 `resampled_frame.get()`。
         * @param[in] align 传入 `0`（默认对齐）。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (av_frame_get_buffer(resampled_frame.get(), 0) < 0)
        {
            /**
             * @brief 释放声道布局结构体内部资源。
             * @param[in,out] ch_layout 传入 `&out_ch_layout`。
             * @return 无返回值（void）。
             */
            av_channel_layout_uninit(&out_ch_layout);
            m_errorString = "Failed to allocate buffer for resampled audio";
            return -1;
        }

        /**
         * @brief 执行重采样/重格式转换。
         * @param[in,out] s 传入 `m_swrCtx`。
         * @param[out] out 输出缓冲，传 `resampled_frame->data`。
         * @param[in] out_count 目标最大样本数，传 `resampled_frame->nb_samples`。
         * @param[in] in 输入缓冲，传 `(const uint8_t**)rawFrame->data`。
         * @param[in] in_count 输入样本数，传 `rawFrame->nb_samples`。
         * @return 返回实际输出样本数；<0 表示失败。
         */
        int converted_samples = swr_convert(m_swrCtx,
            resampled_frame->data,
            resampled_frame->nb_samples,
            (const uint8_t**)rawFrame->data,
            rawFrame->nb_samples);

        /**
         * @brief 释放声道布局结构体内部资源。
         * @param[in,out] ch_layout 传入 `&out_ch_layout`。
         * @return 无返回值（void）。
         */
        av_channel_layout_uninit(&out_ch_layout);
        if (converted_samples < 0)
        {
            m_errorString = "swr_convert failed";
            return -1;
        }
        resampled_frame->nb_samples = converted_samples;

        if (rawFrame->pts != AV_NOPTS_VALUE)
        {
            /**
             * @brief 在不同时间基之间换算时间戳。
             * @param[in] a 传入 `rawFrame->pts`。
             * @param[in] bq 输入时间基，传 `stream->time_base`。
             * @param[in] cq 输出时间基，传 `{1, out_sample_rate}`。
             * @return 返回换算后的 pts。
             */
            resampled_frame->pts = av_rescale_q(rawFrame->pts,
                m_formatContext->streams[m_audioStreamIndex]->time_base,
                AVRational{1, static_cast<int>(out_sample_rate)});
        }

        if (m_effectsEnabled)
        {
            /**
             * @brief 向滤镜图 source 推送一帧音频。
             * @param[in,out] ctx 传入 `m_bufferSrcCtx`。
             * @param[in] frame 传入 `resampled_frame.get()`。
             * @retval >=0 成功。
             * @retval <0 失败。
             */
            if (av_buffersrc_add_frame(m_bufferSrcCtx, resampled_frame.get()) < 0)
            {
                m_errorString = "Failed to send frame to filter graph";
                return -1;
            }
            continue;
        }
        else
        {
            outFrame = std::move(resampled_frame);
            return 1;
        }
    }
}

/**
 * @brief 获取音频时长（秒）。
 */
double AudioDecoder::getDuration() const
{
    if (!m_formatContext || m_audioStreamIndex < 0)
    {
        return 0.0;
    }
    int64_t duration_ts = m_formatContext->streams[m_audioStreamIndex]->duration;
    if (duration_ts == AV_NOPTS_VALUE)
    {
        duration_ts = m_formatContext->duration;
    }
    if (duration_ts != AV_NOPTS_VALUE)
    {
        /**
         * @brief 将 AVRational 转成 double。
         * @param[in] a 传入 `m_formatContext->streams[m_audioStreamIndex]->time_base`。
         * @return 返回 `a.num / a.den`。
         */
        return (double)duration_ts * av_q2d(m_formatContext->streams[m_audioStreamIndex]->time_base);
    }
    return 0.0;
}

/**
 * @brief 关闭解码器。
 */
void AudioDecoder::close()
{
    cleanup();
}

/**
 * @brief 清理解码器、重采样器与滤镜图。
 */
void AudioDecoder::cleanup()
{
    if (m_codecContext)
    {
        /**
         * @brief 释放解码器上下文并将指针置空。
         * @param[in,out] avctx 传入 `&m_codecContext`。
         * @return 无返回值（void）。
         */
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }
    if (m_formatContext)
    {
        /**
         * @brief 关闭输入并释放格式上下文。
         * @param[in,out] s 传入 `&m_formatContext`。
         * @return 无返回值（void）。
         */
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }
    if (m_swrCtx)
    {
        /**
         * @brief 释放重采样上下文并将指针置空。
         * @param[in,out] s 传入 `&m_swrCtx`。
         * @return 无返回值（void）。
         */
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
    if (m_filterGraph)
    {
        /**
         * @brief 释放滤镜图及其内部节点。
         * @param[in,out] graph 传入 `&m_filterGraph`。
         * @return 无返回值（void）。
         */
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr; // m_bufferSrcCtx and m_bufferSinkCtx are freed with the graph
    }
    m_audioStreamIndex = -1;
}

} // namespace VideoCreator
