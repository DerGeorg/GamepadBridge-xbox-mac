#!/usr/bin/env bash
#
# Builds the libusb that the release links statically.
#
# Homebrew's libusb cannot be used for a release: its archive is built for
# the macOS it was bottled on (26 for 1.0.30) and only for the machine's own
# architecture. Linked into the app, it lifts the app's minimum macOS to that
# version and leaves Intel Macs out. This builds the same release from source
# for the app's deployment target, for Apple silicon and Intel at once.
#
# The pin below is the only integrity guarantee. It matches the checksum in
# Homebrew's libusb formula for the same release tarball. Bumping
# LIBUSB_VERSION means updating LIBUSB_SHA256 too, taken from a source you
# have reason to trust.
#
set -euo pipefail

LIBUSB_VERSION="1.0.30"
LIBUSB_SHA256="fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/third_party/libusb"
URL="https://github.com/libusb/libusb/releases/download/v$LIBUSB_VERSION/libusb-$LIBUSB_VERSION.tar.bz2"

die() { printf '\nERROR: %s\n' "$1" >&2; exit 1; }

# One place for the minimum macOS: CMakeLists.txt.
TARGET="$(sed -n 's/^set(CMAKE_OSX_DEPLOYMENT_TARGET "\([^"]*\)".*/\1/p' \
    "$ROOT/CMakeLists.txt")"
[ -n "$TARGET" ] || die "no CMAKE_OSX_DEPLOYMENT_TARGET in CMakeLists.txt"

STAMP="$DEST/.built-for"
WANT="$LIBUSB_VERSION macos-$TARGET arm64+x86_64"

if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ] \
    && [ "${1:-}" != "--force" ]; then
    echo "libusb $LIBUSB_VERSION already in $DEST (--force to rebuild)"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading libusb $LIBUSB_VERSION..."
curl -fsSL --retry 2 -o "$TMP/libusb.tar.bz2" "$URL" \
    || die "download failed"

ACTUAL="$(shasum -a 256 "$TMP/libusb.tar.bz2" | cut -d' ' -f1)"

[ "$ACTUAL" = "$LIBUSB_SHA256" ] || die "checksum mismatch - refusing to use it
  expected $LIBUSB_SHA256
  got      $ACTUAL"

tar -xjf "$TMP/libusb.tar.bz2" -C "$TMP"

echo "Building for macOS $TARGET, arm64 and x86_64..."

# The release tarball ships a generated configure script, so no autotools.
# Dependency tracking has to be off for a build with two -arch flags.
(
    cd "$TMP/libusb-$LIBUSB_VERSION"

    CFLAGS="-O2 -arch arm64 -arch x86_64 -mmacosx-version-min=$TARGET" \
    LDFLAGS="-arch arm64 -arch x86_64 -mmacosx-version-min=$TARGET" \
        ./configure --prefix="$DEST" --disable-shared --enable-static \
        --disable-dependency-tracking > "$TMP/configure.log" 2>&1 \
        || { tail -30 "$TMP/configure.log"; exit 1; }

    make -j"$(sysctl -n hw.ncpu)" > "$TMP/make.log" 2>&1 \
        || { tail -30 "$TMP/make.log"; exit 1; }

    rm -rf "$DEST"
    make install > /dev/null
) || die "libusb build failed"

cp "$TMP/libusb-$LIBUSB_VERSION/COPYING" "$DEST/COPYING"
echo "$WANT" > "$STAMP"

echo "libusb $LIBUSB_VERSION -> $DEST ($(lipo -archs "$DEST/lib/libusb-1.0.a"))"
