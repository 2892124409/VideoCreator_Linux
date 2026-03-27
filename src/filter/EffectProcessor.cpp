#include "EffectProcessor.h"
#include <sstream>
#include <cmath>
#include <locale>
#include <iomanip> // For std::setprecision
#include <libavutil/opt.h>

/**
 * @file EffectProcessor.cpp
 * @brief 实现基于 FFmpeg filter graph 的 Ken Burns 与转场效果处理。
 */

namespace VideoCreator
{
    /**
     * @brief 构造并初始化成员默认值。
     */
    EffectProcessor::EffectProcessor()
        : m_filterGraph(nullptr), m_buffersrcContext(nullptr), m_buffersrcContext2(nullptr), m_buffersinkContext(nullptr),
          m_width(0), m_height(0), m_pixelFormat(AV_PIX_FMT_NONE), m_fps(0),
          m_sequenceType(SequenceType::None), m_expectedFrames(0), m_generatedFrames(0)
    {
    }

    EffectProcessor::~EffectProcessor()
    {
        cleanup();
    }

    /**
     * @brief 设置输出参数并重置序列状态。
     */
    bool EffectProcessor::initialize(int width, int height, AVPixelFormat format, int fps)
    {
        m_width = width;
        m_height = height;
        m_pixelFormat = format;
        m_fps = fps;
        m_errorString.clear();
        resetSequenceState();
        return true;
    }

