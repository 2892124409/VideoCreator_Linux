# VideoCreator 开发学习日志

> 目标：把这个项目按“从零开发”的思路拆开学习，记录每次学习的输入、过程、结论和待办。  
> 使用方式：每次学习后直接追加到对应章节，持续迭代这份文档。

## 1、学习总览

### 当前基线（以当前仓库为准）
- 技术栈：C++14 + FFmpeg + nlohmann/json + CMake
- 构建目标：`VideoCreatorCore`（静态库）+ `VideoCreatorCpp`（可执行，FFmpeg 可链接时）
- 主要能力：`image_scene` / `video_scene` / `transition`，支持 Ken Burns、转场、多音轨混音
- 输出容器：MP4（视频默认 `libx264`，音频编码器可配置）

### 里程碑追踪
- [x] 跑通 Linux 本地构建与渲染
- [x] 明确编码器选择路径（JSON -> Config -> RenderEngine）
- [ ] 系统梳理所有配置字段与默认值
- [ ] 补全最小可用测试集（功能 + 回归）

---

## 2、需求定义与技术选型

### 目标
- 从 JSON 描述的场景脚本中生成可播放视频。
- 保持接口简单，便于主工程“按钮触发、单次渲染”集成。

### 实现要点
- 采用 FFmpeg 做解码、滤镜、编码、封装，避免重复造轮子。
- 配置驱动：将业务规则放进 JSON 与 `ProjectConfig`，渲染层只执行。
- 构建层保持跨平台：Linux + Windows（MSVC/MinGW）。

### 关键代码入口
- `src/main.cpp`：示例入口，读取 `test_config.json` 并触发渲染。
- `src/VideoCreatorAPI.h`：库接口，供外部工程调用。
- `src/model/ProjectConfig.h`：配置模型定义。

### 踩坑与修复
- 早期 Qt 依赖导致环境耦合，后续改为纯标准库实现配置加载和日志输出。

### 当前结论
- 当前选型适合“离线合成 + 配置驱动 + 跨平台构建”的目标。

---

## 3、工程初始化与构建打通

### 目标
- 在当前机器直接完成“配置加载 -> 渲染 -> 输出文件”闭环。

### 实现要点
- CMake 最低版本 `3.16`，C++ 标准固定为 `14`。
- Linux 优先使用 `3rdparty/ffmpeg-linux/sysroot`，否则尝试系统 FFmpeg。
- Windows 使用 `3rdparty/ffmpeg`（MSVC `.lib` / MinGW `.dll.a`）。

### 关键代码入口
- `CMakeLists.txt`

### 踩坑与修复
- Linux 运行期动态库查找问题通过 RPATH 与本地 sysroot 处理。
- 当 FFmpeg 库不可用时，允许仅编译核心静态库，避免整体构建失败。

### 当前结论
- 当前仓库可在 Linux 原生环境完成构建并生成视频文件。

---

## 4、配置模型设计

### 目标
- 用结构化配置承载项目参数、场景资源、特效与编码策略。

### 实现要点
- 顶层：`ProjectConfig` 包含 `project`、`scenes`、`global_effects`。
- 场景类型：`IMAGE_SCENE`、`VIDEO_SCENE`、`TRANSITION`。
- 资源层：`resources.image`、`resources.audio`、`resources.video`、`resources.audio_layers`。
- 编码层：`global_effects.video_encoding`、`global_effects.audio_encoding`。

### 关键代码入口
- `src/model/ProjectConfig.h`

### 踩坑与修复
- 默认值要“可直接运行”，例如 `project` 分辨率/FPS 与编码默认值。
- 音频编码默认是 `aac`，但实际项目可在 JSON 中覆盖为 `libmp3lame`。

### 当前结论
- 模型层已经具备“默认可运行 + 可配置覆盖”的基础形态。

---

## 5、FFmpeg RAII封装学习记录

