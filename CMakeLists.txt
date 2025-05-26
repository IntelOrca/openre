cmake_minimum_required(VERSION 3.10)

option(ENABLE_MOVIE "Enable movie support" OFF)
if(ENABLE_MOVIE)
  list(APPEND VCPKG_MANIFEST_FEATURES "movie")
endif()

project(openre)

set(CMAKE_CXX_STANDARD 17)

SET(COMPILER_PREFIX i686-w64-mingw32-)
SET(CMAKE_C_COMPILER ${COMPILER_PREFIX}gcc)
SET(CMAKE_CXX_COMPILER ${COMPILER_PREFIX}c++)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -m32 -static-libgcc -static-libstdc++ -fpermissive -w")

file(GLOB SOURCES "src/*.cpp")

add_library(openre MODULE ${SOURCES})

find_package(Lua REQUIRED)
target_include_directories(openre PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(openre PRIVATE ${LUA_LIBRARIES})

# For some reason this doesn't work for vcpkg
# find_package(WebP CONFIG REQUIRED)
# target_link_libraries(openre PRIVATE WebP::webp WebP::webpdecoder)
target_link_libraries(openre PRIVATE
    "${VCPKG_INSTALLED_DIR}/x86-mingw-static/lib/libwebp.a"
    "${VCPKG_INSTALLED_DIR}/x86-mingw-static/lib/libwebpdecoder.a")

# For some reason this doesn't work for vcpkg
# find_package(SDL3 CONFIG REQUIRED)
# target_link_libraries(openre PRIVATE SDL3::SDL3)
target_link_libraries(openre PRIVATE "${VCPKG_INSTALLED_DIR}/x86-mingw-static/lib/libSDL3.a")

# Other Windows related libraries to dynamically link (some are transitive from SDL3, FFMPEG)
target_link_libraries(openre PRIVATE "ddraw" "dxguid" "opengl32" "imm32" "winmm" "gdi32" "ole32" "oleaut32" "setupapi" "version" "uuid")

if(ENABLE_MOVIE)
    target_compile_definitions(ENABLE_MOVIE_FFMPEG PRIVATE ENABLE_MOVIE)

    find_package(FFMPEG REQUIRED)
    target_include_directories(openre PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(openre PRIVATE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(openre PRIVATE ${FFMPEG_LIBRARIES})
endif()

set_target_properties(openre PROPERTIES PREFIX "" SUFFIX ".dll")