    /**
     * @brief 启动 Ken Burns 序列并将输入图像写入滤镜图。
     *
     * 关键步骤：
     * 1. 根据 preset 或手工参数组装 zoompan 表达式；
     * 2. 初始化单输入滤镜图；
     * 3. 写入首帧并发送 EOF，转入“按帧拉取”模式。
     */
    bool EffectProcessor::startKenBurnsSequence(const KenBurnsEffect& effect, const AVFrame* inputImage, int total_frames)
    {
        // 先清空上一次序列状态（类型、计数），确保本次从干净状态启动。
        resetSequenceState();

        // 配置未启用 Ken Burns，直接失败返回。
        if (!effect.enabled) {
            m_errorString = "Ken Burns effect is not enabled.";
            return false;
        }

        // 输入帧为空，后续无法克隆或喂入滤镜图。
        if (!inputImage) {
            m_errorString = "Input image for Ken Burns effect is null.";
            return false;
        }

        // 目标输出帧数必须大于 0，否则不存在可生成序列。
        if (total_frames <= 0) {
            m_errorString = "Ken Burns total frames must be positive.";
            return false;
        }

        // 拷贝一份参数用于本地计算，避免改动调用方传入对象。
        KenBurnsEffect params = effect;

        // 用于组装最终 FFmpeg filter 描述字符串。
        std::stringstream ss;

        // 固定使用 C locale，保证浮点数小数点为 '.'，避免地区设置影响滤镜表达式解析。
        ss.imbue(std::locale("C"));

        // 预设模式：统一封装常用镜头运动参数，降低配置复杂度。
        if (params.preset == "zoom_in" || params.preset == "zoom_out")
        {
            // 计算缩放起止值：zoom_in 从 1.0 -> 1.2，zoom_out 从 1.2 -> 1.0。
            double start_z = (params.preset == "zoom_in") ? 1.0 : 1.2;
            double end_z = (params.preset == "zoom_in") ? 1.2 : 1.0;

            // 单独组装缩放表达式，保留足够精度，避免序列中累计误差。
            std::stringstream zoom_ss;
            zoom_ss << std::fixed << std::setprecision(10) << start_z << "+(" << (end_z - start_z) << ")*on/" << total_frames;

            // 提取完整的 z 表达式文本，例如 "1.0000000000+(0.2000000000)*on/120"。
            std::string zoom_expr = zoom_ss.str();

            // 拼接 zoompan 过滤串：只做缩放，位移由 zoompan 默认行为处理。
            ss << "zoompan="
               << "z='" << zoom_expr << "':"
               << "d=" << total_frames << ":s=" << m_width << "x" << m_height << ":fps=" << m_fps;
        }
        else if (params.preset == "pan_right" || params.preset == "pan_left")
        {
            // 平移预设固定轻微放大，避免平移时露出黑边。
            float pan_scale = 1.1f;

            // x/y 的起止位置参数。
            double start_x, end_x, start_y;

            // 根据平移方向确定 x 起止值。
            if (params.preset == "pan_right") {
                start_x = 0;
                end_x = m_width * (pan_scale - 1.0);
            } else {
                start_x = m_width * (pan_scale - 1.0);
                end_x = 0;
            }

            // y 固定在中线附近，让画面横向平移时更稳定。
            start_y = (m_height * (pan_scale - 1.0)) / 2;

            // 拼接 zoompan 过滤串：固定缩放 + x 线性插值 + 固定 y。
            ss << "zoompan="
               << "z='" << pan_scale << "':"
               << "x='" << start_x << "+(" << end_x - start_x << ")*on/" << total_frames << "':"
               << "y='" << start_y << "':"
               << "d=" << total_frames << ":s=" << m_width << "x" << m_height << ":fps=" << m_fps;
        }
        else
        {
            // 手工参数模式：按配置里的 start/end 做线性插值。
            ss << "zoompan="
               << "z='" << params.start_scale << "+(" << params.end_scale - params.start_scale << ")*on/" << total_frames << "':"
               << "x='" << params.start_x << "+(" << params.end_x - params.start_x << ")*on/" << total_frames << "':"
               << "y='" << params.start_y << "+(" << params.end_y - params.start_y << ")*on/" << total_frames << "':"
               << "d=" << total_frames << ":s=" << m_width << "x" << m_height << ":fps=" << m_fps;
        }

        // 用拼好的过滤串初始化单输入滤镜图（buffer -> zoompan -> buffersink）。
        if (!initFilterGraph(ss.str())) {
            return false;
        }

        // 克隆输入帧，避免直接操作调用方帧对象（滤镜内部可能持有引用）。
        AVFrame* src_frame = av_frame_clone(inputImage);
        if (!src_frame) {
            m_errorString = "Failed to clone source image for Ken Burns filter.";
            return false;
        }

        // 作为序列起点，首帧时间戳设为 0。
        src_frame->pts = 0;

        // 把首帧喂给滤镜 source，启动滤镜内部处理。
        if (av_buffersrc_add_frame(m_buffersrcContext, src_frame) < 0) {
            m_errorString = "Error while feeding the source image to the Ken Burns filtergraph.";
            // 推送失败时释放克隆帧，避免泄漏。
            av_frame_free(&src_frame);
            return false;
        }

        // 首帧已被滤镜图接收，立即释放本地克隆帧。
        av_frame_free(&src_frame);

        // 发送空帧通知 source 输入结束（EOF），让后续 fetch 只需持续从 sink 拉取结果。
        if (av_buffersrc_add_frame(m_buffersrcContext, nullptr) < 0) {
            m_errorString = "Failed to signal EOF to Ken Burns filter source.";
            return false;
        }

        // 标记当前运行序列类型为 KenBurns。
        m_sequenceType = SequenceType::KenBurns;
        // 记录本次计划输出的总帧数。
        m_expectedFrames = total_frames;
        // 已输出帧数从 0 开始计。
        m_generatedFrames = 0;

        // 启动成功，后续由 fetchKenBurnsFrame 按帧拉取输出。
        return true;
    }

    /**
     * @brief 获取下一帧 Ken Burns 输出。
     */
    bool EffectProcessor::fetchKenBurnsFrame(FFmpegUtils::AvFramePtr &outFrame)
    {
        if (m_sequenceType != SequenceType::KenBurns) {
            m_errorString = "Ken Burns sequence has not been initialized.";
            return false;
        }
        if (m_generatedFrames >= m_expectedFrames) {
            m_errorString = "Ken Burns sequence already produced all frames.";
            return false;
        }
        if (!retrieveFrame(outFrame)) {
            return false;
        }
        m_generatedFrames++;
        if (m_generatedFrames == m_expectedFrames) {
            resetSequenceState();
        }
        return true;
    }

