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
    src/decoder/ImageDecoder.cpp
    src/decoder/ImageDecoder.h
    src/decoder/AudioDecoder.cpp
    src/decoder/AudioDecoder.h
    src/filter/EffectProcessor.cpp
    src/filter/EffectProcessor.h
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder
    ${CMAKE_CURRENT_SOURCE_DIR}/src/filter
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

# 复制资源文件
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR}/bin)
# 项目示例配置文件 (仓库中为 test_config.json)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_config.json")
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test_config.json DESTINATION ${CMAKE_BINARY_DIR}/bin)
else()
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/config.json DESTINATION ${CMAKE_BINARY_DIR}/bin)
endif()

# 创建输出目录
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin/output)

message(STATUS "VideoCreatorCpp独立构建配置完成")
message(STATUS "Qt路径: ${Qt6_DIR}")
message(STATUS "FFmpeg路径: ${FFMPEG_DIR}")

# 复制FFmpeg DLL文件到输出目录（Windows平台）
if(WIN32)
    # 检查FFmpeg bin目录是否存在
    if(EXISTS "${FFMPEG_DIR}/bin/")
        # 查找FFmpeg bin目录下的所有DLL文件
        file(GLOB FFMPEG_DLLS "${FFMPEG_DIR}/bin/*.dll")
        
        if(FFMPEG_DLLS)
            message(STATUS "找到FFmpeg DLL文件:")
            foreach(DLL_FILE ${FFMPEG_DLLS})
                message(STATUS "  - ${DLL_FILE}")
            endforeach()
            
            # 复制DLL文件到输出目录
            foreach(DLL_FILE ${FFMPEG_DLLS})
                file(COPY ${DLL_FILE} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
            endforeach()
            
            list(LENGTH FFMPEG_DLLS DLL_COUNT)
            message(STATUS "已复制 ${DLL_COUNT} 个FFmpeg DLL文件到输出目录: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        else()
            message(WARNING "FFmpeg bin目录存在，但未找到DLL文件: ${FFMPEG_DIR}/bin/")
            message(WARNING "当前FFmpeg可能只包含静态库，需要获取包含DLL的版本")
        endif()
    else()
        message(STATUS "FFmpeg bin目录不存在: ${FFMPEG_DIR}/bin/")
        message(STATUS "当前使用的是静态库版本，不需要复制DLL文件")
    endif()
endif()
