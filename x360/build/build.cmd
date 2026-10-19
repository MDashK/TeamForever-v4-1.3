@echo off
rem Builds the Xbox 360 port (RXDK-360 clang toolset), see x360\README.md.
rem   build.cmd [Release|Debug] [project ...]
rem Without projects: every library, then both games (Sonic 1 Forever and Sonic 2 Absolute).
setlocal
rem (Before any shift: shift also moves %0)
set HERE=%~dp0
set MSBUILD=E:\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe
set CONFIG=Release
if /I "%~1"=="Release" (set CONFIG=Release& shift)
if /I "%~1"=="Debug" (set CONFIG=Debug& shift)
set PROJECTS=%1 %2 %3 %4 %5 %6
if "%~1"=="" set PROJECTS=x360runtime oggvorbis sdl2lite rsdkv4_s1f rsdkv4_s2a
cd /d "%HERE%"
for %%P in (%PROJECTS%) do (
	echo === %%P ^(%CONFIG%^) ===
	"%MSBUILD%" %%P.vcxproj -nologo -v:m -m /p:Configuration=%CONFIG% "/p:Platform=Xbox 360" || exit /b 1
)
echo === Build OK ===
