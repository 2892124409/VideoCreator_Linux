# VideoCreatorCpp standalone build config

cmake_minimum_required(VERSION 3.16)
project(VideoCreatorCpp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/3rdparty/ffmpeg")
set(VIDEOCREATOR_FFMPEG_LINK_AVAILABLE ON)

find_package(Threads REQUIRED)

if(WIN32)
    if(NOT EXISTS "${FFMPEG_DIR}/include")
        message(FATAL_ERROR "FFmpeg include directory not found: ${FFMPEG_DIR}/include")
    endif()
    include_directories(${FFMPEG_DIR}/include)

    if(MSVC)
        set(FFMPEG_LIBS
            ${FFMPEG_DIR}/lib/avcodec.lib
            ${FFMPEG_DIR}/lib/avformat.lib
            ${FFMPEG_DIR}/lib/avutil.lib
            ${FFMPEG_DIR}/lib/swscale.lib
            ${FFMPEG_DIR}/lib/swresample.lib
            ${FFMPEG_DIR}/lib/avfilter.lib
        )
    else()
        set(FFMPEG_LIBS
            ${FFMPEG_DIR}/lib/libavcodec.dll.a
            ${FFMPEG_DIR}/lib/libavformat.dll.a
            ${FFMPEG_DIR}/lib/libavutil.dll.a
            ${FFMPEG_DIR}/lib/libswscale.dll.a
            ${FFMPEG_DIR}/lib/libswresample.dll.a
            ${FFMPEG_DIR}/lib/libavfilter.dll.a
        )
    endif()
else()
    set(FFMPEG_LINUX_SYSROOT "${CMAKE_SOURCE_DIR}/3rdparty/ffmpeg-linux/sysroot")
    set(FFMPEG_LINUX_LIBDIR "${FFMPEG_LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu")
    set(FFMPEG_LINUX_INCDIR "${FFMPEG_LINUX_SYSROOT}/usr/include/x86_64-linux-gnu")

    if(EXISTS "${FFMPEG_LINUX_LIBDIR}/libavcodec.so" AND EXISTS "${FFMPEG_LINUX_INCDIR}/libavcodec/avcodec.h")
        set(FFMPEG_INCLUDE_DIR "${FFMPEG_LINUX_INCDIR}")
        set(FFMPEG_LIBRARY_DIR "${FFMPEG_LINUX_LIBDIR}")
        set(FFMPEG_RUNTIME_DIRS
            ${FFMPEG_LIBRARY_DIR}
            ${FFMPEG_LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/blas
            ${FFMPEG_LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/lapack
        )
        include_directories(${FFMPEG_INCLUDE_DIR})
        set(FFMPEG_LIBS
            ${FFMPEG_LIBRARY_DIR}/libavcodec.so
            ${FFMPEG_LIBRARY_DIR}/libavformat.so
            ${FFMPEG_LIBRARY_DIR}/libavutil.so
            ${FFMPEG_LIBRARY_DIR}/libswscale.so
            ${FFMPEG_LIBRARY_DIR}/libswresample.so
            ${FFMPEG_LIBRARY_DIR}/libavfilter.so
            ${FFMPEG_LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/blas/libblas.so.3
            ${FFMPEG_LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/lapack/liblapack.so.3
        )
        message(STATUS "Using vendored Linux FFmpeg sysroot: ${FFMPEG_LINUX_SYSROOT}")
    else()
        find_path(FFMPEG_INCLUDE_DIR
            NAMES libavcodec/avcodec.h
            PATH_SUFFIXES x86_64-linux-gnu
            PATHS
                /usr/include
                /usr/local/include
        )
        if(NOT FFMPEG_INCLUDE_DIR)
            message(FATAL_ERROR "FFmpeg headers not found. Install FFmpeg development headers.")
        endif()
        include_directories(${FFMPEG_INCLUDE_DIR})

        find_library(FFMPEG_AVCODEC_LIBRARY NAMES avcodec)
        find_library(FFMPEG_AVFORMAT_LIBRARY NAMES avformat)
        find_library(FFMPEG_AVUTIL_LIBRARY NAMES avutil)
        find_library(FFMPEG_SWSCALE_LIBRARY NAMES swscale)
        find_library(FFMPEG_SWRESAMPLE_LIBRARY NAMES swresample)
        find_library(FFMPEG_AVFILTER_LIBRARY NAMES avfilter)

        if(NOT FFMPEG_AVCODEC_LIBRARY OR
           NOT FFMPEG_AVFORMAT_LIBRARY OR
           NOT FFMPEG_AVUTIL_LIBRARY OR
           NOT FFMPEG_SWSCALE_LIBRARY OR
           NOT FFMPEG_SWRESAMPLE_LIBRARY OR
           NOT FFMPEG_AVFILTER_LIBRARY)
            set(VIDEOCREATOR_FFMPEG_LINK_AVAILABLE OFF)
            message(WARNING "FFmpeg libraries not found. Building compile-only core library without final FFmpeg link step.")
        else()
            set(FFMPEG_LIBS
                ${FFMPEG_AVCODEC_LIBRARY}
                ${FFMPEG_AVFORMAT_LIBRARY}
                ${FFMPEG_AVUTIL_LIBRARY}
                ${FFMPEG_SWSCALE_LIBRARY}
                ${FFMPEG_SWRESAMPLE_LIBRARY}
                ${FFMPEG_AVFILTER_LIBRARY}
            )
        endif()
    endif()
endif()

set(VIDEOCREATOR_CORE_SOURCES
    src/model/ConfigLoader.cpp
    src/model/ConfigLoader.h
    src/model/ProjectConfig.h
    src/engine/RenderEngine.cpp
    src/engine/RenderEngine.h
    src/decoder/ImageDecoder.cpp
    src/decoder/ImageDecoder.h
    src/decoder/AudioDecoder.cpp
    src/decoder/AudioDecoder.h
    src/decoder/VideoDecoder.cpp
    src/decoder/VideoDecoder.h
    src/decoder/FrameScaler.h
    src/decoder/VideoLikeDecoderCore.h
    src/filter/EffectProcessor.cpp
    src/filter/EffectProcessor.h
    src/ffmpeg_utils/AvFrameWrapper.h
    src/ffmpeg_utils/AvPacketWrapper.h
    src/ffmpeg_utils/FFmpegHeaders.h
    src/VideoCreatorAPI.cpp
    src/VideoCreatorAPI.h
)

add_library(VideoCreatorCore STATIC ${VIDEOCREATOR_CORE_SOURCES})

if(VIDEOCREATOR_FFMPEG_LINK_AVAILABLE)
    target_link_libraries(VideoCreatorCore PUBLIC
        ${FFMPEG_LIBS}
        Threads::Threads
    )
else()
    target_link_libraries(VideoCreatorCore PUBLIC
        Threads::Threads
    )
endif()

if(NOT WIN32 AND VIDEOCREATOR_FFMPEG_LINK_AVAILABLE)
    if(DEFINED FFMPEG_RUNTIME_DIRS)
        add_link_options("-Wl,--disable-new-dtags")
        set(CMAKE_BUILD_RPATH "${FFMPEG_RUNTIME_DIRS}")
        set(CMAKE_INSTALL_RPATH "${FFMPEG_RUNTIME_DIRS}")
    elseif(DEFINED FFMPEG_LIBRARY_DIR)
        set(CMAKE_BUILD_RPATH "${FFMPEG_LIBRARY_DIR}")
        set(CMAKE_INSTALL_RPATH "${FFMPEG_LIBRARY_DIR}")
    endif()
endif()

target_include_directories(VideoCreatorCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg_utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder
    ${CMAKE_CURRENT_SOURCE_DIR}/src/filter
)

if(VIDEOCREATOR_FFMPEG_LINK_AVAILABLE)
    add_executable(VideoCreatorCpp src/main.cpp)
    target_link_libraries(VideoCreatorCpp PRIVATE VideoCreatorCore)
    target_include_directories(VideoCreatorCpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" PROJECT_SOURCE_DIR_CMAKE)
    target_compile_definitions(VideoCreatorCpp PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR_CMAKE}")
else()
    message(STATUS "Skipping VideoCreatorCpp executable target because FFmpeg link libraries are unavailable.")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR}/bin)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_config.json")
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test_config.json DESTINATION ${CMAKE_BINARY_DIR}/bin)
endif()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin/output)

message(STATUS "VideoCreatorCpp build config ready")
message(STATUS "FFmpeg directory: ${FFMPEG_DIR}")

if(WIN32)
    if(EXISTS "${FFMPEG_DIR}/bin/")
        file(GLOB FFMPEG_DLLS "${FFMPEG_DIR}/bin/*.dll")
        foreach(DLL_FILE ${FFMPEG_DLLS})
            file(COPY ${DLL_FILE} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
        endforeach()
    endif()
endif()
