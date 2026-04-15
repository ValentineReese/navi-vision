#!/bin/bash
# ============================================================
#  NaviVision macOS Release Packaging Script
#  Usage: ./scripts/package_macos.sh [build_dir]
#    build_dir: CMake build directory (default: build_macos)
# ============================================================

set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$PROJECT_DIR/build_macos}"
OUT="$PROJECT_DIR/release"

echo ""
echo "========================================="
echo "  NaviVision macOS Release Packager"
echo "========================================="
echo "  Build dir : $BUILD_DIR"
echo "  Output to : $OUT"
echo ""

# ── 检查构建产物 ──
if [ ! -f "$BUILD_DIR/NaviVision" ]; then
    echo "[ERROR] NaviVision binary not found in $BUILD_DIR"
    echo "        Please build first: ./scripts/build_and_run_macos.sh"
    exit 1
fi

# ── 清理旧的 release 目录 ──
if [ -d "$OUT" ]; then
    echo "[INFO] Cleaning old release directory..."
    rm -rf "$OUT"
fi
mkdir -p "$OUT/lib"

# ── 复制主程序 ──
echo "[COPY] NaviVision"
cp "$BUILD_DIR/NaviVision" "$OUT/"

# ── 复制运行时 dylib ──
DYLIB_DIRS=("$BUILD_DIR/bin" "$BUILD_DIR/mtmd")
DYLIBS=(libllama libggml-base libggml-cpu libggml-blas libggml libmtmd)

for name in "${DYLIBS[@]}"; do
    found=false
    for dir in "${DYLIB_DIRS[@]}"; do
        # 精确匹配：name.版本号.dylib（排除 name-xxx 开头的）
        real=$(find "$dir" -maxdepth 1 -name "${name}.*.dylib" ! -type l 2>/dev/null | head -1)
        if [ -n "$real" ]; then
            echo "[COPY] $(basename "$real")"
            cp "$real" "$OUT/lib/"
            # 创建 .0.dylib 符号链接
            base=$(basename "$real")
            link="${name}.0.dylib"
            if [ "$base" != "$link" ]; then
                ln -sf "$base" "$OUT/lib/$link"
            fi
            found=true
            break
        fi
    done
    if [ "$found" = false ]; then
        echo "[WARN] $name not found, skipping"
    fi
done

# ── 修正 rpath：让二进制文件在 release 目录中找到 lib/ 下的 dylib ──
install_name_tool -add_rpath @executable_path/lib "$OUT/NaviVision" 2>/dev/null || true

# 同时修正 dylib 间的互相引用
for dylib in "$OUT/lib/"*.dylib; do
    [ -L "$dylib" ] && continue  # 跳过符号链接
    install_name_tool -add_rpath @loader_path "$dylib" 2>/dev/null || true
done

# ── 复制 profiles 目录 ──
if [ -d "$PROJECT_DIR/profiles" ]; then
    echo "[COPY] profiles/"
    cp -r "$PROJECT_DIR/profiles" "$OUT/profiles"
else
    echo "[WARN] profiles directory not found"
fi

# ── 创建空 models 目录 ──
mkdir -p "$OUT/models"
echo "[CREATE] models/ (empty, for downloaded models)"

# ── 验证二进制可运行 ──
echo ""
echo "[INFO] Verifying binary..."
if "$OUT/NaviVision" --help >/dev/null 2>&1 || true; then
    # 检查 dylib 依赖是否都能找到
    missing=$(otool -L "$OUT/NaviVision" | grep "not found" || true)
    if [ -n "$missing" ]; then
        echo "[WARN] Some libraries not found:"
        echo "$missing"
    else
        echo "[OK] All dylib dependencies resolved"
    fi
fi

# ── 统计 ──
FILE_COUNT=$(find "$OUT" -type f | wc -l | tr -d ' ')
TOTAL_SIZE=$(du -sh "$OUT" | cut -f1)

echo ""
echo "========================================="
echo "  Packaging complete!"
echo "========================================="
echo "  Files: $FILE_COUNT"
echo "  Total size: $TOTAL_SIZE"
echo "  Output: $OUT/"
echo "========================================="
echo ""
