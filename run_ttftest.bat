@echo off
set OPENRE_LOG_VERBOSITY=info
set OPENRE_SAVE_PATH=M:\git\openre\bin\Debug\savedata\
set OPENRE_SCRIPTS=%~dp0\scripts
set OPENRE_WEB_PORT=8080
set OPENRE_GAME=ttftest
set "OPENRE_WINDOW_TITLE=OpenRE - TTF font test"

call bin\Debug\openre.exe %*