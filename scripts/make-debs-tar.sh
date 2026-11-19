#!/usr/bin/env bash
# Collect the .deb files for all architectures into one (uncompressed) tar,
# the file you feed to the reprepro repository, and write SHA256 checksums.
#
# Usage: scripts/make-debs-tar.sh [dist-dir]
set -euo pipefail

cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(rpp-release-tool VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "cannot parse project version from CMakeLists.txt" >&2; exit 1; }
DIST=${1:-dist}

cd "$DIST"
shopt -s nullglob
debs=(./*.deb)
[ ${#debs[@]} -gt 0 ] || { echo "no .deb files in $DIST" >&2; exit 1; }

TARBALL="rpp-release-tool-debs-$VERSION.tar"
tar -cf "$TARBALL" "${debs[@]}"

files=(./*.deb ./*.tar)
for f in ./rpp-release-tool-*-linux-*; do
  [ -f "$f" ] && files+=("$f")
done
sha256sum "${files[@]}" > sha256sums.txt

echo "==> debs tar: $DIST/$TARBALL"
ls -lh
