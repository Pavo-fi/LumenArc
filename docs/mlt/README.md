# MLT 合成导出引擎集成（合成导出器 P1 基建）

> 2026-09-03 拍板：编辑器定位 = **分析成果合成导出器**（非通用剪辑器），
> 取代原分段导出；入口 主窗「工具 → 合成导出」；分析演示片强制"非原始证据"角标。

## 定位与架构

```
合成模型(Composition JSON) → MLT XML 生成器 → melt.exe(子进程) → 成品 MP4
预览：复用主视口帧管线（叠加层同渲染器画到预览帧，零 MLT 依赖）
```

## 构建（build_tmp/mlt_build.bat，可重复）

- 源码：MLT master（v7.40 后，含 FFmpeg 8/9 兼容补丁；**v7.40 tag 不能用于 FFmpeg 8**——
  `AVCodec::sample_fmts/pix_fmts` 已移除，master 用 `avcodec_get_supported_config`）
- MSVC 2022 + **vcpkg 工具链**（`-DVCPKG_MANIFEST_MODE=OFF`，否则它自己编 ffmpeg 半小时起）
- **必须 `CMAKE_C_FLAGS="/utf-8"`**：mlt_repository.c 注释含 UTF-8 省略号，GBK 代码页
  下 MSVC 把声明行吃掉（报 qt_module_count 未声明）——和本项目中文注释坑同族
- 模块白名单（LGPL）：core + avformat + xml + plus；其余全 OFF
  （normalize/resample/rubberband/vidstab/xine/openfx/plusgpl/qt6 = **GPL，禁**）
- vcpkg 依赖：libxml2 pthreads dirent dlfcn-win32 sdl2（melt.exe 在 Windows 无条件
  include SDL.h）libebur128（装了就走外部库，绕过内置 ebur128 的 sys/cdefs.h POSIX 依赖）
- **编码用 FFmpeg DLL 要用 BtbN gpl-shared 全功能版**（third_party/ffmpeg 的 DLL 是
  纯解码精简版，无 x264 → melt 报 "video codec libx264 unrecognised" 且输出无视频流）
- melt 运行需 env：MLT_REPOSITORY（lib/mlt）MLT_DATA（share/mlt）MLT_PROFILES_PATH；
  dll 与 melt.exe 同目录（mlt-7/mlt++-7/av*/sw*/libxml2/pthreadVC3/dl/iconv-2/charset-1/z/
  ebur128/SDL2）

## 实测定论（mlt_smoke 冒烟台，2026-09-03）

| 能力 | 结论 |
|---|---|
| 片段序列（in/out 裁剪拼接）→ x264+aac MP4 | ✅ 帧数精确 |
| 视频轨作 b_track 画中画合成 | ✅（composite/affine 均可） |
| **PNG/PNG 序列 alpha 叠层**（composite a_track=0 b_track=1） | ✅ 半透明数值精确 |
| qtrle(argb) MOV 作叠层 | ❌ alpha 被链路归一化吞掉（换 PNG 序列） |
| dynamictext/text/subtitle 文本滤镜 | ❌ 运行时依赖 qt/gtk 模块（GPL 已排除） |
| XML 工程用相对路径（含 `..`） | ❌ 加载静默失败 rc=3；**一律绝对路径** |

**因此叠层协议 = PNG 序列**（QPainter 渲染，校正时间角标/曲线/ROI/水印全走它）；
序列耗尽后 b 轨自动透明（eof=pause 不 hold，无碍）。

## 导出双模式（拍板）

- **证据片段**：不走 melt——现有无损直拷引擎 + 侧车 JSON（校准参数+SHA256），像素零改动
- **分析演示片**：melt 合成，强制右上角「分析演示材料·非原始证据」角标

## 包体

mlt 运行目录当前 ~167MB（BtbN 全功能 DLL 占大头）。P3 可自编译瘦身版
shared ffmpeg（仅 h264/aac/png/mov/mp4）压到 ~40MB。打包时 pack_release.py 需加
mlt/ 目录校验项。
