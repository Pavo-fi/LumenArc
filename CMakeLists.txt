cmake_minimum_required(VERSION 3.16)
project(LumenArc VERSION 1.3.0 LANGUAGES CXX)

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
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Multimedia Concurrent Test)

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


# --- Sources ---
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/videowidget.cpp
    src/chartpanel.cpp
    src/magnifierwidget.cpp
    src/snapshotoverlay.cpp
    src/pinnedwidget.cpp
    src/i18n.cpp
    src/aboutdialog.cpp
    src/videolistpanel.cpp
    src/preprocesswindow.cpp
    src/cliptimelinewidget.cpp
    src/spectrogrampanel_enhanced.cpp
    src/videostatemanager.cpp
    src/domain/region_model.cpp
    src/domain/polygon_model.cpp
    src/domain/guide_line_model.cpp
    src/domain/timeline_model.cpp
    src/domain/filename_timestamp.cpp
    src/domain/smart_sorter.cpp
    src/domain/concat_precheck.cpp
    src/domain/evidence_report.cpp
    src/domain/time_calibration.cpp
    src/domain/time_piecewise.cpp
    src/domain/case_model.cpp
    src/app/case_manager.cpp
    src/casedock.cpp
    src/casedialogs.cpp
    src/startpagewidget.cpp
    src/multicamview.cpp
    src/app/cam_timeline.cpp
    src/app/preprocessing_coordinator.cpp
    src/app/calibration_service.cpp
    src/timesettingsdialog.cpp
    src/infrastructure/ffmpeg_video_engine.cpp
    src/infrastructure/python_analysis_engine.cpp
    src/infrastructure/media_probe_engine.cpp
    src/infrastructure/timestamp_ocr_engine.cpp
    src/infrastructure/concat_engine.cpp
    src/infrastructure/transcode_engine.cpp
    src/resources.qrc
)

# Windows resource file (not needed on macOS)
if(WIN32)
    list(APPEND SOURCES src/app.rc)
endif()

set(HEADERS
    src/mainwindow.h
    src/videowidget.h
    src/chartpanel.h
    src/magnifierwidget.h
    src/snapshotoverlay.h
    src/pinnedwidget.h
    src/i18n.h
    src/aboutdialog.h
    src/videolistpanel.h
    src/preprocesswindow.h
    src/cliptimelinewidget.h
    src/spectrogrampanel_enhanced.h
    src/videostatemanager.h
    src/domain/analysis_snapshot.h
    src/domain/region_model.h
    src/domain/region_shape.h
    src/domain/polygon_model.h
    src/domain/guide_line.h
    src/domain/guide_line_model.h
    src/domain/timeline_model.h
    src/domain/probe_result.h
    src/domain/ocr_result.h
    src/domain/sort_model.h
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
    src/startpagewidget.h
    src/multicamview.h
    src/app/cam_timeline.h
    src/app/preprocessing_coordinator.h
    src/app/calibration_service.h
    src/timesettingsdialog.h
    src/infrastructure/ivideo_engine.h
    src/infrastructure/ianalysis_engine.h
    src/infrastructure/ffmpeg_video_engine.h
    src/infrastructure/python_analysis_engine.h
    src/infrastructure/media_probe_engine.h
    src/infrastructure/timestamp_ocr_engine.h
    src/infrastructure/concat_engine.h
    src/infrastructure/transcode_engine.h
)

# --- Headless engine test harness ---
add_executable(lumenarc_engine_test
    tests/engine_test_main.cpp
    src/infrastructure/ivideo_engine.h
    src/infrastructure/ffmpeg_video_engine.h
    src/infrastructure/ffmpeg_video_engine.cpp
)
target_include_directories(lumenarc_engine_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FFMPEG_INCLUDE_DIR}
)
target_link_libraries(lumenarc_engine_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    ${FFMPEG_LIBRARIES}
)

