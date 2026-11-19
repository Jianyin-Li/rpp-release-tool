#!/usr/bin/env bash
# Build the release artifacts for the current architecture:
#   - a Debian package
#   - a raw (stripped) binary
# The "debs tar" for reprepro import is assembled separately by
# scripts/make-debs-tar.sh from all architectures.
#
# Usage: scripts/make-release.sh [build-dir] [dist-dir]
set -euo pipefail

cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(rpp-release-tool VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "cannot parse project version from CMakeLists.txt" >&2; exit 1; }
ARCH=$(dpkg --print-architecture 2>/dev/null || uname -m)

BUILD=${1:-build}
DIST=${2:-dist}

echo "==> configuring $BUILD"
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
echo "==> building"
cmake --build "$BUILD" -j"$(nproc)"

mkdir -p "$DIST"
# Clean files but keep the directory itself (it may be a mount point in CI).
find "$DIST" -mindepth 1 -maxdepth 1 -exec rm -rf {} +

echo "==> Debian package ($ARCH)"
cpack -G DEB -B "$DIST" --config "$BUILD/CPackConfig.cmake"
rm -rf "$DIST/_CPack_Packages"

echo "==> raw binary ($ARCH)"
BIN="$DIST/rpp-release-tool-$VERSION-linux-$ARCH"
cp "$BUILD/rpp-release-tool" "$BIN"
chmod 755 "$BIN"
if command -v strip >/dev/null 2>&1; then
  strip "$BIN"
fi

echo "==> artifacts"
ls -lh "$DIST"
