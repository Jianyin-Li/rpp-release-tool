#!/usr/bin/env bash
# Build the release artifacts for the current architecture with dpkg-buildpackage:
#   - .deb, .buildinfo, raw binary
#   - optionally the native source package (.dsc + source tar)
#
# Usage: scripts/make-release.sh [--source] [dist-dir]
#   --source   also build the source package (do this on one architecture)
set -euo pipefail

cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(rpp-release-tool VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "cannot parse project version from CMakeLists.txt" >&2; exit 1; }
CHANGELOG_VERSION=$(sed -n '1s/.*(\([^)]*\)).*/\1/p' debian/changelog)
[ "$VERSION" = "$CHANGELOG_VERSION" ] || {
  echo "version mismatch: CMake=$VERSION debian/changelog=$CHANGELOG_VERSION" >&2
  exit 1
}
ARCH=$(dpkg --print-architecture)
# Keep release builds within two cores unless the caller overrides it.
export DEB_BUILD_OPTIONS="${DEB_BUILD_OPTIONS:-parallel=2}"

SOURCE=0
DIST=dist
for arg in "$@"; do
  case "$arg" in
    --source) SOURCE=1 ;;
    -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
    *) DIST=$arg ;;
  esac
done

command -v dpkg-buildpackage >/dev/null || {
  echo "dpkg-buildpackage not found (install dpkg-dev + debhelper)" >&2
  exit 1
}

rm -rf "$DIST"
mkdir -p "$DIST"

if [ "$SOURCE" = 1 ]; then
  echo "==> source package"
  # A native source tar takes the basename of its build directory.  Build a
  # copy named rpp-release-tool-<version> so the tar has the standard
  # top-level directory instead of the checkout directory name (e.g. "work").
  SRC_PARENT=$(mktemp -d)
  SRC_DIR="$SRC_PARENT/rpp-release-tool-${VERSION}"
  mkdir -p "$SRC_DIR"
  tar -C "$PWD" --exclude=.git --exclude=build --exclude=dist --exclude='obj-*' \
      -cf - . | tar -C "$SRC_DIR" -xf -
  ( cd "$SRC_DIR" && dpkg-buildpackage -us -uc -S )
  cp "$SRC_PARENT/rpp-release-tool_${VERSION}.dsc" \
     "$SRC_PARENT/rpp-release-tool_${VERSION}.tar.xz" "$DIST"/
  # dpkg-source >= 1.19 omits the top-level Priority field, which reprepro
  # requires ("No priority, skipping").  Put it back.
  sed -i '/^Source: /a Priority: optional' "$DIST/rpp-release-tool_${VERSION}.dsc"
  rm -rf "$SRC_PARENT"
fi

echo "==> binary package ($ARCH)"
dpkg-buildpackage -us -uc -b

DEB="../rpp-release-tool_${VERSION}_${ARCH}.deb"
BUILDINFO="../rpp-release-tool_${VERSION}_${ARCH}.buildinfo"
cp "$DEB" "$BUILDINFO" "$DIST"/

echo "==> raw binary ($ARCH)"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
dpkg-deb -x "$DEB" "$TMP"
cp "$TMP/usr/bin/rpp-release-tool" "$DIST/rpp-release-tool-${VERSION}-linux-${ARCH}"
chmod 755 "$DIST/rpp-release-tool-${VERSION}-linux-${ARCH}"

echo "==> artifacts"
ls -lh "$DIST"