# --- Headless preprocess domain test harness ---
add_executable(lumenarc_preprocess_test
    tests/preprocess_test_main.cpp
    src/domain/filename_timestamp.cpp
    src/domain/smart_sorter.cpp
    src/domain/concat_precheck.cpp
    src/domain/evidence_report.cpp
)
target_include_directories(lumenarc_preprocess_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(lumenarc_preprocess_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets   # SortableFileTable 拖拽插入语义 GUI 单测（offscreen）
)

# --- Headless calibration domain test harness ---
add_executable(lumenarc_calibration_test
    tests/calibration_test_main.cpp
    src/domain/time_calibration.cpp
    src/domain/time_piecewise.cpp
)
target_include_directories(lumenarc_calibration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(lumenarc_calibration_test PRIVATE
    Qt6::Core
)

# --- Piecewise reconstruction domain test (v1.2.1) ---
add_executable(lumenarc_piecewise_test
    tests/piecewise_test_main.cpp
    src/domain/time_piecewise.cpp
)
target_include_directories(lumenarc_piecewise_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(lumenarc_piecewise_test PRIVATE
    Qt6::Core
)

# --- Reconstruction golden-case integration test (B3 elevator clip) ---
add_executable(lumenarc_reconstruction_test
    tests/reconstruction_integration_main.cpp
    src/app/calibration_service.cpp
    src/infrastructure/ianalysis_engine.h
    src/infrastructure/media_probe_engine.cpp
    src/infrastructure/timestamp_ocr_engine.cpp
    src/infrastructure/python_analysis_engine.cpp
    src/i18n.cpp
    src/domain/time_calibration.cpp
    src/domain/time_piecewise.cpp
    src/domain/probe_result.h
)
target_include_directories(lumenarc_reconstruction_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FFMPEG_INCLUDE_DIR}
)
target_link_libraries(lumenarc_reconstruction_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent   # python_analysis_engine.cpp 后台解析
    ${FFMPEG_LIBRARIES}
)

# --- OCR engine runAtPositions smoke test (bundled python/ffmpeg subprocess) ---
add_executable(lumenarc_ocr_atpositions_test
    tests/ocr_atpositions_test_main.cpp
    src/infrastructure/ianalysis_engine.h
    src/infrastructure/timestamp_ocr_engine.cpp
    src/infrastructure/python_analysis_engine.cpp
    src/i18n.cpp
    src/domain/time_calibration.cpp
    src/domain/time_piecewise.cpp
)
target_include_directories(lumenarc_ocr_atpositions_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FFMPEG_INCLUDE_DIR}
)
target_link_libraries(lumenarc_ocr_atpositions_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent   # python_analysis_engine.cpp 后台解析
    ${FFMPEG_LIBRARIES}
)

# --- Preprocess engine integration test (real ffmpeg/Python subprocesses) ---
add_executable(lumenarc_preprocess_integration
    tests/preprocess_integration_main.cpp
    src/infrastructure/ianalysis_engine.h
    src/infrastructure/media_probe_engine.cpp
    src/infrastructure/timestamp_ocr_engine.cpp
    src/infrastructure/concat_engine.cpp
    src/infrastructure/transcode_engine.cpp
    src/infrastructure/python_analysis_engine.cpp
    src/i18n.cpp
    src/domain/filename_timestamp.cpp
    src/domain/smart_sorter.cpp
    src/domain/concat_precheck.cpp
    src/domain/evidence_report.cpp
)
target_include_directories(lumenarc_preprocess_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FFMPEG_INCLUDE_DIR}
)
target_link_libraries(lumenarc_preprocess_integration PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent   # python_analysis_engine.cpp 后台解析
    ${FFMPEG_LIBRARIES}
)

