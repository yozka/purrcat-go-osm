@echo off
setlocal EnableExtensions

REM Build and run PurrCat OSM tile server on Windows.
REM Double-click or: cmake\build_server_windows.cmd

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "BUILD_DIR=%ROOT%\build\server_windows"
set "DATA_DIR=%BUILD_DIR%\data"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%DATA_DIR%\tiles" mkdir "%DATA_DIR%\tiles"
if not exist "%DATA_DIR%\geometry" mkdir "%DATA_DIR%\geometry"

pushd "%BUILD_DIR%"

echo ==^> Configuring purrcat-osm-tiles
cmake "%ROOT%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo Fallback: trying default generator
    cmake "%ROOT%"
    if errorlevel 1 (
        popd
        exit /b 1
    )
)

echo.
echo ==^> Building Release
cmake --build . --config Release --parallel
if errorlevel 1 (
    popd
    exit /b 1
)

set "BIN=%BUILD_DIR%\Release\purrcat-osm-tiles.exe"
if not exist "%BIN%" set "BIN=%BUILD_DIR%\purrcat-osm-tiles.exe"
if not exist "%BIN%" (
    echo Binary not found under %BUILD_DIR%
    popd
    exit /b 1
)

echo.
echo ==^> Running %BIN%
echo     data: %DATA_DIR%
echo     http://127.0.0.1:8080
"%BIN%" --data "%DATA_DIR%" --port 8080

popd
endlocal
exit /b %ERRORLEVEL%
