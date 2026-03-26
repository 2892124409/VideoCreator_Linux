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

## 5、JSON 配置加载器

### 目标
- 把 JSON 稳定映射到 `ProjectConfig`，并补齐场景时长等派生信息。

### 实现要点
- 入口：`loadFromFile` / `loadFromString`。
- 解析顺序：`project` -> `scenes` -> `global_effects`。
- 字段解析偏宽松（存在且类型匹配才覆盖默认值）。
- 时长规则（当前实现）：
  - 如果场景显式给了 `duration`，优先使用该值。
  - 否则 `image_scene` 尝试使用音频时长（主音频 + audio_layers 取最长）。
  - `video_scene` 优先视频时长，其次音频时长，最后回退 5 秒。

### 关键代码入口
- `src/model/ConfigLoader.h`
- `src/model/ConfigLoader.cpp`

### 踩坑与修复
- 资源路径错误会触发时长回退，需结合控制台日志定位。
- 多音轨场景要注意最长轨道会影响最终 scene duration。

### 当前结论
- 加载器已具备“配置解析 + 时长推断 + 错误信息”三项核心能力。

---

## 6、解码层实现

### 目标
- 提供统一的数据输入能力：图片帧、视频帧、音频采样。

### 实现要点
- `ImageDecoder`：读取图片并输出帧。
- `VideoDecoder`：读取视频并逐帧输出。
- `AudioDecoder`：读取音频并支持重采样、音量处理与淡入淡出。

### 关键代码入口
- `src/decoder/ImageDecoder.cpp`
- `src/decoder/VideoDecoder.cpp`
- `src/decoder/AudioDecoder.cpp`

### 踩坑与修复
- 音频源格式不统一时，重采样与格式转换是稳定编码的前提。

### 当前结论
- 解码层分工清晰，可支持后续更多资源类型扩展。

---

## 7、渲染主流程

### 目标
- 将场景序列按时间顺序渲染为单个输出媒体文件。

### 实现要点
- 初始化阶段：准备输出容器、视频/音频编码器、音频 FIFO。
- 主循环阶段：遍历 `scenes`，分别处理普通场景和转场场景。
- 收尾阶段：flush 编码器与缓存，写入文件尾并关闭输出。

### 关键代码入口
- `src/engine/RenderEngine.h`
- `src/engine/RenderEngine.cpp`

### 踩坑与修复
- 编码器找不到时要直接暴露具体 codec 名称，便于配置层排查。

### 当前结论
- 渲染链路完整，支持从配置到最终输出的端到端执行。

---

## 8、音视频同步与时间轴

### 目标
- 保证视频帧推进与音频采样推进在同一时间轴上对齐。

### 实现要点
- 以 PTS/采样计数作为同步基准。
- 渲染循环内动态比较“当前视频时间”和“当前音频时间”，决定先写哪路数据。
- 转场期间按时间轴补齐音频，避免画面连续但音频断裂。

### 关键代码入口
- `src/engine/RenderEngine.cpp`（`renderScene`、`renderTransition` 音频推进逻辑）

### 踩坑与修复
- 仅靠帧数估算易漂移，需以采样数和 time_base 做真实推进。

### 当前结论
- 当前同步策略可满足常规场景拼接与转场下的音画一致性需求。

---

## 9、特效与转场

### 目标
- 在不破坏主渲染链路的前提下叠加视觉变化。

### 实现要点
- Ken Burns：支持预设与手动参数。
- 转场：支持 `crossfade` / `wipe` / `slide`。
- 特效处理采用“启动序列 + 按帧拉取”方式，避免一次性缓存整段帧序列。

### 关键代码入口
- `src/filter/EffectProcessor.h`
- `src/filter/EffectProcessor.cpp`

### 踩坑与修复
- 历史问题包含转场偏色与内存占用偏高，当前通过像素格式/流程改造缓解（详见 `Note.md`）。

### 当前结论
- 特效与转场能力已可用，后续重点是效果参数化与性能细化。

---

## 10、音频混音与编码

### 目标
- 支持主音频、附加音轨、视频原声的混合并稳定编码输出。

### 实现要点
- 音轨来源：`resources.audio` + `resources.audio_layers` + `video.use_audio`。
- 每个轨道可配置 `volume` 与 `start_offset`，按场景时间轴混合。
- 编码器选择来自 `global_effects.audio_encoding.codec`，运行时动态查找 encoder。

### 关键代码入口
- `src/engine/RenderEngine.cpp`（`createAudioStream`、场景音轨混合）
- `src/model/ProjectConfig.h`（`AudioEncodingConfig` 默认值）
- `src/model/ConfigLoader.cpp`（`audio_encoding` 字段解析）

