cmake_minimum_required(VERSION 3.19)
project(soft_ffmpeg_decode)

set(CMAKE_CXX_STANDARD 17)

set(FFMPEG_LIB_PATH D:/third_lib/ffmpeg/4.4/lib)
set(FFMPEG_INCLUDE_PATH D:/third_lib/ffmpeg/4.4/include)
set(FFMPEG_LIBS avcodec.lib avdevice.lib avfilter.lib
        avformat.lib avutil.lib postproc.lib swresample.lib swscale.lib)
include_directories(${FFMPEG_INCLUDE_PATH})

add_executable(soft_ffmpeg_decode main.cpp)

#连接 ffmpeg库
target_link_directories(soft_ffmpeg_decode PRIVATE ${FFMPEG_LIB_PATH})
target_link_libraries(soft_ffmpeg_decode ${FFMPEG_LIBS})