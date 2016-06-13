@echo off
setlocal

:: Remove old build files
echo Cleaning build area ...
rmdir /s /q build 2> nul
md build\v120 build\v140 2> nul

echo Starting build ...
call:buildx64

echo Build finished.
goto:eof

:buildx64
echo Building targets for x64 ...

setlocal
call "%ProgramFiles(x86)%\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64

(
cd build
(
  cd v120
  "%ProgramFiles%\CMake\bin\cmake.exe" -G "Visual Studio 14 2015 Win64" ..\.. -T "v120" > build.log
  msbuild /nologo /property:Configuration=Release ALL_BUILD.vcxproj >> build.log
  for /D %%f in (lib\*) do dir %%f\*.lib | findstr "\/"
  cd ..
)
(
  cd v140
  "%ProgramFiles%\CMake\bin\cmake.exe" -G "Visual Studio 14 2015 Win64" ..\.. -T "v140" -DCPACK_GENERATOR=WIX > build.log
  msbuild /nologo /property:Configuration=Release ALL_BUILD.vcxproj >> build.log
  msbuild /nologo /property:Configuration=Release PACKAGE.vcxproj >> build.log
  for /D %%f in (lib\*) do dir %%f\*.lib | findstr "\/"
  cd ..
)
cd ..
)

endlocal
goto:eof
