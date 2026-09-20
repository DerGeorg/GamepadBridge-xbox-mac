#!/usr/bin/env bash
#
# Fetches the Sparkle framework used for in-app updates.
#
# Sparkle is MIT licensed, which GPL-2.0 permits. It is not vendored into the
# repository because it is a 16 MB binary; it is downloaded and checked
# against a pinned hash instead.
#
# That pin is the only integrity guarantee there is: the framework inside the
# archive is ad-hoc signed, with no team identifier, so there is nothing to
# verify it against. It gets signed with our own Developer ID when it is
# embedded. Bumping SPARKLE_VERSION means updating SPARKLE_SHA256 too, and
# the new hash should be taken from a download you have reason to trust.
#
set -euo pipefail

SPARKLE_VERSION="2.10.0"
SPARKLE_SHA256="c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/third_party/sparkle"
URL="https://github.com/sparkle-project/Sparkle/releases/download/$SPARKLE_VERSION/Sparkle-$SPARKLE_VERSION.tar.xz"

die() { printf '\nERROR: %s\n' "$1" >&2; exit 1; }

if [ -d "$DEST/Sparkle.framework" ] && [ "${1:-}" != "--force" ]; then
    echo "Sparkle $SPARKLE_VERSION already in $DEST (--force to refetch)"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading Sparkle $SPARKLE_VERSION..."
curl -fsSL --retry 2 -o "$TMP/sparkle.tar.xz" "$URL" \
    || die "download failed"

ACTUAL="$(shasum -a 256 "$TMP/sparkle.tar.xz" | cut -d' ' -f1)"

[ "$ACTUAL" = "$SPARKLE_SHA256" ] || die "checksum mismatch - refusing to use it
  expected $SPARKLE_SHA256
  got      $ACTUAL"

rm -rf "$DEST"
mkdir -p "$DEST"

# ditto rather than tar -C: the framework's Versions/Current symlinks have to
# survive, or the bundle is unsignable.
tar -xJf "$TMP/sparkle.tar.xz" -C "$TMP" ./Sparkle.framework ./bin ./LICENSE
ditto "$TMP/Sparkle.framework" "$DEST/Sparkle.framework"
ditto "$TMP/bin" "$DEST/bin"
cp "$TMP/LICENSE" "$DEST/LICENSE"

echo "Sparkle $SPARKLE_VERSION -> $DEST"
echo
echo "Next, once per project: create the update signing key."
echo "  $DEST/bin/generate_keys"
echo "It stores the private key in your keychain and prints the public key,"
echo "which goes into XOW_SPARKLE_PUBLIC_KEY."
