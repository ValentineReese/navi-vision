#!/bin/bash
# ============================================================
#  NaviVision macOS Build Script
#
#  前置条件:
#    brew install cmake glfw imgui curl
#    vcpkg install imgui[glfw-binding,opengl3-binding]:x64-osx
#  或者使用 vcpkg 工具链:
#    cmake -B build_macos \
#      -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_macos"

echo "========================================="
echo "  NaviVision macOS Builder"
echo "========================================="

# ── 检查依赖 ──
if ! command -v cmake &> /dev/null; then
    echo "[ERROR] cmake not found. Install with: brew install cmake"
    exit 1
fi

# ── CMake 配置 ──
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "[INFO] Configuring CMake..."

    CMAKE_ARGS="-B $BUILD_DIR -S $SCRIPT_DIR"

    # 如果设置了 VCPKG_ROOT，使用 vcpkg 工具链
    if [ -n "$VCPKG_ROOT" ]; then
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
        echo "[INFO] Using vcpkg toolchain: $VCPKG_ROOT"
    fi

    cmake $CMAKE_ARGS
fi

# ── 构建 ──
echo "[INFO] Building..."
cmake --build "$BUILD_DIR" --config Release -j$(sysctl -n hw.ncpu)

echo ""
echo "[INFO] Build complete!"
echo "[INFO] Binary: $BUILD_DIR/NaviVision"

# ── 运行测试（可选） ──
if [ -f "$BUILD_DIR/NaviVisionTest" ]; then
    echo ""
    echo "[INFO] Running tests..."
    "$BUILD_DIR/NaviVisionTest"
fi

# ── 启动（可选，传入 --run 参数） ──
if [ "$1" = "--run" ]; then
    echo ""
    echo "[INFO] Launching NaviVision..."
    "$BUILD_DIR/NaviVision" &
fi
