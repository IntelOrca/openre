#!/usr/bin/env bash
# Download and extract build dependencies into the lib folder.
# Linux equivalent of getdeps.bat (used by the GitHub CI workflow).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB="$ROOT/lib"
SDL_VERSION="3.4.12"
LUA_VERSION="5.4.2"
CACHE="${TMPDIR:-/tmp}/openre-deps"

mkdir -p "$LIB/include/SDL3" "$LIB/bin/x86" "$LIB/lib/x86" "$CACHE"

# ---------------------------------------------------------------------------
# SDL3
# ---------------------------------------------------------------------------
if [[ -f "$LIB/include/SDL3/SDL.h" && -f "$LIB/lib/x86/SDL3.lib" && -f "$LIB/bin/x86/SDL3.dll" ]]; then
    echo "SDL3 $SDL_VERSION already installed."
else
    SDL_ZIP="$CACHE/SDL3-devel-$SDL_VERSION-VC.zip"
    if [[ ! -f "$SDL_ZIP" ]]; then
        echo "Downloading SDL3 $SDL_VERSION..."
        curl -fL -o "$SDL_ZIP" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-devel-$SDL_VERSION-VC.zip"
    fi
    echo "Extracting SDL3 $SDL_VERSION..."
    python3 -c "import zipfile, sys; zipfile.ZipFile(sys.argv[1]).extractall('$CACHE')" "$SDL_ZIP"
    cp "$CACHE/SDL3-$SDL_VERSION/include/SDL3/"*.h "$LIB/include/SDL3/"
    cp "$CACHE/SDL3-$SDL_VERSION/lib/x86/SDL3.lib" "$LIB/lib/x86/"
    cp "$CACHE/SDL3-$SDL_VERSION/lib/x86/SDL3.dll" "$LIB/bin/x86/"
    echo "SDL3 $SDL_VERSION installed."
fi

# ---------------------------------------------------------------------------
# Lua
# ---------------------------------------------------------------------------
if [[ -f "$LIB/include/lua.h" && -f "$LIB/lib/lua54.lib" && -f "$LIB/bin/lua54.dll" ]]; then
    echo "Lua $LUA_VERSION already installed."
else
    LUA_ZIP="$CACHE/lua-${LUA_VERSION}_Win32_dll17_lib.zip"
    if [[ ! -f "$LUA_ZIP" ]]; then
        echo "Downloading Lua $LUA_VERSION..."
        curl -fL -o "$LUA_ZIP" "https://sourceforge.net/projects/luabinaries/files/$LUA_VERSION/Windows%20Libraries/Dynamic/lua-${LUA_VERSION}_Win32_dll17_lib.zip/download?use_mirror=autoselect"
    fi
    echo "Extracting Lua $LUA_VERSION..."
    python3 -c "import zipfile, sys; zipfile.ZipFile(sys.argv[1]).extractall('$CACHE')" "$LUA_ZIP"
    cp "$CACHE/include/"*.h "$LIB/include/"
    cp "$CACHE/include/lua.hpp" "$LIB/include/"
    cp "$CACHE/lua54.lib" "$LIB/lib/"
    cp "$CACHE/lua54.dll" "$LIB/bin/"
    echo "Lua $LUA_VERSION installed."
fi

echo "Dependencies installed in $LIB."
echo "You can now build with: cmake -B out && cmake --build out"