### 学习对象
- `src/ffmpeg_utils/FFmpegHeaders.h`
- `src/ffmpeg_utils/AvFrameWrapper.h`
- `src/ffmpeg_utils/AvPacketWrapper.h`
- `src/ffmpeg_utils/AvCodecContextWrapper.h`
- `src/ffmpeg_utils/AvFormatContextWrapper.h`

### 核心理解
- 这一层采用的是“智能指针 + 自定义删除器”的轻量 RAII，而不是传统“封装类构造/析构”模式。
- `FFmpegHeaders.h` 的核心作用是用 `extern "C"` 引入 FFmpeg C 接口，避免 C++ 名字改编导致链接失败。
- `AvFramePtr/AvPacketPtr/AvCodecContextPtr/AvFormatContextPtr` 本质都是 `std::unique_ptr<T, Deleter>` 别名。
- `unique_ptr` 第二个模板参数是删除器类型，所以可以指定 FFmpeg 对应的释放函数，而不是 `delete`。

### 封装对象与作用（关键）
- `AvFramePtr`（封装 `AVFrame`）：
  - 是什么：一帧“解压后可处理数据”的容器。
  - 作用：承载视频像素帧或音频采样帧，是滤镜处理和编码输入的核心数据结构。
- `AvPacketPtr`（封装 `AVPacket`）：
  - 是什么：一段“压缩码流数据包”的容器。
  - 作用：作为解码器输入和编码器输出，在 demux/mux 与编解码器之间传递压缩数据。
- `AvCodecContextPtr`（封装 `AVCodecContext`）：
  - 是什么：编解码器实例的运行上下文。
  - 作用：保存编码/解码参数与内部状态，并作为 `send/receive` 调用入口。
- `AvFormatContextPtr`（封装 `AVFormatContext`）：
  - 是什么：容器层上下文（mp4/mkv 等）。
  - 作用：管理输入/输出文件(或流)与各路 stream 元数据，负责封装层读写生命周期。

### 关键知识点
- `av_frame_free(&frame)` 里 `&frame` 是“指针变量的地址”（`AVFrame**`），这样函数才能把指针置空。
- `av_frame_alloc()` 只分配 `AVFrame` 结构体本身，不会分配像素/采样数据区。
- `av_frame_get_buffer(...)` 才会按帧参数分配真实 buffer；`32` 是常用对齐值，不是绝对必须。
- `AVPacket` 是压缩数据包容器，`AVFrame` 是可处理的解压后帧；常见流程是 `Packet -> Frame`（解码）和 `Frame -> Packet`（编码）。
- `AVCodecContext` 是编解码器实例运行上下文，既含参数也含内部状态。
- `AVFormatContext` 是容器层上下文，释放顺序通常是先关 `pb` 再 free context。

### 当前发现
- `createAudioFrame(..., channels, ...)` 里的 `channels` 参数当前未真正参与设置，仅通过 `(void)channels` 抑制告警。
- 项目音频链路目前以双声道为主（解码重采样和编码输出都以 stereo 处理）。

### 当前结论
- 这层 RAII 目标很明确：在尽量不改变 FFmpeg 原生调用习惯的前提下，解决资源释放与异常路径泄漏问题。
- 后续阅读解码器与渲染主流程时，可以默认“资源生命周期由智能指针托管”，重点放在数据流与时序逻辑上。

---

## 6、decoder解码器学习记录

### 学习对象
- `src/decoder/VideoLikeDecoderCore.h`
- `src/decoder/FrameScaler.h`
- `src/decoder/ImageDecoder.h` / `src/decoder/ImageDecoder.cpp`
- `src/decoder/VideoDecoder.h` / `src/decoder/VideoDecoder.cpp`
- `src/decoder/AudioDecoder.h` / `src/decoder/AudioDecoder.cpp`

### 模块分层理解
- `VideoLikeDecoderCore`：
  - 是什么：图片/视频共用的底层“开流 + 解码循环”核心。
  - 作用：统一处理 `avformat_open_input`、`av_find_best_stream`、`avcodec_open2`、`send/receive` 解码流程。
