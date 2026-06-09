cmake_minimum_required(VERSION 3.16)
project(LumenArc VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# --- Qt6 ---
set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};$ENV{QT6_DIR}")
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)

# --- libVLC ---
set(VLC_SDK_PATH "" CACHE PATH "Path to VLC SDK root (contains include/ and lib/)")
if(NOT VLC_SDK_PATH AND DEFINED ENV{VLC_SDK_PATH})
    set(VLC_SDK_PATH "$ENV{VLC_SDK_PATH}")
endif()

set(VLC_SEARCH_PATHS
    "${VLC_SDK_PATH}"
    "${VLC_SDK_PATH}/sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/vlc_extracted/vlc-3.0.21/sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/vlc_extracted/vlc-3.0.20/sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/vlc_sdk"
    # Windows
    "C:/Program Files/VideoLAN/VLC/sdk"
    "C:/Program Files (x86)/VideoLAN/VLC/sdk"
    "$ENV{USERPROFILE}/vlc_sdk"
    # macOS
    "/Applications/VLC.app/Contents/MacOS"
    "/Applications/VLC.app/Contents/MacOS/include"
    "/opt/homebrew/opt/vlc"
    "/opt/homebrew/opt/vlc/lib"
    "/opt/homebrew/opt/vlc/include"
    "/usr/local/opt/vlc"
    "/usr/local/opt/vlc/lib"
    "/usr/local/opt/vlc/include"
)

set(VLC_INCLUDE_DIR)
set(VLC_LIBRARY)

foreach(base ${VLC_SEARCH_PATHS})
    if(NOT base)
        continue()
    endif()
    if(EXISTS "${base}/include/vlc/vlc.h")
        set(VLC_INCLUDE_DIR "${base}/include")
        # Windows: look for .lib or .dll.a
        if(EXISTS "${base}/lib/libvlc.lib")
            set(VLC_LIBRARY "${base}/lib/libvlc.lib")
        elseif(EXISTS "${base}/libvlc.lib")
            set(VLC_LIBRARY "${base}/libvlc.lib")
        elseif(EXISTS "${base}/lib/libvlc.dll.a")
            set(VLC_LIBRARY "${base}/lib/libvlc.dll.a")
        # macOS: look for .dylib
        elseif(EXISTS "${base}/lib/libvlc.dylib")
            set(VLC_LIBRARY "${base}/lib/libvlc.dylib")
        elseif(EXISTS "${base}/libvlc.dylib")
            set(VLC_LIBRARY "${base}/libvlc.dylib")
        endif()
        if(VLC_INCLUDE_DIR AND VLC_LIBRARY)
            break()
        endif()
    endif()
endforeach()

if(NOT VLC_INCLUDE_DIR OR NOT VLC_LIBRARY)
    if(WIN32)
        message(FATAL_ERROR
            "libVLC not found. Please set VLC_SDK_PATH to the VLC SDK root directory\n"
            "(the folder containing include/vlc/vlc.h and lib/libvlc.lib).\n"
            "You can download the VLC Windows SDK from:\n"
            "  https://download.videolan.org/pub/videolan/vlc/3.0.21/win64/vlc-3.0.21-win64.zip\n"
            "Or run: python setup_deps.py"
        )
    else()
        message(FATAL_ERROR
            "libVLC not found. Please set VLC_SDK_PATH to the VLC SDK root directory.\n"
            "On macOS, install VLC: brew install vlc\n"
            "Or download from: https://download.videolan.org/pub/videolan/vlc/3.0.21/macosx/"
        )
    endif()
endif()

message(STATUS "libVLC include: ${VLC_INCLUDE_DIR}")
message(STATUS "libVLC library: ${VLC_LIBRARY}")

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
    src/domain/region_model.cpp
    src/domain/timeline_model.cpp
    src/infrastructure/vlc_video_engine.cpp
    src/infrastructure/python_analysis_engine.cpp
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
    src/domain/analysis_snapshot.h
    src/domain/region_model.h
    src/domain/timeline_model.h
    src/infrastructure/ivideo_engine.h
    src/infrastructure/ianalysis_engine.h
    src/infrastructure/vlc_video_engine.h
    src/infrastructure/python_analysis_engine.h
)

# --- Platform-specific executable ---
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})
elseif(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE "app.icns")
    set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/app.icns")
    set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${APP_ICON})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "LumenArc"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.2"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.2 beta"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.firetech.lumenarc"
    )
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VLC_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Charts
    ${VLC_LIBRARY}
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32
        winmm
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

    # Copy libVLC runtime
    get_filename_component(VLC_DLL_DIR "${VLC_LIBRARY}" DIRECTORY)
    get_filename_component(VLC_DLL_DIR "${VLC_DLL_DIR}" DIRECTORY)
    get_filename_component(VLC_DLL_DIR "${VLC_DLL_DIR}" DIRECTORY)
    set(VLC_DLL_SRC "${VLC_DLL_DIR}")
    set(VLC_PLUGINS_SRC "${VLC_DLL_DIR}/plugins")

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VLC_DLL_SRC}/libvlc.dll"
            "${VLC_DLL_SRC}/libvlccore.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying libVLC DLLs..."
    )

    if(EXISTS "${VLC_PLUGINS_SRC}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VLC_PLUGINS_SRC}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins"
            COMMENT "Copying VLC plugins..."
        )
    endif()

    # Copy Python analysis script
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/analyze_video.py"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying analyze_video.py..."
    )

    # Copy app icon
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/app.ico"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying app.ico..."
    )
endif()

# --- macOS deployment ---
if(APPLE)
    # macdeployqt for Qt frameworks
    find_program(MACDEPLOYQT macdeployqt PATHS "$ENV{QT6_DIR}/bin" "${Qt6Core_DIR}/../../../bin")
    if(MACDEPLOYQT)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${MACDEPLOYQT}"
                "$<TARGET_FILE:${PROJECT_NAME}>"
                -qmfiles="${CMAKE_CURRENT_SOURCE_DIR}/portable/translations"
            COMMENT "Running macdeployqt..."
        )
    endif()

    # Copy libVLC dylibs to .app bundle
    get_filename_component(VLC_LIB_DIR "${VLC_LIBRARY}" DIRECTORY)
    get_filename_component(VLC_BASE_DIR "${VLC_LIB_DIR}" DIRECTORY)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VLC_BASE_DIR}/lib/libvlc.dylib"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../MacOS/"
        COMMENT "Copying libvlc.dylib..."
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VLC_BASE_DIR}/lib/libvlccore.dylib"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../MacOS/"
        COMMENT "Copying libvlccore.dylib..."
    )

    if(EXISTS "${VLC_BASE_DIR}/plugins")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VLC_BASE_DIR}/plugins"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../MacOS/plugins"
            COMMENT "Copying VLC plugins..."
        )
    endif()

    # Copy Python analysis script
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/analyze_video.py"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../MacOS/"
        COMMENT "Copying analyze_video.py..."
    )

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
