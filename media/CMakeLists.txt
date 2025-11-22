cmake_minimum_required(VERSION 3.16)

project(media VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)


# ==========================================
# 1. 查找 Qt 模块 (关键修正：加上 Multimedia)
# ==========================================
find_package(Qt6 REQUIRED COMPONENTS Quick)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Multimedia) # <--- 之前报错就是因为缺了这行！

qt_standard_project_setup(REQUIRES 6.5) # 建议稍微降低一点版本要求以兼容旧环境，或者保持 6.8

qt_add_executable(appmedia
    main.cpp
)

# ==========================================
# 2. 定义 QML 模块和 C++ 源文件
# ==========================================
qt_add_qml_module(appmedia
    URI media
    VERSION 1.0
    QML_FILES
    Main.qml
    SOURCES
    videoplayer.h
    videoplayer.cpp
    audiodecoder.h
    audiodecoder.cpp
    videodecoder.h
    videodecoder.cpp
    decoderworker.h
    decoderworker.cpp
    ffmpeg_resource_manager.h
    filemanager.h
    filemanager.cpp
    imagedecoder.h
    imagedecoder.cpp
    # 如果你有 videosynthesizer 类，也记得加在这里
    # videosynthesizer.h
    # videosynthesizer.cpp
)

# ==========================================
# 3. FFmpeg 路径配置
# ==========================================
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/3rdparty/ffmpeg")
include_directories(${FFMPEG_DIR}/include)

# ==========================================
# 4. 链接所有库 (Qt + FFmpeg)
# ==========================================
# 注意：如果你用的是 MinGW 且 lib 文件夹里是 .dll.a 结尾的文件，
# 请把下面的 .lib 后缀改为 .dll.a
target_link_libraries(appmedia PRIVATE
    Qt6::Quick
    Qt6::Multimedia

    # FFmpeg 库
    ${FFMPEG_DIR}/lib/avcodec.lib
    ${FFMPEG_DIR}/lib/avformat.lib
    ${FFMPEG_DIR}/lib/avutil.lib
    ${FFMPEG_DIR}/lib/swscale.lib
    ${FFMPEG_DIR}/lib/swresample.lib # 音频重采样库
)

# ==========================================
# 性能优化编译选项
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
# 5. 系统相关配置 (保持不变)
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
