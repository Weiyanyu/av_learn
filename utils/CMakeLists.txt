cmake_minimum_required(VERSION 3.22)
project(libavdemoutuls)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall -DNDEBUG")

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../lib/utils)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavdevice
    libavfilter
    libavformat
    libavcodec
    libswresample
    libswscale
    libavutil
)

add_compile_options(-fPIC)
aux_source_directory(src DIR_LIB_SRCS)
include_directories(${PROJECT_SOURCE_DIR}/include/)
add_library (avdemoutils SHARED  ${DIR_LIB_SRCS})
target_link_libraries(avdemoutils)
