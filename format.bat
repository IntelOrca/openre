@echo off
setlocal
set PATH=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin;%PATH%
if "%~1"=="" (
  clang-format -i src\*.cpp src\*.hpp src\*.h
) else (
  clang-format -i %*
)
