#!/bin/bash
# 一键构建 + 打包 macOS .app + 可选生成 .dmg
set -e
cd "$(dirname "$0")/.."

BUILD_DIR="build"
BUILD_TYPE="${BUILD_TYPE:-Release}"

echo "==> macOS build (type=$BUILD_TYPE)"

# 自动找 Qt
QT_PREFIX=""
for p in /opt/homebrew/opt/qt /opt/homebrew/opt/qtbase \
         /usr/local/opt/qt /usr/local/opt/qtbase; do
    if [ -d "$p/lib/cmake/Qt6" ]; then
        QT_PREFIX="$p"
        break
    fi
done

if [ -z "$QT_PREFIX" ]; then
    # 回退到 Cellar 最新版
    QT_PREFIX=$(ls -d /opt/homebrew/Cellar/qtbase/* 2>/dev/null | sort -V | tail -1)
fi

if [ -z "$QT_PREFIX" ]; then
    echo "Error: Qt 6 not found. Install with: brew install qt"
    exit 1
fi

echo "==> Using Qt: $QT_PREFIX"

# 检测构建工具
GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
fi

cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build "$BUILD_DIR" -j

APP="$BUILD_DIR/RegexBlocks.app"
if [ ! -d "$APP" ]; then
    echo "Error: build failed - $APP missing"
    exit 1
fi

echo "==> Built: $APP"

# 可选: macdeployqt 打包成可独立分发的 .app + .dmg
if [ "$1" = "--dmg" ]; then
    DEPLOY="$QT_PREFIX/bin/macdeployqt"
    if [ -x "$DEPLOY" ]; then
        echo "==> macdeployqt + .dmg"
        "$DEPLOY" "$APP" -dmg
        echo "==> Created: $BUILD_DIR/RegexBlocks.dmg"
    else
        echo "Warning: macdeployqt not at $DEPLOY"
    fi
fi

echo ""
echo "==> Done. Run with:"
echo "   open $APP"
