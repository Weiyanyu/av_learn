cmake_minimum_required(VERSION 3.22)
project(av-test)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall -DNDEBUG")

aux_source_directory(. DIR_SRCS)
add_subdirectory(core)
add_subdirectory(utils)

include_directories(${PROJECT_SOURCE_DIR}/core/include)
include_directories(${PROJECT_SOURCE_DIR}/utils/include)


add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} avdemocore)
target_link_libraries(${PROJECT_NAME} avdemoutils)



