#!/bin/bash
# The corresponding source of the PS4 package, to attach to a release next to it:
# - Playfin's own source at HEAD;
# - for every recipe of scripts/ps4: the recipe, its patches and the upstream sources it downloads;
# - the build image's libraries and toolchain files that end up in the package (FriBidi, zlib, libsamplerate,
#   the OpenOrbis toolchain's crt and link.x) with the PacBrew recipes they were built with.
# Every download with a known checksum is checked; SHA256SUMS lists every file of the archive.
# Usage: scripts/ps4/source-archive.sh [output.tar]   (default: dist/playfin-<version>-sources.tar)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(sed -n 's/^project(Playfin VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
NAME="playfin-$VERSION-sources"
OUT="${1:-$ROOT/dist/$NAME.tar}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
DEST="$WORK/$NAME"
mkdir -p "$DEST/recipes" "$DEST/image" "$(dirname "$OUT")"

# fetch URL FILE SHA256 (SKIP: no published checksum)
fetch() {
    echo "  $(basename "$2")"
    curl -fsSL --retry 3 -o "$2" "$1"
    if [ "$3" != SKIP ]; then
        echo "$3  $2" | sha256sum -c --quiet -
    fi
}

echo "Playfin $VERSION"
git -C "$ROOT" archive --prefix="playfin-$VERSION/" -o "$DEST/playfin-$VERSION.tar.gz" HEAD

echo "Recipes of scripts/ps4"
for recipe in "$ROOT"/scripts/ps4/*/PKGBUILD; do
    dir="$(dirname "$recipe")"
    name="$(basename "$dir")"
    mkdir -p "$DEST/recipes/$name"
    cp "$dir"/* "$DEST/recipes/$name/"
    # the recipe's source and sha256sums arrays, one "url<TAB>sum" per line
    entries="$(cd "$dir" && set +u && source ./PKGBUILD && for i in "${!source[@]}"; do
        printf '%s\t%s\n' "${source[$i]}" "${sha256sums[$i]:-SKIP}"; done)"
    while IFS=$'\t' read -r src sum; do
        case "$src" in
        *::*) file="${src%%::*}" url="${src#*::}" ;;
        *://*) file="$(basename "$src")" url="$src" ;;
        *) continue ;;  # a local file (patch, cross file): copied with the recipe
        esac
        fetch "$url" "$DEST/recipes/$name/$file" "$sum"
    done <<< "$entries"
done

echo "Build image (xfangfang/pacbrew:250221): libraries and toolchain files in the package"
PORTLIBS=d6a523a5a4a7b86846d91134d40a34357721a273
TOOLCHAIN_RECIPES=16f27e38760ea21eaf1187d0edbf4342c4a44021
TOOLCHAIN=cd480230b946239f758333ef0dff40d67c5f0c20
while read -r url file sum; do
    fetch "$url" "$DEST/image/$file" "$sum"
done <<EOF
https://github.com/fribidi/fribidi/releases/download/v1.0.4/fribidi-1.0.4.tar.bz2 fribidi-1.0.4.tar.bz2 94bdfe553e004d8bd095b109e182682311dd511740d5083326d1582f1df237be
https://zlib.net/fossils/zlib-1.3.1.tar.gz zlib-1.3.1.tar.gz 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
https://github.com/libsndfile/libsamplerate/releases/download/0.1.9/libsamplerate-0.1.9.tar.gz libsamplerate-0.1.9.tar.gz 0a7eb168e2f21353fb6d84da152e4512126f7dc48ccb0be80578c565413444c1
https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/archive/$TOOLCHAIN.zip OpenOrbis-PS4-Toolchain-$TOOLCHAIN.zip b77f9ba633cfe1cc829220321ea808b8990f4a625ccbad75b4f8f7686a9d8161
https://github.com/PacBrew/ps4-openorbis-portlibs/archive/$PORTLIBS.tar.gz pacbrew-ps4-openorbis-portlibs-$PORTLIBS.tar.gz 662559c4771c674e99ae37895a1c6ee64ff1aabf15ad363412f166d44a25b0f8
https://github.com/PacBrew/ps4-openorbis/archive/$TOOLCHAIN_RECIPES.tar.gz pacbrew-ps4-openorbis-$TOOLCHAIN_RECIPES.tar.gz f2b8d148def25ea75ea01c8e2a38af498e4d89f9a0e10c5fb8a956620f194c96
EOF

cat > "$DEST/README.txt" <<EOF
Corresponding source of Playfin $VERSION for the PS4 (IV0001-PLFN00000_00-PLFN000000100000.pkg).

playfin-$VERSION.tar.gz   Playfin itself (GPL-3.0-or-later).
recipes/<name>/            How each library linked into the package was built (pacbrew PKGBUILD, patches,
                           cross files) and the upstream sources it downloads. Built with
                           scripts/ps4/build-deps.sh in the Docker image xfangfang/pacbrew:250221.
image/                     Libraries and toolchain files taken from that image: FriBidi 1.0.4 (LGPL-2.1),
                           zlib 1.3.1, libsamplerate 0.1.9, the OpenOrbis PS4 Toolchain at $TOOLCHAIN
                           (GPL-3.0: crt and link.x), and the PacBrew recipes they were built with
                           (ps4-openorbis-portlibs, ps4-openorbis).

The licences are listed in THIRD_PARTY_LICENSES and resources/licenses/ of Playfin's source.
SHA256SUMS lists every file of this archive.
EOF

(cd "$DEST" && find . -type f ! -name SHA256SUMS -printf '%P\n' | sort | xargs sha256sum > SHA256SUMS)
tar -cf "$OUT" -C "$WORK" "$NAME"
echo "$OUT ($(du -h "$OUT" | cut -f1))"
