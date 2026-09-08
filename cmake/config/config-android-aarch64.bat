@echo off
setlocal enabledelayedexpansion

REM Get script directory and git root directory
set "BASEDIR=%~dp0"
set "GITDIR=%BASEDIR%..\..\"

REM Normalize paths
pushd "%GITDIR%"
set "GITDIR=%CD%"
popd

REM Check NDK_PATH, fallback to ANDROID_SDK_ROOT
if "%NDK_PATH%"=="" (
    if "%ANDROID_SDK_ROOT%"=="" (
        echo ANDROID_SDK_ROOT or NDK_PATH variable should be set to configure cmake.
        exit /b 1
    )
    if not exist "%ANDROID_SDK_ROOT%" (
        echo '%ANDROID_SDK_ROOT%' is not valid!
        exit /b 1
    )
    set "NDK_PATH=%ANDROID_SDK_ROOT%\ndk-bundle"
)

REM Verify NDK_PATH exists
if not exist "%NDK_PATH%" (
    echo '%NDK_PATH%' is not valid!
    exit /b 1
)

REM Run cmake configuration
cmake "%GITDIR%" ^
      -DQBDI_PLATFORM=android ^
      -DQBDI_ARCH=AARCH64 ^
      -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DCMAKE_TOOLCHAIN_FILE="%NDK_PATH%/build/cmake/android.toolchain.cmake" ^
      -DANDROID_ABI="arm64-v8a" ^
      -DANDROID_PLATFORM=24 ^
      -G Ninja

if errorlevel 1 (
    echo CMake configuration failed!
    exit /b 1
)

echo CMake configuration completed successfully!
