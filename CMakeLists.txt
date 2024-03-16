cmake_minimum_required(VERSION 3.10)

project(openre)

set(CMAKE_CXX_STANDARD 17)

SET(COMPILER_PREFIX i686-w64-mingw32-)
SET(CMAKE_C_COMPILER ${COMPILER_PREFIX}gcc)
SET(CMAKE_CXX_COMPILER ${COMPILER_PREFIX}c++)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -m32 -static-libgcc -static-libstdc++ -fpermissive -w")

file(GLOB SOURCES "src/*.cpp")

add_library(openre MODULE ${SOURCES})

target_include_directories(openre PRIVATE "lib/include")
target_link_directories(openre PRIVATE "lib/lib")

target_link_libraries(openre PRIVATE lua54 winmm)

set_target_properties(openre PROPERTIES PREFIX "" SUFFIX ".dll")
