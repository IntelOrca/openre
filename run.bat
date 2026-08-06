@echo off
set OPENRE_LOG_VERBOSITY=debug
set OPENRE_RE2_DATA=F:\games\openre\data
set OPENRE_GFX_BACKEND=1
set OPENRE_GFX_MODE=gpu

for /f "delims=" %%i in ('git -C "%~dp0." branch --show-current 2^>nul') do set "OPENRE_GIT_BRANCH=%%i"
if defined OPENRE_GIT_BRANCH goto set_branch

for /f "delims=" %%i in ('git -C "%~dp0." rev-parse --short HEAD 2^>nul') do set "OPENRE_WINDOW_TITLE=OpenRE - %%i"
goto run

:set_branch
set "OPENRE_WINDOW_TITLE=OpenRE - %OPENRE_GIT_BRANCH%"

:run
call bin\Debug\openre.exe %*
