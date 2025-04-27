@echo off

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

set CommonCompilerFlags=-MT -nologo -Gm- -GR -EHa -Od -Oi -WX -W4 -wd4201 -wd4100 -wd4189 -wd4456 -DHANDMADE_WIN32=1 -DHANDMADE_SLOW=1 -DHANDMADE_INTERNAL=1 -FC -Z7 -Fmwin32_handmade.map
set CommonLinkerFlags=-opt:ref user32.lib gdi32.lib winmm.lib

IF NOT EXIST ..\build mkdir ..\build
pushd ..\build
REM 32-bit build
REM cl %CommonCompilerFlags% ..\code\win32_handmade.cpp /link %CommonLinkerFlags% -subsystem:windows,5.1
Rem 64-bit build
cl %CommonCompilerFlags%  ..\code\win32_handmade.cpp /link %CommonLinkerFlags%
popd
