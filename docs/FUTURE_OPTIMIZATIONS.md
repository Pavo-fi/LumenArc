# 未来优化项（已暂缓/移除的功能记录）

> 本文件记录经评估后**暂缓实现**或**已移除但值得未来重启**的功能项，保留技术要点以便后续参考。

## 1. GPU 零拷贝显示管线（原 v1.6 实验功能，已移除）

**原方案**：硬解帧（D3D11 纹理）不下载到 CPU 内存，直接经 D3D11 VideoProcessor 做 NV12→BGRA 色彩转换，输出到 keyed-mutex 共享纹理，UI 侧 QRhi/QWidget 直接采样显示（零拷贝）。播放期每 30 帧补发一帧 QImage 供放大镜/钉图使用。

**当时的收益假设**：4K 播放时省掉"GPU→CPU 回传 + sws 转换 + QImage 上传"三段开销。

**暂缓原因**：当前 Qt 光栅显示路径在目标场景（SD~1080p 监控视频）已足够流畅；零拷贝路径复杂度高（跨设备同步、截图叠加强制回退、放大镜需低频 CPU 帧），投入产出比低。

**移除时的落点**（重启时的参考）：
- 引擎侧：`FfmpegVideoEngine` 曾含 `ensureGpuPipeline/gpuBlitToShared/releaseGpuPipeline`（D3D11 VideoProcessor + keyed-mutex）
- 接口侧：`IVideoEngine` 曾含 `GpuFrameInfo / frameTextureReady / gpuFramesActiveChanged / setGpuFramesEnabled`
- UI 侧：`GpuVideoPresenter`（QWidget 内嵌共享纹理呈现），shader `src/shaders/video.vert/frag`（qsb 烘焙）
- 注意：**D3D11VA 硬件解码仍然保留**（设置菜单可开关），移除的只是"显示"这一段

## 2. VLC 后备播放内核（已移除）

**原定位**：FFmpeg 内核之外的可切换后备（`设置 → 播放内核`）。

**移除原因**：FFmpeg 内核在测试矩阵（h264/h265/mkv/avi/无音轨/低码率/4K/损坏文件）上已全覆盖，VLC 后备长期无人使用，却占用打包体积（libvlc + 全套插件 ~100MB）和构建/CI 维护成本。

**重启条件**：若未来遇到 FFmpeg 内核无法播放的个别文件，可从 git 历史找回 `vlc_video_engine.{h,cpp}` 及 CMake/CI 中的 VLC 集成（本次移除前的提交）。
