cmake_minimum_required(VERSION 2.6)
project(eva)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(CXX_FLAGS
        -fno-omit-frame-pointer # linux perf
        -Wall
        -Wextra
        -Werror
        -Wconversion
        -Wno-unused-parameter
        -Wold-style-cast
        -Woverloaded-virtual
        -Wpointer-arith
        -Wshadow
        -Wwrite-strings
        -std=c++11
        -march=native
        -rdynamic)
string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -finline-limit=1000 -DNDEBUG")
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

add_subdirectory(third_party/muduo)

include_directories(
        third_party/muduo
        ${PROJECT_SOURCE_DIR})

add_subdirectory(eva)

add_executable(main main.cc)
target_link_libraries(main eva pcap)

add_executable(run run.cc)
target_link_libraries(run eva pcap)

add_executable(flow_generator flowGenerator.cc)
target_link_libraries(flow_generator eva)

add_executable(client client.cc)
target_link_libraries(client eva)