- `FrameScaler`：
  - 是什么：视频帧缩放与像素格式转换工具。
  - 作用：统一封装 `sws_getCachedContext`、`sws_scale` 与色彩空间参数设置。
- `ImageDecoder`：
  - 是什么：图片场景语义封装。
  - 作用：调用 `VideoLikeDecoderCore` 解出首帧，并提供 `decodeAndCache` 缓存能力。
- `VideoDecoder`：
  - 是什么：视频场景语义封装。
  - 作用：逐帧解码 + 按需缩放 + 时长/帧率查询。
- `AudioDecoder`：
  - 是什么：音频解码与标准化输出封装。
  - 作用：音频解码、重采样到统一格式、可选音量/淡入淡出滤镜。

### 三条解码链路（实际数据流）
- 图片链路：
  - `open` -> `decode(单帧)` -> `scaleToSize(可选)` -> `decodeAndCache(复用)`
- 视频链路：
  - `open` -> `decodeFrame(循环)` -> `scaleFrame(逐帧)`
- 音频链路：
  - `open` -> `decodeFrame(循环)` -> `swr_convert` 重采样 -> `filterGraph(可选)` -> 输出统一 PCM

### 关键机制：`send/receive` 两段式解码
- 核心接口：
  - `avcodec_send_packet`：把压缩包（`AVPacket`）送入解码器。
  - `avcodec_receive_frame`：从解码器取解压后的帧（`AVFrame`）。
- 标准循环：
  - 先 `receive` 尝试取帧。
  - 若返回 `AVERROR(EAGAIN)`，说明当前还需要更多输入，再去 `read_frame + send_packet`。
  - 输入结束时调用 `send_packet(..., nullptr)` 触发 flush。
  - 持续 `receive` 直到 `AVERROR_EOF`，表示解码器内缓存已全部输出。
- 关键理解：
  - 不是“一次 send 对应一次 receive”。
  - 解码器内部有缓存，可能“多 send 才 receive 到一帧”，也可能“一次 send receive 出多帧”。

### 学习补充要点（本阶段）
- `EAGAIN / EOF` 语义（统一判断规则）：
  - `receive` 返回 `AVERROR(EAGAIN)`：当前取不出帧，要继续喂包（`send`）。
  - `receive` 返回 `AVERROR_EOF`：解码器已完全结束，没有更多帧。
  - `send` 返回 `AVERROR(EAGAIN)`：当前不能继续喂，需先 `receive` 把内部帧取走。
- `flush` 时机与动作：
  - 当 `av_read_frame` 读到输入结束（EOF）后，必须执行 `avcodec_send_packet(codec, nullptr)`。
  - 之后继续 `receive`，直到返回 `AVERROR_EOF`，才算真正解码完成。
- 资源所有权与释放纪律：
  - `AVPacket/AVFrame` 由当前函数创建并持有，函数返回时由 RAII 自动释放。
  - 每次处理完 `packet` 后要及时 `av_packet_unref`，清掉内部引用，避免内存累积。
  - 解码核心对象（format/codec context）由 decoder 类生命周期托管，`cleanup` 统一释放。
- 缓存与复用（当前已落地）：
  - 图片链路使用 `decodeAndCache` 缓存首帧，避免重复解码同一图片。
  - 视频缩放使用 `sws_getCachedContext` 复用 `SwsContext`，减少重复创建转换上下文开销。

### 与渲染层的接口契约
- `decodeFrame` 返回值约定统一：
  - `>0`：成功拿到一帧
  - `0`：EOF
  - `<0`：错误
- 错误信息统一通过 `getErrorString()` 暴露给 `RenderEngine`。
- 音频输出目标被标准化为固定处理格式（当前为 44.1kHz、`AV_SAMPLE_FMT_FLTP`、立体声），方便后续混音与编码。

### 当前结论
- decoder 层本质是“底层 FFmpeg 调用的工程化封装”，把复杂 API 变成稳定接口给渲染层使用。
- 图片/视频解码共用了核心流程（`VideoLikeDecoderCore + FrameScaler`），音频因重采样与滤镜图需求，链路更复杂但职责清晰。

