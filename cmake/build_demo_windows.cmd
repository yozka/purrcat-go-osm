@echo off
setlocal EnableExtensions

REM Build and run OsmDemo on Windows.
REM Double-click or: cmake\build_demo_windows.cmd
REM
REM Env (optional):
REM   QT_WINDOWS=C:\Qt\6.11.1\msvc2022_64
REM   CMAKE_GENERATOR=Visual Studio 17 2022
REM   CMAKE_ARCH=x64

for %%I in ("%~dp0..") do set "PROJECT=%%~fI"
set "ROOT=%PROJECT%\demo"
set "BUILD_DIR=%PROJECT%\build\demo_windows"

if not defined QT_WINDOWS set "QT_WINDOWS=%USERPROFILE%\Qt\6.11.1\msvc2022_64"
set "QT_CMAKE=%QT_WINDOWS%\bin\qt-cmake.bat"

if not exist "%QT_CMAKE%" (
    echo qt-cmake not found: %QT_CMAKE%
    echo Set QT_WINDOWS to your Qt MSVC kit, e.g.:
    echo   set QT_WINDOWS=C:\Qt\6.11.1\msvc2022_64
    exit /b 1
)

if not defined CMAKE_GENERATOR set "CMAKE_GENERATOR=Visual Studio 17 2022"
if not defined CMAKE_ARCH set "CMAKE_ARCH=x64"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%"

echo ==^> Configuring OsmDemo
echo     ROOT=%ROOT%
echo     QT=%QT_WINDOWS%
echo     BUILD=%BUILD_DIR%
echo     GENERATOR=%CMAKE_GENERATOR%
echo     ARCH=%CMAKE_ARCH%

"%QT_CMAKE%" "%ROOT%" ^
  -G "%CMAKE_GENERATOR%" ^
  -A %CMAKE_ARCH%

if errorlevel 1 (
    popd
    exit /b 1
)

echo.
echo ==^> Building Debug
cmake --build . --config Debug --parallel

if errorlevel 1 (
    popd
    exit /b 1
)

set "EXE=%BUILD_DIR%\Debug\OsmDemo.exe"
if not exist "%EXE%" set "EXE=%BUILD_DIR%\OsmDemo.exe"
if not exist "%EXE%" (
    echo Executable not found under %BUILD_DIR%
    popd
    exit /b 1
)

REM Qt DLLs / plugins for runtime
set "PATH=%QT_WINDOWS%\bin;%PATH%"

echo.
echo ==^> Running %EXE%
start "" "%EXE%"

popd
endlocal
exit /b 0
