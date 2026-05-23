@echo off
REM Windows build script. Run from "Developer Command Prompt for VS 2022"
REM (so that MSVC cl.exe is in PATH).
setlocal

cd /d "%~dp0\.."

set BUILD_DIR=build
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release

echo ==^> Windows build (type=%BUILD_TYPE%)

REM 自动找 Qt
set QT_PREFIX=
for %%v in (6.8.0 6.7.0 6.6.0 6.5.0) do (
    for %%c in (msvc2022_64 msvc2019_64 mingw_64) do (
        if exist "C:\Qt\%%v\%%c\lib\cmake\Qt6" (
            if "!QT_PREFIX!"=="" set QT_PREFIX=C:\Qt\%%v\%%c
        )
    )
)
setlocal EnableDelayedExpansion

if "%QT_PREFIX%"=="" (
    echo Error: Qt 6 not found at C:\Qt\6.x.x\^<compiler^>
    echo Please install Qt 6 from https://www.qt.io/download-open-source
    echo or set CMAKE_PREFIX_PATH manually.
    exit /b 1
)

echo ==^> Using Qt: %QT_PREFIX%

REM 检测 Ninja
set GENERATOR=Ninja
where ninja >nul 2>&1
if errorlevel 1 (
    set GENERATOR=NMake Makefiles
)

cmake -S . -B %BUILD_DIR% -G "%GENERATOR%" ^
      -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
      -DCMAKE_PREFIX_PATH="%QT_PREFIX%"
if errorlevel 1 exit /b 1

cmake --build %BUILD_DIR% -j
if errorlevel 1 exit /b 1

if not exist "%BUILD_DIR%\RegexBlocks.exe" (
    echo Error: build failed - %BUILD_DIR%\RegexBlocks.exe missing
    exit /b 1
)

echo.
echo ==^> Built: %BUILD_DIR%\RegexBlocks.exe

REM 把 Qt DLL 一起打包, 让 .exe 可独立分发
set WINDEPLOY=%QT_PREFIX%\bin\windeployqt.exe
if exist "%WINDEPLOY%" (
    echo ==^> windeployqt
    "%WINDEPLOY%" --release "%BUILD_DIR%\RegexBlocks.exe"
)

echo.
echo ==^> Done. Run with:
echo    %BUILD_DIR%\RegexBlocks.exe

endlocal
