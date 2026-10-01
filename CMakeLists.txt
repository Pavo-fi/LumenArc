cmake_minimum_required(VERSION 3.16)
project(LumenArc VERSION 1.18.0 LANGUAGES CXX)
add_compile_definitions(APP_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 全部源文件为无 BOM UTF-8：告知 MSVC 源字符集（中文路径/文案正确性）
if(MSVC)
    add_compile_options(/utf-8)
endif()

# --- macOS deployment target ---
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# --- Qt6 ---
set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};$ENV{QT6_DIR}")
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Multimedia Concurrent Network)

# --- FFmpeg (LGPL shared build: bin/ include/ lib/) ---
set(FFMPEG_SDK_PATH "" CACHE PATH "Path to FFmpeg SDK root (contains include/ and lib/)")
if(NOT FFMPEG_SDK_PATH AND DEFINED ENV{FFMPEG_SDK_PATH})
    set(FFMPEG_SDK_PATH "$ENV{FFMPEG_SDK_PATH}")
endif()
if(NOT FFMPEG_SDK_PATH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/include/libavformat/avformat.h")
    set(FFMPEG_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")
endif()

set(FFMPEG_INCLUDE_DIR)
set(FFMPEG_LIBRARIES)
if(FFMPEG_SDK_PATH AND EXISTS "${FFMPEG_SDK_PATH}/include/libavformat/avformat.h")
    set(FFMPEG_INCLUDE_DIR "${FFMPEG_SDK_PATH}/include")
    foreach(comp avformat avcodec avutil swscale swresample)
        if(WIN32)
            set(libfile "${FFMPEG_SDK_PATH}/lib/${comp}.lib")
        elseif(APPLE)
            set(libfile "${FFMPEG_SDK_PATH}/lib/lib${comp}.dylib")
        else()
            set(libfile "${FFMPEG_SDK_PATH}/lib/lib${comp}.so")
        endif()
        if(EXISTS "${libfile}")
            list(APPEND FFMPEG_LIBRARIES "${libfile}")
        endif()
    endforeach()
endif()
if(NOT FFMPEG_INCLUDE_DIR OR NOT FFMPEG_LIBRARIES)
    message(FATAL_ERROR
        "FFmpeg SDK not found. Set FFMPEG_SDK_PATH to the FFmpeg root\n"
        "(the folder containing include/libavformat/avformat.h and lib/).\n"
        "Download the LGPL shared build from:\n"
        "  https://github.com/BtbN/FFmpeg-Builds/releases (ffmpeg-master-latest-win64-lgpl-shared.zip)\n"
        "and extract it to third_party/ffmpeg."
    )
endif()
message(STATUS "FFmpeg include: ${FFMPEG_INCLUDE_DIR}")
message(STATUS "FFmpeg libraries: ${FFMPEG_LIBRARIES}")


# --- 微变纯计算 / 基准提取源（app 目标共用）---
# 统一走本变量，避免多处手抄导致漏链（microdiff::roiFrameStats /
# aggregateSeconds / detectOnset / extractMicroDiffBaseline 符号无法解析）。
set(MICRODIFF_SOURCES
    src/domain/microdiff_core.cpp
    src/domain/microdiff_curve.cpp
    src/infrastructure/microdiff_baseline.cpp
)

# --- Sources ---
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow_ui.cpp          # v1.17.0 P-79：构造体/菜单/工具栏（多 TU 拆分）
    src/mainwindow_wiring.cpp      # v1.17.0 P-79：信号接线 8 域
    src/mainwindow_case.cpp        # v1.17.0 P-79：案件 UI 群+多机入口
    src/mainwindow_magnifier.cpp   # v1.17.0 P-79：放大镜群
    src/mainwindow_snapshot.cpp    # v1.17.0 P-79：证据快照
    src/mainwindow_export.cpp      # v1.17.0 P-79：导出三入口
    src/videowidget.cpp
    src/chartpanel.cpp
    src/magnifierwidget.cpp
    src/snapshotoverlay.cpp
    src/playbackadjustpanel.cpp
    src/pinnedwidget.cpp
    src/infrastructure/crash_handler.cpp
    src/infrastructure/cloud_account.cpp
    src/infrastructure/credential_store.cpp
    src/logindialog.cpp
    src/feedbackdialog.cpp
    src/accountdialog.cpp
    src/fullscreenvideowindow.cpp
    src/displayadjust.cpp
    src/microdiff.cpp              # 2026-09-10：微变显示末级（原彩+假彩色叠加）
    src/microdiffdialog.cpp        # 2026-09-10：微变面板（等级/基准段/显示模式）
    ${MICRODIFF_SOURCES}           # 微变纯计算核心/曲线/基准提取（统一定义，见文件上方）
    src/i18n.cpp
    src/aboutdialog.cpp
    src/videolistpanel.cpp
    src/preprocesswindow.cpp
    src/cliptimelinewidget.cpp
    src/spectrogrampanel_enhanced.cpp
    src/frame_annotation.cpp   # v1.17.0 P-80 C0：帧映射/覆盖层烧录纯模块（自 OverlayWidget 迁出）
    src/app/snapshot_composer.cpp   # v1.17.0 P-80 C1：证据快照合成纯模块
    src/videostatemanager.cpp
    src/domain/roi_model.cpp
    src/domain/guide_line_model.cpp
    src/domain/timeline_model.cpp
    src/domain/overlap_cut.cpp
    src/domain/filename_timestamp.cpp
    src/domain/smart_sorter.cpp
    src/domain/concat_precheck.cpp
    src/domain/concat_naming.cpp
    src/domain/dedupe_plan.cpp
    src/domain/run_summary.cpp
    src/domain/evidence_report.cpp
    src/domain/time_calibration.cpp
    src/domain/truth_time_parse.cpp   # v1.12.5 北京时间对时文本解析
    src/domain/time_piecewise.cpp
    src/domain/audio_denoise.cpp   # P-54 谱门控降噪（libav 引擎原生）
    src/domain/case_model.cpp
    src/app/case_manager.cpp
    src/casedock.cpp
    src/imagepreviewdialog.cpp
    src/app/case_open_panel.cpp
    src/casedialogs.cpp
    src/multicamview.cpp
    src/multicamplaybackwindow.cpp
    src/camtilewidget.cpp
    src/app/cam_timeline.cpp
    src/app/preprocessing_coordinator.cpp
    src/app/calibration_service.cpp
    src/app/analysis_controller.cpp
    src/app/project_io.cpp
    src/app/uistate.cpp
    src/app/video_session_manager.cpp
    src/domain/task_registry.cpp
    src/app/analysis_task_service.cpp
    src/app/multicam_sync_service.cpp
    src/app/segment_switch_engine.cpp
    src/app/segment_switch_engine.h
    src/infrastructure/zip_store_writer.cpp
    src/infrastructure/zip_store_writer.h
    src/infrastructure/docx_writer.cpp
    src/infrastructure/docx_writer.h
    src/app/report_service.cpp
    src/app/report_service.h
    src/app/report_docx_builder.cpp
    src/app/report_docx_builder.h
    src/app/report_html_builder.cpp      # P-26 复活：离线 HTML 报告（含时间轴章节）
    src/app/report_html_builder.h
    src/domain/timeline_events.cpp       # 报告时间轴装配（标签+快照 → 事件/阶段）
    src/domain/timeline_events.h
    src/domain/report_data.h
    src/domain/report_preflight.h
    src/reportpreflightdialog.cpp
    src/reportpreflightdialog.h
    src/domain/site_map.h
    src/infrastructure/site_map_render.cpp
    src/infrastructure/site_map_render.h
    src/sitemapeditordialog.cpp
    src/sitemapeditordialog.h
    src/timesettingsdialog.cpp
    src/calibphotodialog.cpp        # v1.12.5 北京时间对时：校时图片两框框选
    src/infrastructure/ffmpeg_video_engine.cpp
    src/infrastructure/tool_paths.cpp
    src/infrastructure/integrity_checker.cpp
    src/infrastructure/libav_analysis_engine.cpp
    src/infrastructure/encoder_probe.cpp
    src/infrastructure/media_probe_engine.cpp
    src/infrastructure/timestamp_ocr_engine.cpp
    src/infrastructure/concat_engine.cpp
    src/infrastructure/transcode_engine.cpp
    src/infrastructure/segment_export_engine.cpp   # P-68 选段分段变速导出
    src/infrastructure/compose_render.cpp          # 合成导出 P2：ROI/曲线滚动条烧录
    src/segmentexportdialog.cpp
    src/composeworkbench.cpp
    # --- 监控直播（2026-09-11）：录像机 RTSP 接入 + 辅助线/截图叠加 + 无损录制 ---
    src/domain/live/live_source.cpp
    src/infrastructure/live/live_stream_engine.cpp
    src/infrastructure/live/live_recorder.cpp
    src/livemonitorwindow.cpp
    src/mainwindow_live.cpp          # v1.17.0 P-79 风格：主窗口仅保留入口接线
    src/resources.qrc
)

# Windows resource file (not needed on macOS)
if(WIN32)
    list(APPEND SOURCES src/app.rc)
endif()

set(HEADERS
    src/mainwindow.h
    src/videowidget.h
    src/domain/task_registry.h
    src/app/analysis_task_service.h
    src/app/multicam_sync_service.h
    src/domain/sync_model.h
    src/app/analysis_controller.h
    src/app/project_io.h
    src/app/uistate.h
    src/app/video_session_manager.h
    src/chartpanel.h
    src/magnifierwidget.h
    src/snapshotoverlay.h
    src/playbackadjustpanel.h
    src/pinnedwidget.h
    src/fullscreenvideowindow.h
    src/displayadjust.h
    src/microdiff.h
    src/microdiffdialog.h
    src/domain/microdiff_core.h
    src/domain/microdiff_curve.h
    src/i18n.h
    src/aboutdialog.h
    src/videolistpanel.h
    src/preprocesswindow.h
    src/cliptimelinewidget.h
    src/spectrogrampanel_enhanced.h
    src/videostatemanager.h
    src/domain/analysis_snapshot.h
    src/domain/roi_model.h
    src/domain/region_shape.h
    src/domain/guide_line.h
    src/domain/guide_line_model.h
    src/domain/timeline_model.h
    src/domain/probe_result.h
    src/domain/ocr_result.h
    src/domain/sort_model.h
    src/domain/dedupe_plan.h
    src/domain/run_summary.h
    src/domain/preprocess_task.h
    src/domain/filename_timestamp.h
    src/domain/smart_sorter.h
    src/domain/concat_precheck.h
    src/domain/evidence_report.h
    src/domain/preprocess_text.h
    src/domain/time_calibration.h
    src/domain/time_piecewise.h
    src/domain/case_model.h
    src/app/case_manager.h
    src/casedock.h
    src/casedialogs.h
    src/multicamview.h
    src/multicamplaybackwindow.h
    src/camtilewidget.h
    src/app/cam_timeline.h
    src/app/preprocessing_coordinator.h
    src/app/calibration_service.h
    src/timesettingsdialog.h
    src/infrastructure/ivideo_engine.h
    src/infrastructure/ianalysis_engine.h
    src/infrastructure/ffmpeg_video_engine.h
    src/infrastructure/tool_paths.h
    src/infrastructure/microdiff_baseline.h
    src/infrastructure/libav_analysis_engine.h
    src/infrastructure/encoder_probe.h
    src/infrastructure/media_probe_engine.h
    src/infrastructure/timestamp_ocr_engine.h
    src/infrastructure/concat_engine.h
    src/infrastructure/transcode_engine.h
    src/infrastructure/segment_export_engine.h
    src/infrastructure/compose_render.h
    src/infrastructure/crash_handler.h
    src/infrastructure/cloud_account.h
    src/infrastructure/credential_store.h
    src/logindialog.h
    src/feedbackdialog.h
    src/accountdialog.h
    src/segmentexportdialog.h
    src/composeworkbench.h
    src/domain/live/live_source.h
    src/infrastructure/live/live_stream_engine.h
    src/infrastructure/live/live_recorder.h
    src/livemonitorwindow.h
)

# --- Platform-specific executable ---
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})
elseif(APPLE)
    set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/app.icns")
    if(EXISTS "${APP_ICON}")
        set(MACOSX_BUNDLE_ICON_FILE "app.icns")
        set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${APP_ICON})
    else()
        add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS})
    endif()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "LumenArc"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.firetech.lumenarc"
    )
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FFMPEG_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Charts
    Qt6::OpenGL
    Qt6::Multimedia
    Qt6::Network      # 账号系统 HTTPS（Schannel 系统 TLS）
    Qt6::Concurrent   # 分析结果解析 / .vla 保存后台线程（分析结束不再卡死 UI）
    ${FFMPEG_LIBRARIES}
)


