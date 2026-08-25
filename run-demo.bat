@echo off
rem Launches a Lua-ECS demo game module from games\<name>.
rem Usage: run-demo.bat [game] (default: config-demo).
rem The demo fonts embed common/data/font0.tim as their atlas, so the
rem original RE2 data dir is still required (same as run.bat).
set OPENRE_LOG_VERBOSITY=info
set OPENRE_RE2_DATA=F:\games\openre\data
set OPENRE_SCRIPTS=%~dp0\scripts
set OPENRE_GAME=config-demo
if not "%~1"=="" set OPENRE_GAME=%~1

for /f "delims=" %%i in ('git -C "%~dp0." branch --show-current 2^>nul') do set "OPENRE_GIT_BRANCH=%%i"
if defined OPENRE_GIT_BRANCH goto set_branch

for /f "delims=" %%i in ('git -C "%~dp0." rev-parse --short HEAD 2^>nul') do set "OPENRE_WINDOW_TITLE=OpenRE - %%i"
goto run

:set_branch
set "OPENRE_WINDOW_TITLE=OpenRE - %OPENRE_GIT_BRANCH%"

:run
bin\Debug\openre.exe