---

## 7、filter模块学习记录

### 学习对象
- `src/filter/EffectProcessor.h`
- `src/filter/EffectProcessor.cpp`
- 当前 `filter` 目录只包含一个核心类 `EffectProcessor`，负责 Ken Burns 与 Transition 两类特效。

### 目标
- 把“输入帧”组织成“可逐帧拉取的特效序列”。
- 向渲染层暴露稳定的 `start + fetch` 接口，屏蔽 FFmpeg filter graph 细节。
- 统一管理特效状态、错误与资源生命周期，保证多场景连续渲染稳定。

### 实现要点
- 采用 FFmpeg filter graph 执行特效，核心模式是“先建图并喂输入，再从 sink 逐帧拉取输出”。
- 特效层不手写像素算法，Ken Burns 与转场算法由 `zoompan` / `xfade` 等 FFmpeg 滤镜完成。
- `fetch...Frame(...)` 每次最多产出一帧，上层需要循环调用直到序列结束。
- 使用 `m_expectedFrames` 与 `m_generatedFrames` 跟踪序列进度，保证按设定帧数收尾。
- 输出帧统一补齐色彩信息，避免后续编码链路出现偏色或协商不一致。

### 关键代码入口
- `initialize(width, height, format, fps)`：设置输出规格，作为滤镜图 source 参数基准。
- `startKenBurnsSequence(...)`：构造 `zoompan` 表达式，建立单输入 graph 并喂入静态图像。
- `fetchKenBurnsFrame(...)`：从 sink 拉取 Ken Burns 序列帧并更新计数。
- `startTransitionSequence(...)`：构造 `xfade` 过滤链，建立双输入 graph 并喂入前后两帧。
- `fetchTransitionFrame(...)`：从 sink 拉取转场序列帧并更新计数。
- `initFilterGraph(...)` / `initTransitionFilterGraph(...)`：分别初始化单输入与双输入滤镜图。
- `retrieveFrame(...)`：统一拉帧、错误转换和色彩信息写回。
- `cleanup()` / `close()`：释放 graph、上下文和序列状态。

### 核心流程
- 计算时机（本阶段关键认知）：
  - `startKenBurnsSequence/startTransitionSequence` 主要负责“建图 + 喂输入 + 发送EOF（输入结束信号）”，不直接对外产出最终帧。
  - 真正“计算并取出一帧”发生在 `fetch...Frame -> retrieveFrame -> av_buffersink_get_frame(...)` 这条调用链。
  - 结论：这是典型 pull 模型，渲染层每次调用 `fetch` 拉取一帧，就触发一次输出帧获取。
- Ken Burns 流程：
  - `startKenBurnsSequence` 做参数校验，拼接 `zoompan` 表达式，初始化单输入 graph。
  - 克隆输入图像帧送入 `buffersrc`，随后送 EOF，进入“只拉取输出”阶段。
  - `fetchKenBurnsFrame` 调用 `retrieveFrame` 逐帧读取，直到 `m_generatedFrames == m_expectedFrames`。
- Transition 流程：
  - `startTransitionSequence` 做参数校验并映射转场类型（`fade/wipeleft/slideleft`）。
  - 构造 `tpad + xfade + format` 过滤链，初始化双输入 graph。
  - 两路输入帧分别送入 `in0/in1`，两路都送 EOF，进入逐帧拉取阶段。
  - `fetchTransitionFrame` 与 Ken Burns 一样，按帧拉取并在结束时重置状态。
- 共同模型：
  - 都是“start 一次，fetch 多次”。
  - 都是 pull（sink 拉取）模型，不是 push 回调模型。
  - 错误统一写入 `m_errorString`，供渲染层读取。

