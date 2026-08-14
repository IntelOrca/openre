#!/usr/bin/env bash
# Build 32-bit SDL3 and Lua from source for the native Linux target.
# Used by the GitHub CI native job (needs g++-multilib) and locally under
# WSL/docker. Outputs land in $CACHE and are reported via the paths below.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CACHE="${TMPDIR:-/tmp}/openre-native-deps"
SDL_VERSION="3.4.12"
LUA_VERSION="5.4.2"

mkdir -p "$CACHE"

echo "Building SDL3 $SDL_VERSION (32-bit)..."
if [[ ! -d "$CACHE/SDL3-src" ]]; then
    curl -fL -o "$CACHE/sdl.tar.gz" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz"
    tar xzf "$CACHE/sdl.tar.gz" -C "$CACHE"
    mv "$CACHE/SDL3-$SDL_VERSION" "$CACHE/SDL3-src"
fi
if [[ ! -f "$CACHE/sdl3-build/libSDL3.so" ]]; then
    cmake -B "$CACHE/sdl3-build" -S "$CACHE/SDL3-src" \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
        -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_VIDEO_DRIVER_OFFSCREEN=ON \
        -DSDL_X11=OFF -DSDL_WAYLAND=OFF \
        -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32
    cmake --build "$CACHE/sdl3-build" -j"$(nproc)"
fi

echo "Building Lua $LUA_VERSION (32-bit)..."
if [[ ! -d "$CACHE/lua-$LUA_VERSION" ]]; then
    curl -fL -o "$CACHE/lua.tar.gz" "https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz"
    tar xzf "$CACHE/lua.tar.gz" -C "$CACHE"
fi
if [[ ! -f "$CACHE/lua-$LUA_VERSION/src/liblua.a" ]]; then
    (cd "$CACHE/lua-$LUA_VERSION" && make clean >/dev/null 2>&1 || true
     make linux MYCFLAGS="-m32" MYLDFLAGS="-m32")
fi

echo "SDL3:  $CACHE/sdl3-build/libSDL3.so"
echo "SDL3 include: $CACHE/SDL3-src/include"
echo "Lua:   $CACHE/lua-$LUA_VERSION/src/liblua.a"
echo "Native dependencies ready."