# --- Offscreen .vla load → chart rebuild chain test ---
add_executable(lumenarc_vla_test
    tests/vla_load_test_main.cpp
    src/chartpanel.cpp
    src/i18n.cpp
    src/domain/region_model.cpp
    src/domain/polygon_model.cpp
    src/domain/timeline_model.cpp
    src/domain/guide_line_model.cpp
    src/domain/time_calibration.cpp
    src/domain/time_piecewise.cpp
)
target_include_directories(lumenarc_vla_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(lumenarc_vla_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Charts
)

# --- Case module domain test (v1.3.0) ---
add_executable(lumenarc_case_test
    tests/case_test_main.cpp
    src/domain/case_model.cpp
    src/app/case_manager.cpp
    src/videostatemanager.cpp   # M3 任务13：migrateKey 键迁移测试
    src/app/cam_timeline.cpp    # M3 任务14：buildCamLanes 装配测试
    src/domain/timeline_model.cpp
    src/domain/time_calibration.cpp
    src/domain/time_piecewise.cpp
)
target_include_directories(lumenarc_case_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(lumenarc_case_test PRIVATE
    Qt6::Core
    Qt6::Gui   # QRectF
)

# --- Case system end-to-end self-check (v1.3.0 封版全面自检) ---
add_executable(lumenarc_case_e2e
    tests/case_e2e_main.cpp
    src/domain/case_model.cpp
    src/app/case_manager.cpp
    src/videostatemanager.cpp
)
target_include_directories(lumenarc_case_e2e PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(lumenarc_case_e2e PRIVATE
    Qt6::Core
    Qt6::Gui
)

# --- UI chain test (offscreen): GO/框选全链路（v1.2.x UX，真实对话框+服务） ---
add_executable(lumenarc_ui_chain_test
    tests/ui_chain_test_main.cpp
    src/timesettingsdialog.cpp
    src/videowidget.cpp
    src/app/calibration_service.cpp
    src/infrastructure/ivideo_engine.h
    src/infrastructure/ianalysis_engine.h
    src/infrastructure/timestamp_ocr_engine.cpp
    src/infrastructure/python_analysis_engine.cpp
    src/infrastructure/media_probe_engine.cpp
    src/i18n.cpp
    src/domain/time_calibration.cpp
    src/domain/time_piecewise.cpp
    src/domain/region_model.cpp
    src/domain/polygon_model.cpp
    src/domain/guide_line_model.cpp
)
target_include_directories(lumenarc_ui_chain_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FFMPEG_INCLUDE_DIR}
)
target_link_libraries(lumenarc_ui_chain_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test       # QTest 鼠标事件注入
    Qt6::Concurrent   # python_analysis_engine.cpp 后台解析
    ${FFMPEG_LIBRARIES}
)

# 测试以 -platform offscreen 运行：exe 同目录 platforms/ 需有 qoffscreen.dll
#（windeployqt 只部署 qwindows.dll，否则 Qt 弹“no Qt platform plugin”错误框）
if(WIN32)
    set(_qoffscreen_dll "${Qt6_DIR}/../../../plugins/platforms/qoffscreen.dll")
    if(EXISTS "${_qoffscreen_dll}")
        add_custom_command(TARGET lumenarc_ui_chain_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:lumenarc_ui_chain_test>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_qoffscreen_dll}"
                "$<TARGET_FILE_DIR:lumenarc_ui_chain_test>/platforms/")
    endif()
endif()

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
        MACOSX_BUNDLE_BUNDLE_VERSION "1.3.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.3.0"
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
    )
    target_link_libraries(lumenarc_engine_test PRIVATE dxgi d3d11)
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

    # Copy Python analysis script
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/analyze_video.py"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying analyze_video.py..."
    )

    # Copy preprocess OCR script
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/probe_timestamps.py"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying probe_timestamps.py..."
    )

    # Copy user manual PDF (mac: 与可执行文件同目录，供 帮助→使用手册 打开)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/追光者 Lumen Arc v1.0 — 操作手册.pdf")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/追光者 Lumen Arc v1.0 — 操作手册.pdf"
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
    set(SYSDLLS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/portable/sysdlls")
    if(EXISTS "${SYSDLLS_SRC}/vcruntime140_1.dll" AND EXISTS "${SYSDLLS_SRC}/dxgi.dll")
        file(GLOB SYSDLLS "${SYSDLLS_SRC}/*.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SYSDLLS}
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            COMMENT "Copying system runtime DLLs for compatibility..."
        )
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

    # Copy user manual PDF
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/追光者 Lumen Arc v1.0 — 操作手册.pdf")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/追光者 Lumen Arc v1.0 — 操作手册.pdf"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            COMMENT "Copying user manual PDF..."
        )
    endif()
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


    # Copy Python analysis script
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/analyze_video.py"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../MacOS/"
        COMMENT "Copying analyze_video.py..."
    )

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
