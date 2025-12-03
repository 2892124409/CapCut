cmake_minimum_required(VERSION 3.16)

project(media VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# 1. 查找 Qt 模块
# ==========================================
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Multimedia OpenGL)

qt_standard_project_setup(REQUIRES 6.5)

# ==========================================
# 2. FFmpeg 路径配置
# ==========================================
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/../3rdparty/ffmpeg")
include_directories(${FFMPEG_DIR}/include)

set(MEDIA_CORE_SOURCES
    audiodecoder.cpp
    audioplayer.cpp
    demuxer.cpp
    filemanager.cpp
    imageviewer.cpp
    media_api.cpp
    mediacontroller.cpp
    mediacontroller.h
    videodecoder.cpp
    videoplayer_impl.cpp
    audiodecoder.h
    audioplayer.h
    demuxer.h
    ffmpeg_resource_manager.h
    filemanager.h
    imageviewer.h
    imediaplayer.h
    media_api.h
    videodecoder.h
    videoplayer_impl.h
)

# ==========================================
# 3. media_core 库（供测试程序和宿主项目复用）
# ==========================================
qt_add_library(media_core STATIC)
target_sources(media_core PRIVATE ${MEDIA_CORE_SOURCES})
target_include_directories(media_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(media_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Multimedia
    Qt6::OpenGL
    ${FFMPEG_DIR}/lib/avcodec.lib
    ${FFMPEG_DIR}/lib/avformat.lib
    ${FFMPEG_DIR}/lib/avutil.lib
    ${FFMPEG_DIR}/lib/swscale.lib
    ${FFMPEG_DIR}/lib/swresample.lib
)

# ==========================================
# 4. 将 QML 资源打包到 media_core 模块
# ==========================================
qt_add_qml_module(media_core
    URI media
    VERSION 1.0
    QML_FILES
        Main.qml
        MediaView.qml
)

# ==========================================
# 5. 测试可执行程序（继续加载模块中的 Main.qml）
# ==========================================
qt_add_executable(appmedia
    main.cpp
)
target_link_libraries(appmedia PRIVATE media_core Qt6::Quick)

# ==========================================
# 6. 性能优化编译选项
# ==========================================
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(appmedia PRIVATE 
        -O2
        -march=native
        -ffast-math
        -DNDEBUG
    )
    message(STATUS "启用Release模式优化选项")
else()
    target_compile_options(appmedia PRIVATE 
        -O0
        -g3
        -DDEBUG
    )
    message(STATUS "启用Debug模式编译选项")
endif()

# ==========================================
# 7. 系统相关配置
# ==========================================
set_target_properties(appmedia PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS appmedia
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
