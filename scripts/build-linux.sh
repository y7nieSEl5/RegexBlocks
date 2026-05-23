#!/bin/bash
# 一键构建 Linux 可执行文件
set -e
cd "$(dirname "$0")/.."

BUILD_DIR="build"
BUILD_TYPE="${BUILD_TYPE:-Release}"

echo "==> Linux build (type=$BUILD_TYPE)"

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
fi

cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j"$(nproc)"

if [ ! -x "$BUILD_DIR/RegexBlocks" ]; then
    echo "Error: build failed - $BUILD_DIR/RegexBlocks missing"
    exit 1
fi

echo ""
echo "==> Done. Run with:"
echo "   ./$BUILD_DIR/RegexBlocks"
echo ""
echo "==> Optional system install:"
echo "   sudo cmake --install $BUILD_DIR --prefix /usr/local"
