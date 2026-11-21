#!/usr/bin/env bash
# End-to-end check of the non-TUI path using real dpkg-source / dpkg-deb.
set -euo pipefail

BIN=${1:?usage: integration.sh <rpp-release-tool>}
WORK=$(mktemp -d /tmp/rpp-it-XXXXXX)
trap 'code=$?; if [ $code -ne 0 ]; then echo "work kept: $WORK"; else rm -rf "$WORK"; fi' EXIT
export LC_ALL=C

say() { printf '  %s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- package ---
SRC="$WORK/src/ccid-1.8.4"
mkdir -p "$SRC/debian/source"
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
cat > "$SRC/debian/changelog" <<'EOF'
ccid (1.8.4-1) trixie; urgency=medium

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
echo "hello" > "$SRC/README"

mkdir -p "$WORK/upstream/ccid-1.8.4"
echo "hello" > "$WORK/upstream/ccid-1.8.4/README"
tar -C "$WORK/upstream" -cJf "$WORK/src/ccid_1.8.4.orig.tar.xz" ccid-1.8.4

PUB="$WORK/pub"
mkdir -p "$PUB"
( cd "$SRC" && dpkg-source -b . >/dev/null 2>&1 )
mv "$WORK/src"/*.dsc "$WORK/src"/*.debian.tar.* "$WORK/src"/*.orig.tar.* "$PUB"/

PKG="$WORK/pkg"
mkdir -p "$PKG/DEBIAN" "$PKG/usr/share/doc/libccid"
cat > "$PKG/DEBIAN/control" <<'EOF'
Source: ccid
Package: libccid
Version: 1.8.4-1
Architecture: amd64
Maintainer: Test <test@example.com>
Section: libs
Priority: optional
Description: test package for rpp-release-tool
 A long description line.
EOF
dpkg-deb --build "$PKG" "$PUB/libccid_1.8.4-1_amd64.deb" >/dev/null

# dpkg-source >= 1.19 omits the top-level Priority field; reprepro still needs it.
sed -i '/^Source: ccid$/a Priority: optional' "$PUB"/*.dsc
ls "$PUB" >/dev/null
say "built fixture: $(cd "$PUB" && ls | tr '\n' ' ')"

# ------------------------------------------------------------- fake repo ---
BASE="$WORK/repo"
mkdir -p "$BASE/conf" "$BASE/db" "$BASE/pool" \
         "$BASE/dists/trixie/main/binary-amd64" "$BASE/dists/trixie/main/source"
cat > "$BASE/conf/distributions" <<'EOF'
Codename: trixie
Suite: trixie
Components: main
Architectures: amd64 source
EOF
: > "$BASE/dists/trixie/main/binary-amd64/Packages"
: > "$BASE/dists/trixie/main/source/Sources"

CFG="$WORK/config.toml"
cat > "$CFG" <<EOF
version = 1
default_repo = "test"

[paths]
artifacts = "$PUB"

[policy]
require_both = true
missing_arch = "error"
buildinfo = "warn"
sha256 = "error"

[ui]
detail = "normal"
unchanged_fold = 15
language = "en-US"

[[repo]]
name = "test"
base = "$BASE"
conf = "$BASE/conf/distributions"
EOF

run_capture() {
  local out code
  set +e
  out=$("$@" 2>&1)
  code=$?
  set -e
  printf '%s' "$out"
  return $code
}

echo "[1] both .deb and .dsc, check-only"
set +e; out=$(run_capture "$BIN" --no-tui --check-only --ack --config "$CFG"); code=$?; set -e
if [ $code -ne 0 ]; then echo "$out" | sed 's/^/    /'; fail "expected exit 0, got $code"; fi
echo "$out" | grep -q "libccid" || fail "missing libccid in output"
echo "$out" | grep -q "checksum ok" || fail "no checksum line"
echo "$out" | grep -q "ccid" || fail "missing source package"
say "ok"

echo "[2] print commands"
out=$(run_capture "$BIN" --no-tui --print-cmd --ack --config "$CFG") || fail "expected exit 0"
echo "$out" | grep -q "includedsc" || fail "missing includedsc"
echo "$out" | grep -q "includedeb" || fail "missing includedeb"
echo "$out" | grep -q "reprepro -b $BASE" || fail "wrong base"
say "ok"

echo "[3] .dsc without Priority only warns"
PUB2="$WORK/pub2"
mkdir -p "$PUB2"
cp "$PUB"/* "$PUB2"/
sed -i '/^Priority:/d' "$PUB2"/*.dsc
out=$(run_capture "$BIN" --no-tui --check-only --ack --config "$CFG" "$PUB2") || fail "expected success"
echo "$out" | grep -q "\.dsc has no Priority" || fail "no .dsc priority warning"
say "ok"

echo "[4] binary-only needs acknowledgement"
PUB3="$WORK/pub3"
mkdir -p "$PUB3"
cp "$PUB"/*.deb "$PUB3"/
if run_capture "$BIN" --no-tui --check-only --config "$CFG" "$PUB3" >/dev/null; then
  fail "expected exit 2 without --ack"
fi
run_capture "$BIN" --no-tui --check-only --ack --config "$CFG" "$PUB3" >/dev/null || fail "expected exit 0 with --ack"
say "ok"

echo "[5] source-only needs acknowledgement too"
PUB4="$WORK/pub4"
mkdir -p "$PUB4"
cp "$PUB"/*.dsc "$PUB"/*.tar.* "$PUB4"/
out=$(run_capture "$BIN" --no-tui --check-only --config "$CFG" "$PUB4") || true
echo "$out" | grep -qi "source-only\|only .dsc" || fail "no source-only warning"
if run_capture "$BIN" --no-tui --check-only --config "$CFG" "$PUB4" >/dev/null; then
  fail "expected exit 2 without --ack"
fi
run_capture "$BIN" --no-tui --check-only --ack --config "$CFG" "$PUB4" >/dev/null || fail "expected exit 0 with --ack"
say "ok"

echo "[6] missing architecture warns and needs acknowledgement"
BASE2="$WORK/repo2"
mkdir -p "$BASE2/conf" "$BASE2/db" "$BASE2/pool" \
         "$BASE2/dists/trixie/main/binary-amd64" "$BASE2/dists/trixie/main/binary-arm64" \
         "$BASE2/dists/trixie/main/source"
cat > "$BASE2/conf/distributions" <<'EOF'
Codename: trixie
Suite: trixie
Components: main
Architectures: amd64 arm64 source
EOF
: > "$BASE2/dists/trixie/main/binary-amd64/Packages"
: > "$BASE2/dists/trixie/main/binary-arm64/Packages"
: > "$BASE2/dists/trixie/main/source/Sources"
CFG2="$WORK/config2.toml"
sed -e "s|$BASE|$BASE2|g" -e 's/missing_arch = "error"/missing_arch = "warn"/' "$CFG" > "$CFG2"
out=$(run_capture "$BIN" --no-tui --check-only --config "$CFG2") || true
echo "$out" | grep -q "missing a .deb for architecture arm64" || fail "no missing-arm64 warning"
if run_capture "$BIN" --no-tui --check-only --config "$CFG2" >/dev/null; then
  fail "expected exit 2 without --ack for missing arch"
fi
run_capture "$BIN" --no-tui --check-only --ack --config "$CFG2" >/dev/null || fail "expected exit 0 with --ack"
say "ok"

echo "[7] legacy codename/component/architectures in config are ignored"
CFG3="$WORK/config3.toml"
cat > "$CFG3" <<EOF
version = 1
default_repo = "test"

[paths]
artifacts = "$PUB"

[policy]
require_both = true
missing_arch = "error"
buildinfo = "warn"
sha256 = "error"

[[repo]]
name = "test"
base = "$BASE"
conf = "$BASE/conf/distributions"
codename = "ghost"
component = "ghost"
architectures = ["i386"]
EOF
out=$(run_capture "$BIN" --no-tui --check-only --ack --config "$CFG3") || fail "expected success"
echo "$out" | grep -q "test/trixie/main" || fail "legacy codename/component were not replaced by live conf values"
say "ok"

echo "[8] missing config"
if "$BIN" --no-tui --config "$WORK/nope.toml" >/dev/null 2>&1; then
  fail "expected config error"
fi
say "ok"

echo "integration: all checks passed"