### 踩坑与修复
- 浮点参数字符串受系统 locale 影响：已使用 `std::locale("C")` + 固定精度，保证小数点为 `.`。
- 转场两路输入色彩信息不一致会导致偏色：在输出端统一 `stampFrameColorInfo(...)`。
- 多序列连续执行若不清状态会串场：序列结束时统一 `resetSequenceState()`，失败路径 `cleanup()`。
- 转场输出像素格式若不固定会增加后续转换成本：当前统一输出 `yuv420p`，与主编码链路对齐。

### 当前结论
- `filter` 层已形成稳定的序列式特效执行框架：建图、喂输入、逐帧拉取、统一收尾。
- 渲染层只需按契约循环调用 `fetch...Frame(...)`，无需关心 graph 内部细节。
- 后续新增特效可复用同一套 `start/fetch + sequence state` 抽象。

---

## 8、RenderEngine总调度器学习记录

### 学习对象
- `src/engine/RenderEngine.h`
- `src/engine/RenderEngine.cpp`

### 目标
- 把 `ProjectConfig` 组织成可落地的视频输出流程。
- 统一调度“解码/特效/混音/编码/封装”，保证时间轴连续。
- 对外只暴露简洁接口：`initialize()` + `render()`。

### 实现要点
- `render()` 本身很短，职责是“遍历场景并分发”：
  - 普通场景走 `renderScene(...)`。
  - 转场场景走 `renderTransition(...)`。
  - 全部结束后统一 `flushAudio`、`flushEncoder`、`av_write_trailer`。
- 真正复杂逻辑在 `renderScene/renderTransition`，不是在 `render` 函数本体。
- 编码与封装分层明确：
  - 编码：`AVFrame -> AVPacket`（`avcodec_send_frame / avcodec_receive_packet`）。
  - 写包：`AVPacket -> 容器文件`（`av_packet_rescale_ts / av_interleaved_write_frame`）。

### 关键代码入口
- `initialize(const ProjectConfig&)`：重置状态、创建输出流、写文件头。
- `render()`：总循环 + 收尾。
- `renderScene(const SceneConfig&)`：普通场景主流程（图片/视频 + 音频）。
- `renderTransition(...)`：场景间转场渲染。
- `sendBufferedAudioFrames()`：FIFO 音频送编码器并写包。
- `flushAudio()` / `flushEncoder(...)`：输出尾段冲洗。

### renderScene核心流程
- 根据 `scene.type` 区分图片场景和视频场景，准备解码器与时长。
- 视频场景启视频解码线程（解码+缩放后入队）。
- 音频按“每个音轨一个线程”解码为双声道 float 数据，主线程混音后写入 FIFO。
- 图片场景可选启动 Ken Burns 序列；视频场景从异步队列取帧。
- 主循环按时间轴交织推进音视频，持续编码并写包。
- 结束时缓存本场景末帧，供转场复用。

### renderTransition核心流程
- 优先从缓存获取 `from` 末帧和 `to` 首帧；缓存缺失时实时计算。
- 两端场景若带 Ken Burns，会额外计算首/末关键帧。
- 使用 `EffectProcessor` 逐帧拉取转场结果并送视频编码器。
- 同时补齐转场区间音频时间轴（静音或可选混音路径）。

### 关键机制：音画同步与并发模型
- 音画同步由主线程统一决策，不由子线程决定：
  - `video_time = m_frameCount / fps`
  - `audio_time = m_audioSamplesCount / sample_rate`
  - 谁更落后先推进谁。
- 子线程职责是“生产数据”，主线程职责是“按时间轴消费并写出”。
- 线程间同步的作用是并发安全，不是时间同步：
  - `mutex + condition_variable + deque + stop/error/finished`。
- 音频结构不是单个 MPSC 队列，而是“每音轨独立缓冲 + 主线程汇总混音”，更接近多个 SPSC 生产消费通道。

### 编码写包执行位置（关键确认）
- 视频编码/写包在主线程：
  - `renderScene` 视频分支中执行。
  - `renderTransition` 转场循环中执行。
- 音频编码/写包也在主线程：
  - 通过 `sendBufferedAudioFrames()` 执行。
