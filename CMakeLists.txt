cmake_minimum_required(VERSION 3.10)

project(openre)

set(CMAKE_CXX_STANDARD 17)

SET(COMPILER_PREFIX i686-w64-mingw32-)
SET(CMAKE_C_COMPILER ${COMPILER_PREFIX}gcc)
SET(CMAKE_CXX_COMPILER ${COMPILER_PREFIX}c++)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -m32 -static-libgcc -static-libstdc++ -fpermissive -w -DOPENRE_NO_D3D")

include_directories(${CMAKE_SOURCE_DIR}/lib/include)

file(GLOB SOURCES "src/*.cpp")

add_library(openre MODULE ${SOURCES})

target_link_directories(openre PRIVATE "${CMAKE_SOURCE_DIR}/lib/lib")

# Note: no ddraw/dxguid/d3d11/dxgi - the OPENRE_NO_D3D prototype removes all
# D3D/DDRAW usage from the linker.
target_link_libraries(openre PRIVATE lua54 ws2_32 gdi32 ole32 winmm imm32 mfplat mfuuid ${CMAKE_SOURCE_DIR}/lib/lib/x86/SDL3.lib)

set_target_properties(openre PROPERTIES PREFIX "" SUFFIX ".dll")

add_custom_command(TARGET openre POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/lib/bin/x86/SDL3.dll" $<TARGET_FILE_DIR:openre>)
add_custom_command(TARGET openre POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/lib/bin/lua54.dll" $<TARGET_FILE_DIR:openre>)
