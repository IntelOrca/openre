cmake_minimum_required(VERSION 3.10)

# ---------------------------------------------------------------------------
# Build target selection
#
# The default target cross-compiles the Windows DLL (openre.dll) with the
# MinGW i686 toolchain, exactly as GitHub CI does (plain `cmake -B out &&
# cmake --build out` on ubuntu-latest with g++-mingw-w64 installed).
#
# A native Linux target is opt-in:
#     cmake -B build-linux -DOPENRE_NATIVE_LINUX=ON
#
# Host-platform auto-detection is intentionally NOT used: CI runs on a Linux
# host but must produce the MinGW DLL, so the MinGW build stays the default
# regardless of host OS.
# ---------------------------------------------------------------------------
option(OPENRE_NATIVE_LINUX "Build a native Linux target instead of the MinGW Windows DLL" OFF)
# Native Linux only: build a loadable module (openre.so) instead of an
# executable. A shared module tolerates unresolved symbols at link time
# (e.g. while the Lua library is not available on Linux yet), which makes it
# a useful intermediate form until the executable fully links.
option(OPENRE_LINUX_MODULE "Native Linux: build openre.so (loadable module) instead of an executable" OFF)

# The compiler must be selected BEFORE project(): modern CMake (>= 3.25-ish)
# silently ignores CMAKE_C_COMPILER / CMAKE_CXX_COMPILER set after project()
# has already run its compiler detection. For a native build we deliberately
# leave the compilers unset so project() picks the host gcc/g++. Note: use a
# fresh build directory when switching between the two target types, or pass
# the compiler explicitly (e.g. -DCMAKE_CXX_COMPILER=g++).
if(NOT OPENRE_NATIVE_LINUX)
    SET(COMPILER_PREFIX i686-w64-mingw32-)
    SET(CMAKE_C_COMPILER ${COMPILER_PREFIX}gcc)
    SET(CMAKE_CXX_COMPILER ${COMPILER_PREFIX}c++)
endif()

project(openre)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The bundled headers cover both SDL3 and Lua and are cross-platform.
include_directories(${CMAKE_SOURCE_DIR}/lib/include)

file(GLOB SOURCES "src/*.cpp")