    /**
     * @brief 启动转场序列。
     *
     * 关键步骤：
     * 1. 把内部转场枚举映射为 xfade transition 名称；
     * 2. 创建双输入滤镜图并显式补齐色彩信息；
     * 3. 写入 from/to 帧并发送 EOF，再由 fetchTransitionFrame 拉取结果。
     */
    bool EffectProcessor::startTransitionSequence(TransitionType type, const AVFrame* fromFrame, const AVFrame* toFrame, int duration_frames)
    {
        resetSequenceState();
        if (!fromFrame || !toFrame) {
            m_errorString = "Input frames for transition are null.";
            return false;
        }
        if (duration_frames <= 0) {
            m_errorString = "Transition frame count must be positive.";
            return false;
        }

        std::string transitionName;
        switch (type)
        {
        case TransitionType::CROSSFADE:
            transitionName = "fade";
            break;
        case TransitionType::WIPE:
            transitionName = "wipeleft";
            break;
        case TransitionType::SLIDE:
            transitionName = "slideleft";
            break;
        default:
            transitionName = "fade";
            break;
        }

        double transition_duration_sec = static_cast<double>(duration_frames) / m_fps;
        std::stringstream ss;
        ss.imbue(std::locale("C"));
        ss << std::fixed << std::setprecision(5);
        // tpad 用于在输入不足时复制边界帧，确保 xfade 在完整时间窗内有稳定输入。
        ss << "[in0]tpad=stop_mode=clone:stop_duration=" << transition_duration_sec << "[s0];"
           << "[in1]tpad=stop_mode=clone:stop_duration=" << transition_duration_sec << "[s1];"
           << "[s0][s1]xfade=transition=" << transitionName
           << ":duration=" << transition_duration_sec << ":offset=0"
           << ",format=pix_fmts=yuv420p[out]";

        if (!initTransitionFilterGraph(ss.str())) {
            return false;
        }

        /**
         * @brief 克隆起始帧，得到可独立管理的 AVFrame。
         * @param[in] src 传入 `fromFrame`。
         * @return 成功返回新帧指针；失败返回 `nullptr`。
         */
        AVFrame* from_clone = av_frame_clone(fromFrame);
        /**
         * @brief 克隆目标帧，得到可独立管理的 AVFrame。
         * @param[in] src 传入 `toFrame`。
         * @return 成功返回新帧指针；失败返回 `nullptr`。
         */
        AVFrame* to_clone = av_frame_clone(toFrame);
        if (!from_clone || !to_clone) {
            /**
             * @brief 释放 AVFrame 并将指针置空。
             * @param[in,out] frame 传入 `&from_clone`。
             * @return 无返回值（void）。
             */
            av_frame_free(&from_clone);
            /**
             * @brief 释放 AVFrame 并将指针置空。
             * @param[in,out] frame 传入 `&to_clone`。
             * @return 无返回值（void）。
             */
            av_frame_free(&to_clone);
            m_errorString = "Failed to clone frames for transition.";
            return false;
        }

        stampFrameColorInfo(from_clone);
        stampFrameColorInfo(to_clone);

        from_clone->pts = 0;
        /**
         * @brief 向第1路 source 推送输入帧，并保持内部引用。
         * @param[in,out] ctx 传入 `m_buffersrcContext`。
         * @param[in] frame 传入 `from_clone`。
         * @param[in] flags 传入 `AV_BUFFERSRC_FLAG_KEEP_REF`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (av_buffersrc_add_frame_flags(m_buffersrcContext, from_clone, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
            m_errorString = "Error feeding 'from' frame to transition filtergraph.";
            /**
             * @brief 释放 AVFrame 并置空。
             * @param[in,out] frame 传入 `&from_clone`。
             */
            av_frame_free(&from_clone);
            /**
             * @brief 释放 AVFrame 并置空。
             * @param[in,out] frame 传入 `&to_clone`。
             */
            av_frame_free(&to_clone);
            return false;
        }
        /**
         * @brief 释放 AVFrame 并置空。
         * @param[in,out] frame 传入 `&from_clone`。
         */
        av_frame_free(&from_clone);