### 踩坑与修复
- VSCode 播放无声问题：文件本身有音轨，根因是播放器对 AAC 兼容性；切 MP3 音轨可规避。

### 当前结论
- 项目支持 AAC 与 MP3（如 `libmp3lame`）等可用编码器，取决于 FFmpeg 构建能力与配置值。

---

## 11、跨平台与依赖治理

### 目标
- 保持单套代码在 Linux/Windows 可构建，并减少外部框架耦合。

### 实现要点
- 完整去除 Qt 依赖，配置与日志改为标准库实现。
- C++ 标准降为 C++14，避免依赖 C++17 特性。
- Linux：优先 vendored FFmpeg sysroot；Windows：使用 `3rdparty/ffmpeg` 导入库。

### 关键代码入口
- `CMakeLists.txt`
- `src/model/ConfigLoader.cpp`
- `src/main.cpp`

### 踩坑与修复
- 运行时库路径和链接方式在 Linux/Windows 差异大，需在 CMake 内显式处理。

### 当前结论
- 当前仓库已满足“标准库 + 第三方库（FFmpeg/json）”的目标。

---

## 12、调试实战记录

### 目标
- 形成“问题 -> 定位 -> 结论 -> 固化动作”的可复用调试闭环。

### 实现要点
- 先判定“渲染问题”还是“播放器问题”，避免错误修代码。
- 用 `ffprobe` 和 `ffmpeg volumedetect` 做媒体事实验证，不靠主观听感。
- 遇到配置相关问题，优先检查 JSON 是否覆盖了默认值。

### 关键代码入口
- `test_config.json`
- `src/engine/RenderEngine.cpp`
- `src/model/ConfigLoader.cpp`

### 踩坑与修复
- 案例：同一 MP4 在 VSCode 无声、外部播放器有声。验证结果表明文件有有效音轨且非静音，属于播放端兼容性问题。

### 当前结论
- 调试优先级应是：可复现命令 -> 流信息 -> 时长/音量统计 -> 代码定位。

---

## 13、验证与测试清单

### 构建验证
```bash
cmake -S . -B build
cmake --build build
```

### 运行验证
```bash
./build/bin/VideoCreatorCpp
```

### 输出文件验证（流信息）
```bash
LD_LIBRARY_PATH=3rdparty/ffmpeg-linux/sysroot/usr/lib/x86_64-linux-gnu:3rdparty/ffmpeg-linux/sysroot/lib/x86_64-linux-gnu:3rdparty/ffmpeg-linux/sysroot/usr/lib/x86_64-linux-gnu/blas:3rdparty/ffmpeg-linux/sysroot/usr/lib/x86_64-linux-gnu/lapack:3rdparty/ffmpeg-linux/sysroot/usr/lib/x86_64-linux-gnu/pulseaudio \
3rdparty/ffmpeg-linux/sysroot/usr/bin/ffprobe -hide_banner -show_streams -show_format output/effects_test_audio_linux.mp4
```

### 输出文件验证（音量非静音）
```bash
LD_LIBRARY_PATH=3rdparty/ffmpeg-linux/sysroot/usr/lib/x86_64-linux-gnu:3rdparty/ffmpeg-linux/sysroot/lib/x86_64-linux-gnu:3rdparty/ffmpeg-linux/sysroot/usr/lib/x86_64-linux-gnu/blas:3rdparty/ffmpeg-linux/sysroot/usr/lib/x86_64-linux-gnu/lapack:3rdparty/ffmpeg-linux/sysroot/usr/lib/x86_64-linux-gnu/pulseaudio \
3rdparty/ffmpeg-linux/sysroot/usr/bin/ffmpeg -hide_banner -i output/effects_test_audio_linux.mp4 -vn -af volumedetect -f null -
```

### 验收标准
- 可执行程序成功返回并输出目标文件。
- `ffprobe` 显示音视频流完整，时长合理。
- `volumedetect` 有有效样本且 `max_volume` 不是 `-inf`。

---

## 14、个人复盘与后续计划

### 当前收获
- 已能独立跟踪“配置 -> 解析 -> 渲染 -> 媒体验证”完整链路。
- 已理解编码器选择是配置驱动，不是写死在输出容器里。

### 仍需补强
- 对 `EffectProcessor` 滤镜图细节理解还不够系统。
- 对复杂混音场景（多轨 offset + fade）的边界用例覆盖不足。

### 下一步计划
- 补一个“最小配置样例集”（图片场景、视频场景、混音场景各 1 份）。
- 给关键路径补自动化回归检查（至少流信息与时长断言）。
