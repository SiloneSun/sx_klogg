#!/bin/bash
# sxl_build.sh - 编译 sx_klogg 并部署到任务栏图标指向的路径
# 用法: ./sxl_build.sh
# 完成后即可通过任务栏图标打开新编译的 sx_klogg
set -e

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SRC_DIR/build_cmake331"
INSTALL_BIN="/home/sunxilong/opt/sx-klogg/bin/klogg"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 首次运行才做 cmake 配置，之后走增量编译
# (第三方依赖缓存在 _deps 中，不清空构建目录可避免重新下载和全量重编)
if [ ! -f CMakeCache.txt ]; then
    cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DKLOGG_USE_LTO=ON \
          -DKLOGG_USE_VECTORSCAN=ON \
          -DKLOGG_USE_HYPERSCAN=OFF \
          -DKLOGG_USE_SENTRY=OFF \
          -DKLOGG_BUILD_TESTS=ON \
          ..
fi

make -j8

[ -f output/klogg ] || { echo "ERROR: output/klogg 未生成"; exit 1; }

# strip 后部署到图标路径；构建目录内保留带调试信息的版本
strip -o "$INSTALL_BIN" "$BUILD_DIR/output/klogg"
chmod +x "$INSTALL_BIN"

echo "=============================="
echo "构建完成，已部署到: $INSTALL_BIN"
echo "现在可以通过任务栏图标打开新版 sx_klogg"
