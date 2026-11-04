#!/bin/bash
# GameEngine 一键构建（MSVC 14.51 + SDK 10.0.26100 + Qt 6.10 msvc2022_64）
set -e
ROOT="D:/visualstudio/GameEngine"
VSDIR="C:/Program Files/Microsoft Visual Studio/18/Community"
VC="$VSDIR/VC/Tools/MSVC/14.51.36231"
SDKBASE="C:/Program Files (x86)/Windows Kits/10"
SDKVER="10.0.26100.0"

# bash 的 PATH 用 POSIX 冒号分隔；INCLUDE/LIB 保留 Windows 分号格式（给 cl.exe 用）
export PATH="/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64:/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja:/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin:$PATH"
export INCLUDE="$VC\\include;$SDKBASE\\Include\\$SDKVER\\ucrt;$SDKBASE\\Include\\$SDKVER\\um;$SDKBASE\\Include\\$SDKVER\\shared;$SDKBASE\\Include\\$SDKVER\\winrt;$SDKBASE\\Include\\$SDKVER\\cppwinrt"
export LIB="$VC\\lib\\x64;$SDKBASE\\Lib\\$SDKVER\\ucrt\\x64;$SDKBASE\\Lib\\$SDKVER\\um\\x64"

cd "$ROOT"
cmake -S . -B out/build/x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="D:/QT/6.10.0/msvc2022_64"
cmake --build out/build/x64-Debug