- 因此当前架构是“解码并行、编码串行、mux 串行”，时序更可控。

### 踩坑与修复
- 若把编码与写包拆成独立线程，会显著增加队列、背压、flush、时间戳顺序复杂度。
- 当前方案先保持编码+mux 主线程串行，依赖 FFmpeg 编码器内部线程提升吞吐，工程风险更低。

### 当前结论
- `RenderEngine` 已形成清晰的总调度模型：子线程产数据，主线程做时间轴决策并完成编码/封装。
- 主流程短而稳定，复杂度被下沉到 `renderScene/renderTransition`，便于后续按模块继续深入优化。

---

## 9、面试指导指南

### 目标
- 把“做了什么、为什么这么做、效果如何”讲成一条完整技术故事。
- 避免只背术语，强调你在这个模块中的真实设计决策与工程取舍。
- 面向实习/校招面试，支持 30 秒、1 分钟、3 分钟三种讲法。

### 项目介绍模板（可直接背）
- 30 秒版本：
  - 我负责小组项目里的底层视频合成模块，基于 C++14 + FFmpeg 实现了一个 JSON 驱动的合成引擎。上层通过 `RenderFromJson/RenderFromJsonString` 触发渲染，底层支持图片/视频场景、转场、多轨音频混音和 MP4 导出。核心难点是音画同步和特效链路内存控制，我通过主线程统一时间轴调度、特效单帧拉取模型和标准化编码封装流程把链路跑通并稳定落地。
- 1 分钟版本：
  - 这个模块本质是“配置驱动的离线视频渲染引擎”。我主要做了三件事：第一，定义 JSON 协议并封装统一 API，让上层只传配置即可渲染。第二，搭建完整音视频链路：解码、缩放/滤镜、重采样、多轨混音、编码和 MP4 封装。第三，解决稳定性问题：音画同步采用主线程统一时间轴推进，子线程只负责解码生产；特效处理改为 `start/fetch` 的单帧拉取，降低长场景峰值内存，并修复转场偏色。最终形成了可在 Linux/Windows 构建、可复用的合成子模块。
- 3 分钟展开顺序（建议）：
  - 先讲定位：这是业务项目中的底层渲染引擎，不是 UI 功能。
  - 再讲架构：`ConfigLoader -> RenderEngine -> Decoder/Filter -> Encoder/Mux`。
  - 再讲关键机制：音画同步、并发模型、编码写包分层。
  - 最后讲你的改进点：特效拉取模型、色彩信息统一、接口封装。

### 架构口述模板
- 配置入口：
  - 上层传 JSON 文件或字符串，`VideoCreatorAPI` 解析并构造 `ProjectConfig`。
- 调度核心：
  - `RenderEngine` 负责场景遍历、普通场景渲染、转场渲染、最终 flush 和 trailer。
- 数据处理：
  - 视频：`ImageDecoder/VideoDecoder -> (可选 EffectProcessor) -> video encoder`。
  - 音频：`AudioDecoder(多层) -> 重采样/混音 -> AVAudioFifo -> audio encoder`。
- 输出封装：
  - 编码器产出 `AVPacket`，再由 mux 层写入 MP4。

### 你这个项目最值得强调的亮点
- 配置驱动能力：
  - 通过 JSON 协议把场景、转场、音轨、编码参数统一抽象，上层调用成本低。
- 音画同步机制：
  - 主线程比较 `video_time` 和 `audio_time`，谁落后先推进谁，保证时间轴连续。
- 并发模型清晰：
  - 子线程只生产（解码/预处理），主线程做同步决策与编码写包，职责边界清楚。
- 特效链路工程化：
  - 采用 `start... + fetch...` 单帧拉取模型，避免整段缓存导致内存峰值过高。

### 高频问答（深入版，贴合当前源码）

**Q：你这个模块的核心职责是什么？它和“播放器”有什么本质区别？**

