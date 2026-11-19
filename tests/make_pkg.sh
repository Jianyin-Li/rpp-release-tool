#!/usr/bin/env bash
# Build a small source+binary fixture into <outdir>.
# usage: make_pkg.sh <outdir> [version] [arch] [payload-seed]
set -euo pipefail
OUT=${1:?outdir}
VER=${2:-1.8.4-1}
ARCH=${3:-amd64}
SEED=${4:-}
UPVER=${VER%-*}          # 1.8.4-1 -> 1.8.4
SRCNAME=ccid
WORK=$(mktemp -d /tmp/rpp-mk-XXXXXX)
trap 'rm -rf "$WORK"' EXIT
export LC_ALL=C

SRC="$WORK/src/${SRCNAME}-${UPVER}"
mkdir -p "$SRC/debian/source" "$WORK/upstream/${SRCNAME}-${UPVER}" "$OUT"
cat > "$SRC/debian/control" <<'EOF'
Source: ccid
Section: libs
Priority: optional
Maintainer: Test <test@example.com>
Build-Depends: debhelper-compat (= 13)
Standards-Version: 4.6.2
Rules-Requires-Root: no

Package: libccid
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}
Description: test package for rpp-release-tool
 A long description line.
EOF
cat > "$SRC/debian/changelog" <<EOF
${SRCNAME} (${VER}) trixie; urgency=medium

  * test build

 -- Test <test@example.com>  Thu, 01 Jan 2026 00:00:00 +0000
EOF
printf '3.0 (quilt)\n' > "$SRC/debian/source/format"
cat > "$SRC/debian/rules" <<'EOF'
#!/usr/bin/make -f
%:
	dh $@
EOF
chmod +x "$SRC/debian/rules"
echo "payload ${VER} ${SEED}" > "$SRC/README"
echo "payload ${VER} ${SEED}" > "$WORK/upstream/${SRCNAME}-${UPVER}/README"
tar -C "$WORK/upstream" -cJf "$WORK/src/${SRCNAME}_${UPVER}.orig.tar.xz" "${SRCNAME}-${UPVER}"

( cd "$SRC" && dpkg-source -b . >/dev/null 2>&1 )
mv "$WORK/src"/*.dsc "$WORK/src"/*.debian.tar.* "$WORK/src"/*.orig.tar.* "$OUT"/

PKG="$WORK/pkg"
mkdir -p "$PKG/DEBIAN" "$PKG/usr/share/doc/libccid"
cat > "$PKG/DEBIAN/control" <<EOF
Source: ${SRCNAME}
Package: libccid
Version: ${VER}
Architecture: ${ARCH}
Maintainer: Test <test@example.com>
Section: libs
Priority: optional
Description: test package for rpp-release-tool
 A long description line.
EOF
echo "payload ${VER} ${SEED}" > "$PKG/usr/share/doc/libccid/README"
dpkg-deb --build "$PKG" "$OUT/libccid_${VER}_${ARCH}.deb" >/dev/null 2>&1

# reprepro (unlike dpkg-source) requires a top-level Priority in .dsc
sed -i "/^Source: ${SRCNAME}\$/a Priority: optional" "$OUT"/*.dsc
echo "built ${VER} ${ARCH} in ${OUT}"