if(OPENRE_NATIVE_LINUX)
    # ------------------------------------------------------------------
    # Native Linux target
    # ------------------------------------------------------------------
    # marni_ddraw.cpp is entirely #if defined(_WIN32) && !defined(OPENRE_NO_D3D)
    # gated and would be an empty translation unit on Linux; exclude it cleanly.
    # marni_draw.cpp IS portable and stays in the build.
    list(REMOVE_ITEM SOURCES "${CMAKE_SOURCE_DIR}/src/marni_ddraw.cpp")

    if(OPENRE_LINUX_MODULE)
        add_library(openre MODULE ${SOURCES})
        set_target_properties(openre PROPERTIES PREFIX "" SUFFIX ".so")
    else()
        # openre.cpp provides a portable main() under #ifndef _WIN32.
        add_executable(openre ${SOURCES})
    endif()

    # Host compiler, no MinGW prefix. The re2.h memory model mirrors the 32-bit
    # RE2 binary, so the native target must compile with -m32 (its structs use
    # 4-byte pointers; a 64-bit build trips ~150 re2.h static_asserts). -fpermissive
    # for MSVC-isms and -w to keep the scaffold build quiet, mirroring the MinGW
    # build. On 64-bit hosts -m32 requires g++-multilib.
    target_compile_options(openre PRIVATE -m32 -fpermissive -w)
    target_link_options(openre PRIVATE -m32)

    # OPENRE_NO_D3D is REQUIRED for the native target, verified by compiling:
    #  * openre.cpp fails without it: `#ifndef OPENRE_NO_D3D` -> #include <ddraw.h>
    #    which does not exist on Linux (d3d_shim.h is included by the marni/
    #    system_gpu_backend headers, not by openre.cpp's raw <ddraw.h> include).
    #  * marni.cpp without it: 111 errors (SetRect/LPUNKNOWN/DDBD_*/incomplete
    #    IDirectDraw* COM types the shim deliberately leaves opaque).
    #
    # Native compile status: the whole tree compiles with -m32 (the re2.h
    # static_asserts pass because a 32-bit target matches the RE2 binary's
    # memory model). The native main() in openre.cpp is scaffolding only.
    target_compile_definitions(openre PRIVATE OPENRE_NO_D3D)

    # SDL3: prefer pkg-config, then find_package(SDL3) (works e.g. with
    # -DCMAKE_PREFIX_PATH=/path/to/sdl3-build), then an explicit library path.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL3 QUIET sdl3)
    endif()
    if(SDL3_FOUND)
        target_link_libraries(openre PRIVATE ${SDL3_LIBRARIES})
        target_link_directories(openre PRIVATE ${SDL3_LIBRARY_DIRS})
    else()
        find_package(SDL3 QUIET)
        if(SDL3_FOUND)
            target_link_libraries(openre PRIVATE SDL3::SDL3)
        else()
            set(OPENRE_SDL3_LIBRARY "" CACHE FILEPATH "Path to a Linux libSDL3.so (used when pkg-config/find_package find none)")
            if(OPENRE_SDL3_LIBRARY)
                target_link_libraries(openre PRIVATE "${OPENRE_SDL3_LIBRARY}")
            else()
                find_library(SDL3_LIBRARY NAMES SDL3 libSDL3 PATHS /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib)
                if(SDL3_LIBRARY)
                    target_link_libraries(openre PRIVATE ${SDL3_LIBRARY})
                else()
                    message(WARNING "SDL3 library not found. Install SDL3, or pass -DOPENRE_SDL3_LIBRARY=/path/to/libSDL3.so (or -DCMAKE_PREFIX_PATH=/path/to/sdl3-build).")
                endif()
            endif()
        endif()
    endif()

    # Lua: lua54 on Windows; try the usual Linux names. Headers come from the
    # bundled lib/include. The library itself is Windows-only in lib/, so on a
    # stock Ubuntu without liblua5.4-dev the executable link will fail on
    # undefined lua_* symbols; use -DOPENRE_LINUX_MODULE=ON for an openre.so
    # that tolerates that until a Linux Lua is available.
    if(PkgConfig_FOUND)
        pkg_check_modules(LUA QUIET lua5.4)
        if(NOT LUA_FOUND)
            pkg_check_modules(LUA QUIET lua)
        endif()
    endif()
    if(LUA_FOUND)
        target_link_libraries(openre PRIVATE ${LUA_LIBRARIES})
    else()
        find_library(LUA_LIBRARY NAMES lua5.4 lua54 lua-5.4 lua PATHS /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib)
        if(LUA_LIBRARY)
            target_link_libraries(openre PRIVATE ${LUA_LIBRARY})
        else()
            message(WARNING "Lua library not found. The executable link will fail on lua_* symbols unless you install Lua (or use -DOPENRE_LINUX_MODULE=ON).")
        endif()
    endif()

else()
    # ------------------------------------------------------------------
    # MinGW cross-compile to Windows DLL (default, used by CI)
    # ------------------------------------------------------------------
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -m32 -static-libgcc -static-libstdc++ -fpermissive -w -DOPENRE_NO_D3D")

    add_library(openre MODULE ${SOURCES})

    target_link_directories(openre PRIVATE "${CMAKE_SOURCE_DIR}/lib/lib")

    # Note: no ddraw/dxguid/d3d11/dxgi - the OPENRE_NO_D3D prototype removes all
    # D3D/DDRAW usage from the linker. strmiids/uuid provide the DirectShow
    # MEDIASUBTYPE_* and IID_*/CLSID_* GUIDs referenced by marni_movie.cpp.
    target_link_libraries(openre PRIVATE lua54 ws2_32 gdi32 ole32 oleaut32 winmm imm32 mfplat mfuuid strmiids uuid ${CMAKE_SOURCE_DIR}/lib/lib/x86/SDL3.lib)

    set_target_properties(openre PROPERTIES PREFIX "" SUFFIX ".dll")

    add_custom_command(TARGET openre POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/lib/bin/x86/SDL3.dll" $<TARGET_FILE_DIR:openre>)
    add_custom_command(TARGET openre POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/lib/bin/lua54.dll" $<TARGET_FILE_DIR:openre>)
endif()