A：  
核心职责是“离线生产视频文件”，不是“实时播放”。  
播放器是按时间读取和渲染现成媒体流；我的模块是把图片/视频/音频资源按 JSON 描述重新编排，经过解码、特效、混音、编码、封装，生成一个新的 MP4 输出。  
一句话：播放器是消费链路，合成引擎是生产链路。

**Q：如果让你 1 分钟讲清架构，你怎么讲？**

A：  
入口是 `RenderFromJson/RenderFromJsonString`，先把 JSON 解析成 `ProjectConfig`。  
`RenderEngine` 是总调度器：按场景顺序调用 `renderScene` 或 `renderTransition`。  
场景内部由 decoder 产出帧，filter 产出特效帧，audio 侧做重采样与混音，最后统一进入编码器与 mux。  
收尾阶段执行 audio flush、encoder flush、write trailer，保证尾包完整可播。

**Q：你简历里写的 `AVFormat / AVCodec / SwrContext / AVAudioFifo` 在项目里分别做什么？**

A：  
这四个组件对应四层职责，连起来就是完整音视频链路：  
- `AVFormat`（容器层）：负责输入探测和输出封装，管理 stream 与 mux。  
- `AVCodec`（编解码层）：负责 `AVPacket <-> AVFrame` 转换，视频/音频压缩与解压都在这层。  
- `SwrContext`（音频重采样层）：把不同输入音频统一到目标采样率/格式/声道布局，便于后续混音与编码。  
- `AVAudioFifo`（音频对齐缓冲层）：把可变长度采样块整理成编码器需要的固定 `frame_size`，并处理尾段补齐。  
所以我在面试里会讲成一句话：`AVFormat` 管容器，`AVCodec` 管压缩，`SwrContext` 管音频规格统一，`AVAudioFifo` 管编码喂入对齐。

**Q：如果只看音频链路，这四者是怎么串起来的？**

A：  
输入先经 `AVFormat` 拆流，音频包送 `AVCodec` 解码成 PCM 帧；  
解码后若格式不一致，用 `SwrContext` 重采样到统一目标格式；  
混音结果写入 `AVAudioFifo`，按编码器 `frame_size` 取帧再送 `AVCodec` 编码；  
编码得到的音频 `AVPacket` 最后回到 `AVFormat` 进行 mux 写入 MP4。  
这条链路保证了“源格式再杂，输出编码侧仍然稳定可控”。

**Q：音画同步到底怎么保证？是“感觉差不多”还是有明确机制？**

A：  
有明确机制，不是经验值。  
主循环每次都比较两条时间轴：  
- `video_time = m_frameCount / fps`  
- `audio_time = m_audioSamplesCount / sample_rate`  
谁更落后先推进谁。推进时会给 frame 赋 PTS，再 `av_packet_rescale_ts` 到 stream time_base 后写包。  
这保证了时间语义从 frame 到 packet 到 mux 一致，不依赖播放器补偿。

**Q：子线程很多，那同步逻辑放在哪？**

A：  
时间同步只在主线程。  
子线程只负责生产数据（视频解码缩放、音轨解码）。  
线程间 `mutex + cv` 的作用是并发安全、背压和可控退出，不是做音画同步决策。  
也就是说：子线程产数据，主线程按统一时钟消费并写出。

**Q：你们的音频多轨模型是 MPSC 吗？**

A：  
严格说不是单一 MPSC。  
每个音轨有独立缓冲和锁，主线程混音时逐层拉取，拓扑更接近“多个 SPSC + 主线程汇总”。  
这么做的好处是每层状态隔离，出错定位和控制更直接，不会在一个共享大队列里互相污染。

**Q：为什么音频要引入 AVAudioFifo，这层是不是多余？**

A：  
不是多余，是编码对齐所必需。  
音频解码/混音每次拿到的 samples 数不稳定，而编码器通常要求按固定 `frame_size` 喂入。  
FIFO 的作用就是把“可变块输入”整形成“固定块输出”，不足时补静音，避免尾段丢样本和时长漂移。

**Q：`avcodec_send_frame / avcodec_receive_packet` 为什么是两段式，不能一进一出吗？**

