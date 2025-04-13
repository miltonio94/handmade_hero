@echo off

"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

mkdir ..\build
pushd ..\build
cl -FC -Zi ..\code\win32_handmade.cpp user32.lib gdi32.lib
popd
