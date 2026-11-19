#!/usr/bin/env bash
# Generate a fixture matrix for rpp-release-tool:
#   - artifact sets: complete / arch:all / source-only / binary-only / broken...
#   - repositories: main only, main+contrib, with/without source arch
#   - ready-to-use configs for host and container paths
#
# usage: make_fixtures.sh [base-dir]
#   base-dir defaults to /tmp/rpp-real/fixtures
#   RPP_TEST_KEY=<gpg-key-id> adds SignWith: to every distribution
#   GUEST_BASE=/real/fixtures  controls the container-side paths in configs/
#
# The repositories are only skeletons (conf/distributions).  Inside the
# container run `reprepro -b <repo> export` once to create db/pool/dists.
set -euo pipefail
export LC_ALL=C

BASE=${1:-/tmp/rpp-real/fixtures}
GUEST_BASE=${GUEST_BASE:-/real/fixtures}
SIGN=${RPP_TEST_KEY:-}
HOST_BASE=$BASE

rm -rf "$BASE"
mkdir -p "$BASE"/{arts,repos,configs,configs-host,pkgs/broken}

# ------------------------------------------------------------------ helpers --
make_source() {  # <outdir> <src> <version> <binary> <binary-arch>
  local out=$1 src=$2 ver=$3 binname=$4 binarch=$5
  local up=${ver%-*}
  local work; work=$(mktemp -d /tmp/rpp-fx-XXXXXX)
  local sd="$work/$src-$up"
  mkdir -p "$sd/debian/source" "$work/up/$src-$up" "$out"
  cat > "$sd/debian/control" <<EOF
Source: $src
Section: libs
Priority: optional
Maintainer: Fixture <fixture@example.com>
Build-Depends: debhelper-compat (= 13)
Standards-Version: 4.6.2
Rules-Requires-Root: no

Package: $binname
Architecture: $binarch
Depends: \${shlibs:Depends}, \${misc:Depends}
Description: fixture package $binname
 Long description for fixtures.
EOF
  cat > "$sd/debian/changelog" <<EOF
$src ($ver) trixie; urgency=medium

  * fixture build

 -- Fixture <fixture@example.com>  Thu, 01 Jan 2026 00:00:00 +0000
EOF
  printf '3.0 (quilt)\n' > "$sd/debian/source/format"
  printf '#!/usr/bin/make -f\n%%:\n\tdh $@\n' > "$sd/debian/rules"
  chmod +x "$sd/debian/rules"
  echo "fixture $src $ver" > "$sd/README"
  echo "fixture $src $ver" > "$work/up/$src-$up/README"
  tar -C "$work/up" -cJf "$work/${src}_${up}.orig.tar.xz" "$src-$up"
  ( cd "$sd" && dpkg-source -b . >/dev/null 2>&1 )
  mv "$work"/*.dsc "$work"/*.debian.tar.* "$work"/*.orig.tar.* "$out"/
  rm -rf "$work"
}

add_dsc_priority() {  # <dir>
  sed -i "/^Source: /a Priority: optional" "$1"/*.dsc
}

make_deb() {  # <outfile> <pkg> <version> <arch> <source> [omit-fields-regex]
  local out=$1 pkg=$2 ver=$3 arch=$4 src=$5 omit=${6:-}
  mkdir -p "$(dirname "$out")"
  local d; d=$(mktemp -d /tmp/rpp-fx-XXXXXX)
  mkdir -p "$d/DEBIAN" "$d/usr/share/doc/$pkg"
  cat > "$d/DEBIAN/control" <<EOF
Source: $src
Package: $pkg
Version: $ver
Architecture: $arch
Maintainer: Fixture <fixture@example.com>
Section: libs
Priority: optional
Description: fixture package $pkg
 Long description for fixtures.
EOF
  if [ -n "$omit" ]; then
    grep -v -E "^($omit):" "$d/DEBIAN/control" > "$d/DEBIAN/control.new" || true
    mv "$d/DEBIAN/control.new" "$d/DEBIAN/control"
  fi
  echo "$pkg $ver $arch" > "$d/usr/share/doc/$pkg/README"
  dpkg-deb --build "$d" "$out" >/dev/null 2>&1
  rm -rf "$d"
}

make_repo() {  # <dir> <components> <architectures>
  local dir=$1 comps=$2 archs=$3
  mkdir -p "$dir/conf"
  {
    echo "Codename: trixie"
    echo "Suite: trixie"
    echo "Components: $comps"
    echo "Architectures: $archs"
    [ -n "$SIGN" ] && echo "SignWith: $SIGN"
  } > "$dir/conf/distributions"
}

emit_config() {  # <file> <repo-dir> <component> <artifacts-dir>
  local file=$1 repo=$2 comp=$3 art=$4
  mkdir -p "$(dirname "$file")"
  cat > "$file" <<EOF
version = 1
default_repo = "fixture"

[paths]
artifacts = "$art"

[policy]
require_both   = true
missing_arch   = "error"
buildinfo      = "warn"
sha256         = "error"
allow_replace  = false

[ui]
detail         = "normal"
unchanged_fold = 15
language       = "zh-CN"

[[repo]]
name          = "fixture"
base          = "$repo"
conf          = "$repo/conf/distributions"
codename      = "trixie"
component     = "$comp"
architectures = ["amd64", "arm64", "source"]
EOF
}

# --------------------------------------------------------------- artifacts ---
# complete: source + amd64 + arm64 binary
D="$BASE/arts/complete"
make_source "$D" ccid 1.8.4-1 libccid any
make_deb "$D/libccid_1.8.4-1_amd64.deb" libccid 1.8.4-1 amd64 ccid
make_deb "$D/libccid_1.8.4-1_arm64.deb" libccid 1.8.4-1 arm64 ccid
add_dsc_priority "$D"

# complete-2.0: a fresh version, handy for a real publish
D="$BASE/arts/complete-2.0"
make_source "$D" ccid 2.0-1 libccid any
make_deb "$D/libccid_2.0-1_amd64.deb" libccid 2.0-1 amd64 ccid
make_deb "$D/libccid_2.0-1_arm64.deb" libccid 2.0-1 arm64 ccid
add_dsc_priority "$D"

# arch-all: only an Architecture: all binary
D="$BASE/arts/arch-all"
make_source "$D" ccid-data 1.8.4-1 libccid-data all
make_deb "$D/libccid-data_1.8.4-1_all.deb" libccid-data 1.8.4-1 all ccid-data
add_dsc_priority "$D"

# source-only
D="$BASE/arts/source-only"
make_source "$D" ccid 1.8.5-1 libccid any
add_dsc_priority "$D"

# binary-only
D="$BASE/arts/binary-only"
make_deb "$D/libccid_1.8.5-1_amd64.deb" libccid 1.8.5-1 amd64 ccid

# missing one declared architecture
cp -a "$BASE/arts/complete" "$BASE/arts/missing-arm64"
rm -f "$BASE/arts/missing-arm64"/*_arm64.deb

# broken: checksum mismatch on a referenced tarball
cp -a "$BASE/arts/complete" "$BASE/arts/broken-checksum"
printf 'corrupted' >> "$BASE/arts/broken-checksum"/ccid_1.8.4.orig.tar.xz

# broken: .dsc without Priority
D="$BASE/arts/broken-no-dsc-priority"
make_source "$D" ccid 1.8.4-1 libccid any
make_deb "$D/libccid_1.8.4-1_amd64.deb" libccid 1.8.4-1 amd64 ccid
make_deb "$D/libccid_1.8.4-1_arm64.deb" libccid 1.8.4-1 arm64 ccid

# broken: .deb without Priority
D="$BASE/arts/broken-no-deb-priority"
make_source "$D" ccid 1.8.4-1 libccid any
make_deb "$D/libccid_1.8.4-1_amd64.deb" libccid 1.8.4-1 amd64 ccid "Priority"
make_deb "$D/libccid_1.8.4-1_arm64.deb" libccid 1.8.4-1 arm64 ccid "Priority"
add_dsc_priority "$D"

# broken: dsc version != deb version
D="$BASE/arts/broken-version-mismatch"
make_source "$D" ccid 1.8.4-1 libccid any
make_deb "$D/libccid_1.8.5-1_amd64.deb" libccid 1.8.5-1 amd64 ccid
make_deb "$D/libccid_1.8.5-1_arm64.deb" libccid 1.8.5-1 arm64 ccid
add_dsc_priority "$D"

# broken: deb Source != dsc Source
D="$BASE/arts/broken-source-mismatch"
make_source "$D" ccid 1.8.4-1 libccid any
make_deb "$D/libccid_1.8.4-1_amd64.deb" libccid 1.8.4-1 amd64 otherpkg
make_deb "$D/libccid_1.8.4-1_arm64.deb" libccid 1.8.4-1 amd64 otherpkg
add_dsc_priority "$D"

# broken: .dsc references a missing tarball
cp -a "$BASE/arts/complete" "$BASE/arts/broken-missing-file"
rm -f "$BASE/arts/broken-missing-file"/ccid_1.8.4.orig.tar.xz

# broken: .dsc declares a binary that is not present
cp -a "$BASE/arts/complete" "$BASE/arts/broken-missing-binary"
sed -i 's/^Binary: .*/Binary: libccid, ccid-tools/' "$BASE/arts/broken-missing-binary"/*.dsc

# broken: truncated .deb
cp -a "$BASE/arts/complete" "$BASE/arts/broken-corrupt-deb"
head -c 64 "$BASE/arts/complete/libccid_1.8.4-1_amd64.deb" \
  > "$BASE/arts/broken-corrupt-deb/libccid_1.8.4-1_amd64.deb"

# ------------------------------------------------------------------- pkgs ---
make_deb "$BASE/pkgs/libccid_1.8.4-1_amd64.deb" libccid 1.8.4-1 amd64 ccid
make_deb "$BASE/pkgs/libccid_1.8.4-1_arm64.deb" libccid 1.8.4-1 arm64 ccid
make_deb "$BASE/pkgs/libccid-data_1.8.4-1_all.deb" libccid-data 1.8.4-1 all ccid-data
make_deb "$BASE/pkgs/broken/libccid_1.8.4-1_no-priority_amd64.deb" libccid 1.8.4-1 amd64 ccid "Priority"
make_deb "$BASE/pkgs/broken/libccid_1.8.4-1_no-maintainer_amd64.deb" libccid 1.8.4-1 amd64 ccid "Maintainer"
make_deb "$BASE/pkgs/broken/libccid_1.8.4-1_riscv64.deb" libccid 1.8.4-1 riscv64 ccid
head -c 64 "$BASE/pkgs/libccid_1.8.4-1_amd64.deb" \
  > "$BASE/pkgs/broken/libccid_1.8.4-1_truncated_amd64.deb"

# ------------------------------------------------------------------ repos ---
make_repo "$BASE/repos/main-src"          "main"         "amd64 arm64 source"
make_repo "$BASE/repos/main-nosrc"        "main"         "amd64 arm64"
make_repo "$BASE/repos/main-contrib-src"  "main contrib" "amd64 arm64 source"
make_repo "$BASE/repos/src-only"          "main"         "source"

# ---------------------------------------------------------------- configs ---
gen_configs() {  # <path-base>
  local b=$1 d=$2
  emit_config "$d/complete.toml"                "$b/repos/main-src"         main "$b/arts/complete"
  emit_config "$d/fresh.toml"                   "$b/repos/main-src"         main "$b/arts/complete-2.0"
  emit_config "$d/arch-all.toml"                "$b/repos/main-src"         main "$b/arts/arch-all"
  emit_config "$d/source-only.toml"             "$b/repos/src-only"         main "$b/arts/source-only"
  emit_config "$d/binary-only.toml"             "$b/repos/main-src"         main "$b/arts/binary-only"
  emit_config "$d/missing-arm64.toml"           "$b/repos/main-src"         main "$b/arts/missing-arm64"
  emit_config "$d/nosrc-with-dsc.toml"          "$b/repos/main-nosrc"       main "$b/arts/complete"
  emit_config "$d/contrib.toml"                 "$b/repos/main-contrib-src" contrib "$b/arts/complete-2.0"
  emit_config "$d/main.toml"                    "$b/repos/main-contrib-src" main "$b/arts/complete-2.0"
  emit_config "$d/wrong-component.toml"         "$b/repos/main-contrib-src" contribx "$b/arts/complete-2.0"
  emit_config "$d/broken-checksum.toml"         "$b/repos/main-src"         main "$b/arts/broken-checksum"
  emit_config "$d/broken-no-dsc-priority.toml"  "$b/repos/main-src"         main "$b/arts/broken-no-dsc-priority"
  emit_config "$d/broken-no-deb-priority.toml"  "$b/repos/main-src"         main "$b/arts/broken-no-deb-priority"
  emit_config "$d/broken-version-mismatch.toml" "$b/repos/main-src"         main "$b/arts/broken-version-mismatch"
  emit_config "$d/broken-source-mismatch.toml"  "$b/repos/main-src"         main "$b/arts/broken-source-mismatch"
  emit_config "$d/broken-missing-file.toml"     "$b/repos/main-src"         main "$b/arts/broken-missing-file"
  emit_config "$d/broken-missing-binary.toml"   "$b/repos/main-src"         main "$b/arts/broken-missing-binary"
  emit_config "$d/broken-corrupt-deb.toml"      "$b/repos/main-src"         main "$b/arts/broken-corrupt-deb"
}
gen_configs "$GUEST_BASE" "$BASE/configs"
gen_configs "$HOST_BASE"  "$BASE/configs-host"

# ----------------------------------------------------------------- readme ---
cat > "$BASE/README.md" <<EOF
# rpp-release-tool fixtures

Generated by \`tests/make_fixtures.sh\`.

## Artifacts (\`arts/\`)

| dir | content | expected |
|---|---|---|
| complete | .dsc + amd64 + arm64 .deb | publishable |
| complete-2.0 | same, version 2.0-1 | publishable (fresh) |
| arch-all | .dsc + \`Architecture: all\` .deb | publishable, shown under \`all/\` |
| source-only | .dsc + tarballs | exit 2 unless \`--ack\` |
| binary-only | amd64 .deb only | exit 2 unless \`--ack\` |
| missing-arm64 | complete minus arm64 | blocked (missing_arch=error) |
| broken-checksum | corrupted orig tarball | blocked |
| broken-no-dsc-priority | .dsc without Priority | blocked |
| broken-no-deb-priority | .deb without Priority | blocked |
| broken-version-mismatch | dsc 1.8.4-1 vs deb 1.8.5-1 | blocked |
| broken-source-mismatch | deb Source: otherpkg | blocked |
| broken-missing-file | referenced tarball deleted | blocked |
| broken-missing-binary | dsc declares ccid-tools, no such deb | warning (not blocking) |
| broken-corrupt-deb | truncated .deb | blocked |

## Repositories (\`repos/\`)

| dir | Components | Architectures |
|---|---|---|
| main-src | main | amd64 arm64 source |
| main-nosrc | main | amd64 arm64 |
| main-contrib-src | main contrib | amd64 arm64 source |
| src-only | main | source |

Skeletons only. Create db/pool/dists inside the container with:

\`\`\`bash
for r in main-src main-nosrc main-contrib-src src-only; do
  docker exec rpp-trixie reprepro -b $GUEST_BASE/repos/\$r export
done
\`\`\`

## Configs

- \`configs/\` uses container paths (\`$GUEST_BASE/...\`)
- \`configs-host/\` uses host paths (\`$HOST_BASE/...\`)

Example:

\`\`\`bash
# container
docker exec -it rpp-trixie /work/build/rpp-release-tool --config $GUEST_BASE/configs/complete.toml
# host (no TUI)
./build/rpp-release-tool --no-tui --check-only --config $HOST_BASE/configs-host/complete.toml
\`\`\`

Signing key: ${SIGN:-<none: SignWith omitted>}
EOF

echo "fixtures generated in $BASE"
find "$BASE" -maxdepth 2 -type d | sort | sed 's/^/  /'
