# VideoCreatorCpp 完全独立构建配置
# 不依赖任何主项目，可以直接编译运行

cmake_minimum_required(VERSION 3.16)
project(VideoCreatorCpp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置UTF-8编码支持
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 设置Qt6路径
set(CMAKE_PREFIX_PATH "D:/Qt/6.9.2/mingw_64")

# 查找Qt6
find_package(Qt6 REQUIRED COMPONENTS Core)

# 设置FFmpeg路径
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/../3rdparty/ffmpeg")
include_directories(${FFMPEG_DIR}/include)

# 添加可执行文件
qt_add_executable(VideoCreatorCpp
    src/main.cpp
    src/model/ConfigLoader.cpp
    src/model/ConfigLoader.h
    src/model/ProjectConfig.h
    src/engine/RenderEngine.cpp
    src/engine/RenderEngine.h
    src/ffmpeg_utils/AvFrameWrapper.h
    src/ffmpeg_utils/AvPacketWrapper.h
    src/ffmpeg_utils/FFmpegHeaders.h
)

# 链接依赖库
target_link_libraries(VideoCreatorCpp PRIVATE
    Qt6::Core
    ${FFMPEG_DIR}/lib/avcodec.lib
    ${FFMPEG_DIR}/lib/avformat.lib
    ${FFMPEG_DIR}/lib/avutil.lib
    ${FFMPEG_DIR}/lib/swscale.lib
    ${FFMPEG_DIR}/lib/swresample.lib
    ${FFMPEG_DIR}/lib/avfilter.lib
)

# 包含目录
target_include_directories(VideoCreatorCpp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg_utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

# 复制资源文件
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR}/bin)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/config.json DESTINATION ${CMAKE_BINARY_DIR}/bin)

# 创建输出目录
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin/output)

message(STATUS "VideoCreatorCpp独立构建配置完成")
message(STATUS "Qt路径: ${Qt6_DIR}")
message(STATUS "FFmpeg路径: ${FFMPEG_DIR}")
