#!/usr/bin/env bash
# Assemble the single (uncompressed) tar used to import the release into a
# reprepro repository.  It contains every .deb plus the matching source
# package (.dsc and source tar) and the per-architecture .buildinfo files.
# Raw binaries and the tar itself are kept outside and checksummed.
#
# Usage: scripts/make-repo-tar.sh [incoming-dir] [dist-dir]
set -euo pipefail

cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(rpp-release-tool VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "cannot parse project version from CMakeLists.txt" >&2; exit 1; }
SRC=${1:-incoming}
DIST=${2:-dist}

shopt -s nullglob
debs=("$SRC"/*.deb)
dscs=("$SRC"/*.dsc)
sources=("$SRC"/*.tar.xz)
buildinfos=("$SRC"/*.buildinfo)
[ ${#debs[@]} -gt 0 ] || { echo "no .deb in $SRC" >&2; exit 1; }
[ ${#dscs[@]} -gt 0 ] || { echo "no .dsc in $SRC" >&2; exit 1; }
[ ${#sources[@]} -gt 0 ] || { echo "no source tar in $SRC" >&2; exit 1; }
[ ${#buildinfos[@]} -gt 0 ] || { echo "no .buildinfo in $SRC" >&2; exit 1; }

rm -rf "$DIST"
mkdir -p "$DIST"
REPO_STAGE=$(mktemp -d)
trap 'rm -rf "$REPO_STAGE"' EXIT

cp "${debs[@]}" "${dscs[@]}" "${sources[@]}" "${buildinfos[@]}" "$REPO_STAGE"/
TARBALL="$DIST/rpp-release-tool-$VERSION-repo.tar"
case "$TARBALL" in
  /*) ;;
  *) TARBALL="$(pwd)/$TARBALL" ;;
esac
( cd "$REPO_STAGE" && tar -cf "$TARBALL" ./* )

cp "${debs[@]}" "$DIST"/
for f in "$SRC"/rpp-release-tool-*-linux-*; do
  [ -f "$f" ] && cp "$f" "$DIST"/
done

cd "$DIST"
files=(./*.deb ./*.tar)
for f in ./rpp-release-tool-*-linux-*; do
  [ -f "$f" ] && files+=("$f")
done
sha256sum "${files[@]}" > sha256sums.txt

echo "==> repo tar: $TARBALL"
ls -lh
