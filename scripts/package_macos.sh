#!/bin/bash
# ============================================================
#  NaviVision macOS .app Bundle Packaging Script
#  Usage: ./scripts/package_macos.sh [build_dir]
#    build_dir: CMake build directory (default: build_macos)
#
#  Output: release/NaviVision.app
# ============================================================

set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$PROJECT_DIR/build_macos}"
OUT="$PROJECT_DIR/release"
APP="$OUT/NaviVision.app"
CONTENTS="$APP/Contents"
MACOS="$CONTENTS/MacOS"
FRAMEWORKS="$CONTENTS/Frameworks"
RESOURCES="$CONTENTS/Resources"

echo ""
echo "========================================="
echo "  NaviVision macOS .app Packager"
echo "========================================="
echo "  Build dir : $BUILD_DIR"
echo "  Output to : $APP"
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
mkdir -p "$MACOS" "$FRAMEWORKS" "$RESOURCES"

# ── 生成 Info.plist ──
cat > "$CONTENTS/Info.plist" << 'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>NaviVision</string>
    <key>CFBundleDisplayName</key>
    <string>NaviVision</string>
    <key>CFBundleIdentifier</key>
    <string>com.navivision.app</string>
    <key>CFBundleVersion</key>
    <string>1.0.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleExecutable</key>
    <string>NaviVision</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
</dict>
</plist>
PLIST
echo "[CREATE] Info.plist"

# ── 复制主程序 ──
echo "[COPY] NaviVision"
cp "$BUILD_DIR/NaviVision" "$MACOS/"

# ── 复制运行时 dylib 到 Frameworks ──
DYLIB_DIRS=("$BUILD_DIR/bin" "$BUILD_DIR/mtmd")
DYLIBS=(libllama libggml-base libggml-cpu libggml-blas libggml libmtmd)

for name in "${DYLIBS[@]}"; do
    found=false
    for dir in "${DYLIB_DIRS[@]}"; do
        real=$(find "$dir" -maxdepth 1 -name "${name}.*.dylib" ! -type l 2>/dev/null | head -1)
        if [ -n "$real" ]; then
            echo "[COPY] $(basename "$real")"
            cp "$real" "$FRAMEWORKS/"
            base=$(basename "$real")
            link="${name}.0.dylib"
            if [ "$base" != "$link" ]; then
                ln -sf "$base" "$FRAMEWORKS/$link"
            fi
            found=true
            break
        fi
    done
    if [ "$found" = false ]; then
        echo "[WARN] $name not found, skipping"
    fi
done

# ── 修正 rpath ──
# 删除旧的 rpath，添加 @executable_path/../Frameworks
install_name_tool -add_rpath @executable_path/../Frameworks "$MACOS/NaviVision" 2>/dev/null || true

# dylib 间互相引用
for dylib in "$FRAMEWORKS/"*.dylib; do
    [ -L "$dylib" ] && continue
    install_name_tool -add_rpath @loader_path "$dylib" 2>/dev/null || true
done

# ── 复制 Resources ──
if [ -d "$PROJECT_DIR/profiles" ]; then
    echo "[COPY] profiles/"
    cp -r "$PROJECT_DIR/profiles" "$RESOURCES/profiles"
else
    echo "[WARN] profiles directory not found"
fi

mkdir -p "$RESOURCES/models"
echo "[CREATE] models/ (empty, for downloaded models)"

# 如果有图标文件则复制
if [ -f "$PROJECT_DIR/AppIcon.icns" ]; then
    cp "$PROJECT_DIR/AppIcon.icns" "$RESOURCES/"
    echo "[COPY] AppIcon.icns"
fi

# ── 验证 ──
echo ""
echo "[INFO] Verifying binary..."
missing=$(otool -L "$MACOS/NaviVision" | grep "not found" || true)
if [ -n "$missing" ]; then
    echo "[WARN] Some libraries not found:"
    echo "$missing"
else
    echo "[OK] All dylib dependencies resolved"
fi

# ── 统计 ──
FILE_COUNT=$(find "$APP" -type f | wc -l | tr -d ' ')
TOTAL_SIZE=$(du -sh "$APP" | cut -f1)

echo ""
echo "========================================="
echo "  Packaging complete!"
echo "========================================="
echo "  Files: $FILE_COUNT"
echo "  Total size: $TOTAL_SIZE"
echo "  Output: $APP"
echo "========================================="
echo ""
echo "  Double-click NaviVision.app to launch,"
echo "  or run: open $APP"
echo ""
