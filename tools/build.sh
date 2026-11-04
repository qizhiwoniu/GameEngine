#!/bin/bash
# GameEngine 一键构建（MSVC 14.51 + SDK 10.0.26100 + Qt 6.10 msvc2022_64）
# 用法：bash tools/build.sh
# 可用环境变量覆盖：QT_DIR / VSDIR / VCVER / SDKBASE / SDKVER
set -e

# 工程根目录由脚本位置推导，不写死绝对路径（clone 到任何位置都能构建）
# 注意：cmake 是原生 Windows 程序，必须给它 D:/xxx 形式的路径，不能是 /d/xxx
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
if command -v cygpath >/dev/null 2>&1; then
    ROOT="$(cygpath -m "$ROOT")"
fi

QT_DIR="${QT_DIR:-D:/QT/6.10.0/msvc2022_64}"
VSDIR="${VSDIR:-C:/Program Files/Microsoft Visual Studio/18/Community}"
VCVER="${VCVER:-14.51.36231}"
SDKBASE="${SDKBASE:-C:/Program Files (x86)/Windows Kits/10}"
SDKVER="${SDKVER:-10.0.26100.0}"
VC="$VSDIR/VC/Tools/MSVC/$VCVER"

if [ ! -d "$VC" ]; then
    echo "找不到 MSVC 工具链：$VC"
    echo "如果你的 VS 版本不同，请先设置环境变量，例如："
    echo "  VSDIR='C:/Program Files/Microsoft Visual Studio/2022/Community' VCVER=14.44.35207 bash tools/build.sh"
    exit 1
fi

# bash 的 PATH 用 POSIX 冒号分隔（且必须是 /c/xxx 形式，C:/xxx 带空格时 bash 查不到）；
# INCLUDE/LIB 保留 Windows 分号格式（给 cl.exe 用）
topath() { if command -v cygpath >/dev/null 2>&1; then cygpath -u "$1"; else echo "$1"; fi; }
export PATH="$(topath "$VC/bin/Hostx64/x64"):$(topath "$SDKBASE/bin/$SDKVER/x64"):$(topath "$VSDIR/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"):$(topath "$VSDIR/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin"):$PATH"
export INCLUDE="$VC\\include;$SDKBASE\\Include\\$SDKVER\\ucrt;$SDKBASE\\Include\\$SDKVER\\um;$SDKBASE\\Include\\$SDKVER\\shared;$SDKBASE\\Include\\$SDKVER\\winrt;$SDKBASE\\Include\\$SDKVER\\cppwinrt"
export LIB="$VC\\lib\\x64;$SDKBASE\\Lib\\$SDKVER\\ucrt\\x64;$SDKBASE\\Lib\\$SDKVER\\um\\x64"

cd "$ROOT"
cmake -S . -B out/build/x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$QT_DIR"
cmake --build out/build/x64-Debug

echo
echo "构建完成：$ROOT/out/build/x64-Debug/GameEngine/GameEngine.exe"
