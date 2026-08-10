#!/bin/sh
# ============================================================================
#  TOWNSEMU (Tsugaru) libretro core 一键构建脚本
#
#  适用环境：
#    - MSYS2 MinGW 64-bit 终端（Windows）：需先装 gcc + cmake
#        pacman -S --needed base-devel mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake
#    - Linux / WSL 终端：需 gcc + cmake（Debian: apt install build-essential cmake）
#
#  用法：
#    cd TOWNSEMU/src/main_libretro
#    sh build_libretro.sh
#
#  产物：
#    Windows : src/build_libretro/src/main_libretro/towns_libretro.dll
#    Linux   : src/build_libretro/src/main_libretro/towns_libretro.so  (改名为 .dylib 可给 macOS)
# ============================================================================
set -e

# 该脚本位于 src/main_libretro/，向上一级即为 src/（CMake 工程根）
SRC=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$SRC/build_libretro"

echo "==> 源码目录: $SRC"
echo "==> 构建目录: $BUILD"

rm -rf "$BUILD"
mkdir -p "$BUILD"
cd "$BUILD"

# 判断是否在 MSYS2 / MinGW 环境
if [ "$(uname -o 2>/dev/null)" = "Msys" ] || [ "$OSTYPE" = "msys" ] || [ "$OSTYPE" = "cygwin" ]; then
    echo "==> 检测到 Windows/MSYS2，使用 MinGW Makefiles"
    cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_LIBRETRO=ON "$SRC"
    cmake --build . --target towns_libretro -j"$(nproc 2>/dev/null || echo 4)"
    echo ""
    echo "==> 构建完成！产物："
    echo "    $BUILD/src/main_libretro/towns_libretro.dll"
else
    echo "==> 检测到类 Unix 环境"
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_LIBRETRO=ON "$SRC"
    cmake --build . --target towns_libretro -j"$(nproc 2>/dev/null || echo 4)"
    echo ""
    echo "==> 构建完成！产物："
    echo "    $BUILD/src/main_libretro/towns_libretro.so"
fi