        to_clone->pts = 0;
        /**
         * @brief 向第2路 source 推送输入帧，并保持内部引用。
         * @param[in,out] ctx 传入 `m_buffersrcContext2`。
         * @param[in] frame 传入 `to_clone`。
         * @param[in] flags 传入 `AV_BUFFERSRC_FLAG_KEEP_REF`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (av_buffersrc_add_frame_flags(m_buffersrcContext2, to_clone, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
            m_errorString = "Error feeding 'to' frame to transition filtergraph.";
            /**
             * @brief 释放 AVFrame 并置空。
             * @param[in,out] frame 传入 `&to_clone`。
             */
            av_frame_free(&to_clone);
            return false;
        }
        /**
         * @brief 释放 AVFrame 并置空。
         * @param[in,out] frame 传入 `&to_clone`。
         */
        av_frame_free(&to_clone);

        /**
         * @brief 向两路 source 发送空帧，通知输入结束（flush）。
         * @param[in,out] ctx 传入 `m_buffersrcContext` / `m_buffersrcContext2`。
         * @param[in] frame 传入 `nullptr`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (av_buffersrc_add_frame(m_buffersrcContext, nullptr) < 0 || av_buffersrc_add_frame(m_buffersrcContext2, nullptr) < 0) {
            m_errorString = "Failed to signal EOF to transition filter sources.";
            return false;
        }

        m_sequenceType = SequenceType::Transition;
        m_expectedFrames = duration_frames;
        m_generatedFrames = 0;
        return true;
    }

    /**
     * @brief 获取下一帧转场输出。
     */
    bool EffectProcessor::fetchTransitionFrame(FFmpegUtils::AvFramePtr &outFrame)
    {
        if (m_sequenceType != SequenceType::Transition) {
            m_errorString = "Transition sequence has not been initialized.";
            return false;
        }
        if (m_generatedFrames >= m_expectedFrames) {
            m_errorString = "Transition sequence already produced all frames.";
            return false;
        }
        if (!retrieveFrame(outFrame)) {
            return false;
        }
        m_generatedFrames++;
        if (m_generatedFrames == m_expectedFrames) {
            resetSequenceState();
        }
        return true;
    }

    /**
     * @brief 从 buffersink 拉取单帧输出并补齐色彩元信息。
     */
    bool EffectProcessor::retrieveFrame(FFmpegUtils::AvFramePtr &outFrame)
    {
        if (!m_buffersinkContext) {
            m_errorString = "Filter graph is not initialized.";
            return false;
        }
        auto filteredFrame = FFmpegUtils::createAvFrame();
        if (!filteredFrame) {
            m_errorString = "Failed to allocate frame for filter output.";
            return false;
        }
        /**
         * @brief 从滤镜图 sink 拉取一帧输出。
         * @param[in,out] ctx 传入 `m_buffersinkContext`。
         * @param[out] frame 传入 `filteredFrame.get()`。
         * @retval 0 成功。
         * @retval AVERROR(EAGAIN) 暂无输出帧。
         * @retval AVERROR_EOF 输出结束。
         * @retval <0 其他错误。
         */
        int ret = av_buffersink_get_frame(m_buffersinkContext, filteredFrame.get());
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            /**
             * @brief 将 FFmpeg 错误码转换为可读字符串。
             * @param[in] errnum 传入 `ret`。
             * @param[out] errbuf 输出缓冲区。
             * @param[in] errbuf_size 缓冲区大小。
             * @retval >=0 成功。
             * @retval <0 失败。
             */
            av_strerror(ret, errbuf, sizeof(errbuf));
            m_errorString = std::string("Failed to retrieve frame from filter graph: ") + errbuf;
            return false;
        }
        stampFrameColorInfo(filteredFrame.get());
        outFrame = std::move(filteredFrame);
        return true;
    }

    /**
     * @brief 对输出帧显式写入色彩空间信息，避免后续链路隐式推断。
     */
    void EffectProcessor::stampFrameColorInfo(AVFrame *frame) const
    {
        if (!frame) {
            return;
        }
        bool use_bt709 = m_height >= 720;
        frame->color_range = AVCOL_RANGE_MPEG;
        frame->colorspace = use_bt709 ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        frame->color_primaries = use_bt709 ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
        frame->color_trc = use_bt709 ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M;
        frame->sample_aspect_ratio = AVRational{1, 1};
    }

    /**
     * @brief 清空序列计数状态。
     */
    void EffectProcessor::resetSequenceState()
    {
        m_sequenceType = SequenceType::None;
        m_expectedFrames = 0;
        m_generatedFrames = 0;
    }

    /**
     * @brief 对外关闭接口。
     */
    void EffectProcessor::close()
    {
        cleanup();
    }

    /**
     * @brief 释放滤镜图和节点上下文。
     */
    void EffectProcessor::cleanup()
    {
        resetSequenceState();
        if (m_filterGraph) {
            /**
             * @brief 释放滤镜图及其内部节点。
             * @param[in,out] graph 传入 `&m_filterGraph`。
             * @return 无返回值（void）。
             */
            avfilter_graph_free(&m_filterGraph);
            m_filterGraph = nullptr;
        }
        m_buffersrcContext = nullptr;
        m_buffersrcContext2 = nullptr;
        m_buffersinkContext = nullptr;
    }

    /**
     * @brief 初始化单输入滤镜图（Ken Burns）。
     */
    bool EffectProcessor::initFilterGraph(const std::string &filterDescription)
    {
        cleanup();

        /**
         * @brief 通过名称查找滤镜定义。
         * @param[in] name 传入 `"buffer"`。
         * @return 成功返回 `AVFilter*`；失败返回 `nullptr`。
         */
        const AVFilter *buffersrc = avfilter_get_by_name("buffer");

        /**
         * @brief 通过名称查找滤镜定义。
         * @param[in] name 传入 `"buffersink"`。
         * @return 成功返回 `AVFilter*`；失败返回 `nullptr`。
         */
        const AVFilter *buffersink = avfilter_get_by_name("buffersink");

        /**
         * @brief 分配滤镜连接描述结构（InOut 链表节点）。
         * @return 成功返回 `AVFilterInOut*`；失败返回 `nullptr`。
         */
        AVFilterInOut *outputs = avfilter_inout_alloc();

        /**
         * @brief 分配滤镜连接描述结构（InOut 链表节点）。
         * @return 成功返回 `AVFilterInOut*`；失败返回 `nullptr`。
         */
        AVFilterInOut *inputs = avfilter_inout_alloc();
        char args[512];
        std::string fullFilterDesc;
        int colorspace;

        /**
         * @brief 分配滤镜图根对象。
         * @return 成功返回 `AVFilterGraph*`；失败返回 `nullptr`。
         */
        m_filterGraph = avfilter_graph_alloc();

        if (!outputs || !inputs || !m_filterGraph) {
            m_errorString = "无法分配滤镜图资源";
            goto end;
        }

        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=1/%d:pixel_aspect=%d/%d:frame_rate=%d/1",
                m_width, m_height, m_pixelFormat, m_fps, 1, 1, m_fps);

        /**
         * @brief 在滤镜图中创建具体滤镜节点。
         * @param[out] filt_ctx 传入 `&m_buffersrcContext`，接收节点上下文。
         * @param[in] filt 传入 `buffersrc`。
         * @param[in] name 节点名，传 `"in"`。
         * @param[in] args 参数字符串，传入 `args`。
         * @param[in] opaque 传入 `nullptr`。
         * @param[in] graph_ctx 传入 `m_filterGraph`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (avfilter_graph_create_filter(&m_buffersrcContext, buffersrc, "in", args, nullptr, m_filterGraph) < 0) {
            m_errorString = "无法创建buffer source滤镜";
            goto end;
        }
        
        colorspace = (m_height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        // 强制写入色彩元信息，避免 filter 链路自动协商导致偏色。

        /**
         * @brief 给滤镜节点设置整型选项。
         * @param[in,out] obj 传入 `m_buffersrcContext`。
         * @param[in] name 依次传入 `"color_range"`、`"colorspace"`、`"color_primaries"`、`"color_trc"`。
         * @param[in] val 对应传入目标色彩元数据。
         * @param[in] search_flags 传入 `0`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        av_opt_set_int(m_buffersrcContext, "color_range", AVCOL_RANGE_MPEG, 0);
        av_opt_set_int(m_buffersrcContext, "colorspace", colorspace, 0);
        av_opt_set_int(m_buffersrcContext, "color_primaries", (m_height >= 720) ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M, 0);
        av_opt_set_int(m_buffersrcContext, "color_trc", (m_height >= 720) ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M, 0);

        /**
         * @brief 在滤镜图中创建具体滤镜节点。
         * @param[out] filt_ctx 传入 `&m_buffersinkContext`。
         * @param[in] filt 传入 `buffersink`。
         * @param[in] name 节点名，传 `"out"`。
         * @param[in] args 传 `nullptr`（无参数）。
         * @param[in] opaque 传 `nullptr`。
         * @param[in] graph_ctx 传入 `m_filterGraph`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (avfilter_graph_create_filter(&m_buffersinkContext, buffersink, "out", nullptr, nullptr, m_filterGraph) < 0) {
            m_errorString = "无法创建buffer sink滤镜";
            goto end;
        }
        
        /**
         * @brief 复制字符串到新分配内存。
         * @param[in] s 这里传 `"in"`。
         * @return 成功返回新字符串指针；失败返回 `nullptr`。
         */
        outputs->name = av_strdup("in");
        outputs->filter_ctx = m_buffersrcContext;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        /**
         * @brief 复制字符串到新分配内存。
         * @param[in] s 这里传 `"out"`。
         * @return 成功返回新字符串指针；失败返回 `nullptr`。
         */
        inputs->name = av_strdup("out");
        inputs->filter_ctx = m_buffersinkContext;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        fullFilterDesc = "[in]" + filterDescription + "[out]";

        /**
         * @brief 解析滤镜描述并建立节点连接关系。
         * @param[in,out] graph 传入 `m_filterGraph`。
         * @param[in] filters 传入 `fullFilterDesc.c_str()`。
         * @param[in,out] inputs 传入 `&inputs`。
         * @param[in,out] outputs 传入 `&outputs`。
         * @param[in] log_ctx 传入 `nullptr`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (avfilter_graph_parse_ptr(m_filterGraph, fullFilterDesc.c_str(), &inputs, &outputs, nullptr) < 0) {
            m_errorString = "解析滤镜描述失败: " + fullFilterDesc;
            goto end;
        }

        /**
         * @brief 校验并配置滤镜图，使其进入可运行状态。
         * @param[in,out] graphctx 传入 `m_filterGraph`。
         * @param[in] log_ctx 传入 `nullptr`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (avfilter_graph_config(m_filterGraph, nullptr) < 0) {
            m_errorString = "配置滤镜图失败";
            goto end;
        }

    end:
        /**
         * @brief 释放 AVFilterInOut 链表。
         * @param[in,out] inout 传入 `&inputs`。
         * @return 无返回值（void）。
         */
        avfilter_inout_free(&inputs);
        /**
         * @brief 释放 AVFilterInOut 链表。
         * @param[in,out] inout 传入 `&outputs`。
         * @return 无返回值（void）。
         */
        avfilter_inout_free(&outputs);
        if (!m_errorString.empty()) {
            cleanup();
            return false;
        }
        return true;
    }

    /**
     * @brief 初始化双输入滤镜图（转场）。
     */
    bool EffectProcessor::initTransitionFilterGraph(const std::string& filter_description)
    {
        cleanup();

        /**
         * @brief 通过名称查找滤镜定义。
         * @param[in] name 传入 `"buffer"`。
         * @return 成功返回 `AVFilter*`；失败返回 `nullptr`。
         */
        const AVFilter* buffersrc = avfilter_get_by_name("buffer");

        /**
         * @brief 通过名称查找滤镜定义。
         * @param[in] name 传入 `"buffersink"`。
         * @return 成功返回 `AVFilter*`；失败返回 `nullptr`。
         */
        const AVFilter* buffersink = avfilter_get_by_name("buffersink");

        /**
         * @brief 分配滤镜连接描述结构（InOut）。
         * @return 成功返回 `AVFilterInOut*`；失败返回 `nullptr`。
         */
        AVFilterInOut* outputs = avfilter_inout_alloc();

        /**
         * @brief 分配滤镜连接描述结构（InOut）。
         * @return 成功返回 `AVFilterInOut*`；失败返回 `nullptr`。
         */
        AVFilterInOut* inputs = avfilter_inout_alloc();
        char args[512];
        int colorspace;

        /**
         * @brief 分配滤镜图根对象。
         * @return 成功返回 `AVFilterGraph*`；失败返回 `nullptr`。
         */
        m_filterGraph = avfilter_graph_alloc();
        if (!outputs || !inputs || !m_filterGraph) {
            m_errorString = "Cannot allocate filter graph resources.";
            goto end;
        }
        snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=1/%d:pixel_aspect=%d/%d:frame_rate=%d/1", m_width, m_height, m_pixelFormat, m_fps, 1, 1, m_fps);

        /**
         * @brief 创建第1路输入 buffer 节点。
         * @param[out] filt_ctx 传入 `&m_buffersrcContext`。
         * @param[in] filt 传入 `buffersrc`。
         * @param[in] name 传入 `"in0"`。
         * @param[in] args 传入 `args`。
         * @param[in] opaque 传入 `nullptr`。
         * @param[in] graph_ctx 传入 `m_filterGraph`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (avfilter_graph_create_filter(&m_buffersrcContext, buffersrc, "in0", args, nullptr, m_filterGraph) < 0) {
            m_errorString = "Cannot create buffer source 0.";
            goto end;
        }
        /**
         * @brief 创建第2路输入 buffer 节点。
         * @param[out] filt_ctx 传入 `&m_buffersrcContext2`。
         * @param[in] filt 传入 `buffersrc`。
         * @param[in] name 传入 `"in1"`。
         * @param[in] args 传入 `args`。
         * @param[in] opaque 传入 `nullptr`。
         * @param[in] graph_ctx 传入 `m_filterGraph`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (avfilter_graph_create_filter(&m_buffersrcContext2, buffersrc, "in1", args, nullptr, m_filterGraph) < 0) {
            m_errorString = "Cannot create buffer source 1.";
            goto end;
        }

        colorspace = (m_height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        // 两路输入必须使用同一色彩元数据，保证 xfade 输出一致性。

        /**
         * @brief 给滤镜节点设置整型选项。
         * @param[in,out] obj 传入 `m_buffersrcContext` / `m_buffersrcContext2`。
         * @param[in] name 依次传入色彩元数据键名。
         * @param[in] val 对应目标值。
         * @param[in] search_flags 传入 `0`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        av_opt_set_int(m_buffersrcContext, "color_range", AVCOL_RANGE_MPEG, 0);
        av_opt_set_int(m_buffersrcContext, "colorspace", colorspace, 0);
        av_opt_set_int(m_buffersrcContext, "color_primaries", (m_height >= 720) ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M, 0);
        av_opt_set_int(m_buffersrcContext, "color_trc", (m_height >= 720) ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M, 0);
        av_opt_set_int(m_buffersrcContext2, "color_range", AVCOL_RANGE_MPEG, 0);
        av_opt_set_int(m_buffersrcContext2, "colorspace", colorspace, 0);
        av_opt_set_int(m_buffersrcContext2, "color_primaries", (m_height >= 720) ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M, 0);
        av_opt_set_int(m_buffersrcContext2, "color_trc", (m_height >= 720) ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M, 0);

        /**
         * @brief 创建输出 buffer sink 节点。
         * @param[out] filt_ctx 传入 `&m_buffersinkContext`。
         * @param[in] filt 传入 `buffersink`。
         * @param[in] name 传入 `"out"`。
         * @param[in] args 传入 `nullptr`。
         * @param[in] opaque 传入 `nullptr`。
         * @param[in] graph_ctx 传入 `m_filterGraph`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (avfilter_graph_create_filter(&m_buffersinkContext, buffersink, "out", nullptr, nullptr, m_filterGraph) < 0) {
            m_errorString = "Cannot create buffer sink.";
            goto end;
        }

        /**
         * @brief 复制字符串到新分配内存。
         * @param[in] s 传入 `"in0"`。
         * @return 成功返回新字符串指针；失败返回 `nullptr`。
         */
        outputs->name = av_strdup("in0");
        outputs->filter_ctx = m_buffersrcContext;
        outputs->pad_idx = 0;

        /**
         * @brief 分配滤镜连接描述结构（InOut）用于第二输入。
         * @return 成功返回 `AVFilterInOut*`；失败返回 `nullptr`。
         */
        outputs->next = avfilter_inout_alloc();
        if (!outputs->next) {
            /**
             * @brief 释放 FFmpeg 申请的内存块。
             * @param[in] ptr 传入 `outputs->name`。
             * @return 无返回值（void）。
             */
            av_free(outputs->name);
            m_errorString = "Cannot allocate inout for second input.";
            goto end;
        }

        /**
         * @brief 复制字符串到新分配内存。
         * @param[in] s 传入 `"in1"`。
         * @return 成功返回新字符串指针；失败返回 `nullptr`。
         */
        outputs->next->name = av_strdup("in1");
        outputs->next->filter_ctx = m_buffersrcContext2;
        outputs->next->pad_idx = 0;
        outputs->next->next = nullptr;
        
        /**
         * @brief 复制字符串到新分配内存。
         * @param[in] s 传入 `"out"`。
         * @return 成功返回新字符串指针；失败返回 `nullptr`。
         */
        inputs->name = av_strdup("out");
        inputs->filter_ctx = m_buffersinkContext;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        /**
         * @brief 解析滤镜描述并建立节点连接关系。
         * @param[in,out] graph 传入 `m_filterGraph`。
         * @param[in] filters 传入 `filter_description.c_str()`。
         * @param[in,out] inputs 传入 `&inputs`。
         * @param[in,out] outputs 传入 `&outputs`。
         * @param[in] log_ctx 传入 `nullptr`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (avfilter_graph_parse_ptr(m_filterGraph, filter_description.c_str(), &inputs, &outputs, nullptr) < 0) {
            m_errorString = "Failed to parse filter description: " + filter_description;
            goto end;
        }

        /**
         * @brief 校验并配置滤镜图，使其进入可运行状态。
         * @param[in,out] graphctx 传入 `m_filterGraph`。
         * @param[in] log_ctx 传入 `nullptr`。
         * @retval >=0 成功。
         * @retval <0 失败。
         */
        if (avfilter_graph_config(m_filterGraph, nullptr) < 0) {
            m_errorString = "Failed to configure filter graph.";
            goto end;
        }

    end:
        /**
         * @brief 释放 AVFilterInOut 链表。
         * @param[in,out] inout 传入 `&inputs`。
         * @return 无返回值（void）。
         */
        avfilter_inout_free(&inputs);
        /**
         * @brief 释放 AVFilterInOut 链表。
         * @param[in,out] inout 传入 `&outputs`。
         * @return 无返回值（void）。
         */
        avfilter_inout_free(&outputs);
        if (!m_errorString.empty()) {
            cleanup();
            return false;
        }
        return true;
    }

} // namespace VideoCreator