# Platform-specific libraries
if(WIN32)
    add_compile_definitions(NOMINMAX)   # d3d11.h→windows.h 的 min/max 宏会毒化 Qt 头
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32
        winmm
        dxgi
        d3d11
        dbghelp   # 崩溃黑匣子 MiniDumpWriteDump（系统库，零三方依赖）
    )
endif()

# --- Windows deployment ---
if(WIN32)
    find_program(WINDEPLOYQT windeployqt PATHS "$ENV{QT6_DIR}/bin" "${Qt6Core_DIR}/../../../bin")
    if(WINDEPLOYQT)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT}" "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Running windeployqt..."
        )
    endif()

    # Copy FFmpeg runtime DLLs
    if(FFMPEG_SDK_PATH AND EXISTS "${FFMPEG_SDK_PATH}/bin")
        file(GLOB FFMPEG_DLLS "${FFMPEG_SDK_PATH}/bin/av*.dll" "${FFMPEG_SDK_PATH}/bin/sw*.dll")
        if(FFMPEG_DLLS)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${FFMPEG_DLLS}
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                COMMENT "Copying FFmpeg DLLs..."
            )
        endif()
    endif()

    # Copy preprocess OCR script
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/probe_timestamps.py"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying probe_timestamps.py..."
    )

    # Copy Python deps one-click installer (老系统的备用路径；pack_release.py 必含项)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/setup_python_deps.bat")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/setup_python_deps.bat"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            COMMENT "Copying setup_python_deps.bat..."
        )
    endif()

    # Copy user manual PDF (mac: 与可执行文件同目录，供 帮助→使用手册 打开)
    # v1.16.0：文件名去版本号（手册随包永远最新）；由 MANUAL.md 经 Qt markdown 导出
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/追光者 Lumen Arc — 操作手册.pdf")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/追光者 Lumen Arc — 操作手册.pdf"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            COMMENT "Copying user manual PDF..."
        )
    endif()

    # Copy ffmpeg (if bundled)
    set(FFMPEG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/portable/ffmpeg")
    if(EXISTS "${FFMPEG_SRC}/ffmpeg.exe")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${FFMPEG_SRC}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/ffmpeg"
            COMMENT "Copying ffmpeg..."
        )
    endif()

    # Copy system runtime DLLs for compatibility with stripped Windows systems
    #
    # v1.18.0 修复（开箱即用红线）：原守卫为
    #   if(EXISTS ".../vcruntime140_1.dll" AND EXISTS ".../dxgi.dll")
    # 但 dxgi.dll 在 1130f0b（"accumulated cleanup - drop VLC engine"）被删除，
    # 守卫自此恒为假 —— POST_BUILD 拷贝静默失效，msvcp140/vcruntime140 等
    # VC++ 运行库从未进入 build/Release，导致 v1.16.0/v1.16.1/v1.17.0 包
    # 在未装 VC++ 运行库的干净 Windows 上双击即报 "VCRUNTIME140.dll 缺失"。
    # 现改为只依赖关键运行库，并在 glob 为空 / 关键文件缺失时**显式报错**，
    # 避免同类问题再次静默发生。
    set(SYSDLLS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/portable/sysdlls")
    if(EXISTS "${SYSDLLS_SRC}/vcruntime140_1.dll" AND EXISTS "${SYSDLLS_SRC}/msvcp140.dll")
        file(GLOB SYSDLLS "${SYSDLLS_SRC}/*.dll")
        list(LENGTH SYSDLLS _sysdlls_n)
        if(_sysdlls_n EQUAL 0)
            message(FATAL_ERROR
                "portable/sysdlls 存在但未匹配到任何 DLL —— 系统运行库将缺失，"
                "干净 Windows 无法启动。请检查该目录是否被误清空。")
        endif()
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SYSDLLS}
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            COMMENT "Copying ${_sysdlls_n} system runtime DLLs for compatibility..."
        )
    else()
        message(WARNING
            "portable/sysdlls 缺少 msvcp140.dll / vcruntime140_1.dll —— "
            "产物将不含 VC++ 运行库，干净 Windows 需先安装随包 vc_redist.x64.exe。")
    endif()

    # Copy app icon
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/app.ico"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying app.ico..."
    )

    # Copy lightchaser background
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lightchaser.jpg")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/lightchaser.jpg"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            COMMENT "Copying lightchaser.jpg..."
        )
    endif()

    # v1.18.0：此处原为 "…v1.0 — 操作手册.pdf" 的重复拷贝分支，文件早已不存在，
    # 属历史遗留死代码，已整块删除（手册由上方按当前文件名统一拷贝）。
    # CI 也曾用 "…v0.5 Beta…" 旧名找文件，导致包里缺手册——已同步修正。
endif()

# --- macOS deployment ---
if(APPLE)
    # macdeployqt for Qt frameworks
    find_program(MACDEPLOYQT macdeployqt PATHS "$ENV{QT6_DIR}/bin" "${Qt6Core_DIR}/../../../bin")
    if(MACDEPLOYQT)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${MACDEPLOYQT}"
                "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>"
            COMMENT "Running macdeployqt..."
        )
    endif()


    # Copy preprocess OCR script
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/probe_timestamps.py"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../MacOS/"
        COMMENT "Copying probe_timestamps.py..."
    )

    # Copy ffmpeg (if bundled)
    set(FFMPEG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/portable/ffmpeg")
    if(EXISTS "${FFMPEG_SRC}/ffmpeg")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${FFMPEG_SRC}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../MacOS/ffmpeg"
            COMMENT "Copying ffmpeg..."
        )
    endif()

    # Copy lightchaser background
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lightchaser.jpg")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/lightchaser.jpg"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../MacOS/"
            COMMENT "Copying lightchaser.jpg..."
        )
    endif()
endif()