A：  
不能假设一进一出。  
编码器/解码器内部有缓存，可能多次 send 才有一个 packet，也可能一次 send 产多个 packet。  
所以必须用循环 receive，并正确处理 `EAGAIN/EOF`，否则会丢数据或提前结束。  
这也是 FFmpeg 官方推荐的标准模式。

**Q：编码和写包为什么要分开理解？**

A：  
编码是 codec 层，把 `AVFrame` 压缩成 `AVPacket`。  
写包是 mux 层，把 packet 按容器规则（MP4）交织落盘。  
如果不区分这两层，常见错误是时间基处理混乱：编码器 time_base、stream time_base、容器写入时间戳容易错配。

**Q：你为什么不把“编码”和“写包”拆成两个线程？**

A：  
当前阶段优先正确性与可维护性。  
mux 对时间戳顺序和流交织非常敏感，主线程串行更稳。  
编码器内部已经可多线程，外层再拆线程要引入 packet 队列、背压、flush 协议和错序处理，复杂度上升明显。  
我的取舍是先保证时序正确和稳定输出，再按 profile 决定是否做异步解耦。

**Q：转场链路里最容易出问题的点是什么？你怎么处理的？**

A：  
两个高风险点：  
1. 关键帧来源不稳定（from 末帧 / to 首帧）。  
2. 色彩信息不一致导致偏色。  
对应处理：  
- 关键帧优先走缓存，缺失时再回退实时提取。  
- 转场输入输出显式写入色彩元信息并统一 `yuv420p`，减少 filter 协商差异。

**Q：为什么把特效从“整段缓存”改成“单帧拉取”？**

A：  
整段缓存会把特效结果一次性堆在内存里，长场景很容易出现峰值抖动。  
单帧拉取（`start + fetch`）把特效输出改成流式消费：主循环要一帧拉一帧，天然和编码节奏对齐。  
结果是内存更可控，且错误定位更精确（可直接定位到第几帧拉取失败）。

**Q：你这里有哪些“工程化而不是 demo 化”的体现？**

A：  
有三点可以讲：  
1. API 层隔离：上层只调用 JSON API，不感知底层 FFmpeg 细节。  
2. 失败路径完整：每一步都回填可读错误并做资源回收。  
3. 生命周期清晰：初始化、场景循环、flush、trailer 收尾是闭环，不会生成“能播一半”的残缺文件。

**Q：如果面试官追问“你这方案的缺点是什么”，怎么答？**

A：  
可以主动讲两个边界：  
1. 当前时间比较使用 `double`，长期可升级为统一整数时基（`AVRational`）提高严谨性。  
2. 编码与写包未异步解耦，在极高吞吐场景可能受 I/O 阻塞影响。  
然后补一句：这些是可演进项，不影响当前方案的正确性和稳定性目标。

### 面试回答策略（建议）
- 先结论，后细节：
  - 先一句话说清“我做了什么”，再展开实现点。
- 每个点都补“为什么”：
  - 不只讲用了什么 API，要讲取舍依据（正确性、复杂度、稳定性）。
- 避免空泛描述：
  - 少说“优化了很多”，多说“改成了什么机制，解决了什么具体问题”。

### 可能被追问的扩展方向
- 如果继续优化性能：
  - 时间比较从 `double` 迁移到统一整数时基（`AVRational`）；
  - 评估编码与写包异步解耦的收益与风险；
  - 减少混音缓冲拷贝、优化队列容量策略。
- 如果继续增强功能：
  - 增加更多转场/滤镜模板；
  - 增加更细粒度的配置校验和错误定位；
  - 增加回归测试样例（长视频、多音轨、异常资源）。

### 当前结论
- 该模块在面试中最强的讲法是“工程化落地能力”：配置驱动、可扩展架构、稳定时序、可解释取舍。
- 只要按“定位 -> 架构 -> 关键机制 -> 结果”的顺序讲，基本能把这个项目讲得清楚且有技术深度。
