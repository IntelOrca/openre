@echo off
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "LIB=%ROOT%lib"
set "SDL_VERSION=3.4.12"
set "LUA_VERSION=5.4.2"
set "CACHE=%TEMP%\openre-deps"

rem ---------------------------------------------------------------------------
rem Download and extract build dependencies into the lib folder.
rem ---------------------------------------------------------------------------

where curl >nul 2>&1
if errorlevel 1 (
    echo error: curl not found. Install curl or add it to PATH.
    exit /b 1
)

mkdir "%LIB%\include\SDL3" 2>nul
mkdir "%LIB%\bin\x86" 2>nul
mkdir "%LIB%\lib\x86" 2>nul
mkdir "%CACHE%" 2>nul

rem ---------------------------------------------------------------------------
rem SDL3
rem ---------------------------------------------------------------------------
if exist "%LIB%\include\SDL3\SDL.h" if exist "%LIB%\lib\x86\SDL3.lib" if exist "%LIB%\bin\x86\SDL3.dll" goto sdl_done

set "SDL_ZIP=%CACHE%\SDL3-devel-%SDL_VERSION%-VC.zip"
if not exist "%SDL_ZIP%" (
    echo Downloading SDL3 %SDL_VERSION%...
    curl -fL -o "%SDL_ZIP%" "https://github.com/libsdl-org/SDL/releases/download/release-%SDL_VERSION%/SDL3-devel-%SDL_VERSION%-VC.zip"
    if errorlevel 1 (
        echo error: failed to download SDL3
        exit /b 1
    )
)

echo Extracting SDL3 %SDL_VERSION%...
tar -xf "%SDL_ZIP%" -C "%CACHE%" 2>nul
if errorlevel 1 (
    echo error: failed to extract %SDL_ZIP%
    exit /b 1
)

copy /y "%CACHE%\SDL3-%SDL_VERSION%\include\SDL3\*.h" "%LIB%\include\SDL3\" >nul
copy /y "%CACHE%\SDL3-%SDL_VERSION%\lib\x86\SDL3.lib" "%LIB%\lib\x86\" >nul
copy /y "%CACHE%\SDL3-%SDL_VERSION%\lib\x86\SDL3.dll" "%LIB%\bin\x86\" >nul
echo SDL3 %SDL_VERSION% installed.

:sdl_done

rem ---------------------------------------------------------------------------
rem Lua
rem ---------------------------------------------------------------------------
if exist "%LIB%\include\lua.h" if exist "%LIB%\lib\lua54.lib" if exist "%LIB%\bin\lua54.dll" goto lua_done

set "LUA_ZIP=%CACHE%\lua-%LUA_VERSION%_Win32_dll17_lib.zip"
if not exist "%LUA_ZIP%" (
    echo Downloading Lua %LUA_VERSION%...
    curl -fL -o "%LUA_ZIP%" "https://sourceforge.net/projects/luabinaries/files/%LUA_VERSION%/Windows%%20Libraries/Dynamic/lua-%LUA_VERSION%_Win32_dll17_lib.zip/download"
    if errorlevel 1 (
        echo error: failed to download Lua
        exit /b 1
    )
)

echo Extracting Lua %LUA_VERSION%...
tar -xf "%LUA_ZIP%" -C "%CACHE%" 2>nul
if errorlevel 1 (
    echo error: failed to extract %LUA_ZIP%
    exit /b 1
)

copy /y "%CACHE%\include\*.h" "%LIB%\include\" >nul
copy /y "%CACHE%\include\lua.hpp" "%LIB%\include\" >nul
copy /y "%CACHE%\lua54.lib" "%LIB%\lib\" >nul
copy /y "%CACHE%\lua54.dll" "%LIB%\bin\" >nul
echo Lua %LUA_VERSION% installed.

:lua_done

echo.
echo Dependencies installed in %LIB%.
echo You can now build with: msbuild openre.sln
endlocal
