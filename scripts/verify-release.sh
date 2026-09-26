#!/usr/bin/env bash
#
# Checks a release before anything about it is published.
#
# The release pipeline runs this first. It needs no secrets and no macOS, so
# it runs on the Linux runners — and locally, which is how it is tested.
#
# Every check here is a failure that would otherwise surface silently, on
# someone else's Mac:
#
#   disk image       GitLab answers some URLs with an HTML page; a release
#                    carrying one fails as a "damaged" download
#   Sparkle length   Sparkle rejects an update whose size does not match
#   Sparkle signature  and one whose EdDSA signature does not verify — for
#                    every installed copy at once, without telling anyone
#   cask sha256      Homebrew fails with a checksum error that reads like a
#                    corrupted download
#
#   scripts/verify-release.sh [pending.json]
#
# The pending file is what scripts/release.sh --publish writes on the Mac:
# version, download URL, length, EdDSA signature and sha256.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PENDING="${1:-$ROOT/packaging/pending-release.json}"
PUBLIC_KEY_FILE="$ROOT/packaging/sparkle_public_key.txt"

fail() { printf '  \342\234\227 %s\n' "$1" >&2; exit 1; }
pass() { printf '  \342\234\223 %s\n' "$1"; }

# macOS ships LibreSSL, which cannot verify Ed25519 over raw input.
OPENSSL="${OPENSSL:-openssl}"
if ! "$OPENSSL" version 2>/dev/null | grep -q "^OpenSSL 3"; then
    [ -x /opt/homebrew/opt/openssl@3/bin/openssl ] \
        && OPENSSL=/opt/homebrew/opt/openssl@3/bin/openssl \
        || fail "needs OpenSSL 3 for Ed25519 (brew install openssl@3)"
fi

[ -f "$PENDING" ] || fail "no pending release at $PENDING"

field() {
    python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' \
        "$PENDING" "$1"
}

VERSION="$(field version)"
URL="$(field url)"
LENGTH="$(field length)"
SIGNATURE="$(field edSignature)"
SHA256="$(field sha256)"

echo "Verifying GamepadBridge $VERSION"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -f "$ROOT/packaging/release-notes/$VERSION.md" ] \
    && pass "release notes exist" \
    || fail "packaging/release-notes/$VERSION.md is missing"

EXPECTED_URL="https://gitlab.dergeorg.at/api/v4/projects/mac%2Fgamepadbridge/packages/generic/gamepadbridge/$VERSION/GamepadBridge.dmg"
[ "$URL" = "$EXPECTED_URL" ] \
    && pass "download URL points at the package registry" \
    || fail "unexpected download URL: $URL"

curl --fail --silent --show-error --location -o "$WORK/app.dmg" "$URL" \
    || fail "cannot download $URL"

# A UDIF disk image ends in a 512-byte trailer that starts with "koly". An
# HTML page does not, however plausible its size.
[ "$(tail -c 512 "$WORK/app.dmg" | head -c 4)" = "koly" ] \
    && pass "the download is a disk image" \
    || fail "the download is not a disk image (first bytes: $(head -c 40 "$WORK/app.dmg" | tr -cd '[:print:]'))"

ACTUAL_LENGTH="$(wc -c < "$WORK/app.dmg" | tr -d ' ')"
[ "$ACTUAL_LENGTH" = "$LENGTH" ] \
    && pass "length matches what Sparkle will be told ($LENGTH bytes)" \
    || fail "length is $ACTUAL_LENGTH, Sparkle would be told $LENGTH"

ACTUAL_SHA="$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$WORK/app.dmg")"
[ "$ACTUAL_SHA" = "$SHA256" ] \
    && pass "sha256 matches what the cask will say" \
    || fail "sha256 is $ACTUAL_SHA, the cask would say $SHA256"

# Sparkle's public key is 32 raw bytes; OpenSSL wants it wrapped as an
# Ed25519 SubjectPublicKeyInfo. The prefix is that structure's fixed header.
python3 - "$PUBLIC_KEY_FILE" "$WORK/key.pem" "$SIGNATURE" "$WORK/sig.bin" <<'PY'
import base64, sys
raw = base64.b64decode(open(sys.argv[1]).read().strip())
assert len(raw) == 32, "Sparkle public key must be 32 bytes"
der = bytes.fromhex("302a300506032b6570032100") + raw
pem = "-----BEGIN PUBLIC KEY-----\n" + base64.b64encode(der).decode() + "\n-----END PUBLIC KEY-----\n"
open(sys.argv[2], "w").write(pem)
open(sys.argv[4], "wb").write(base64.b64decode(sys.argv[3]))
PY

"$OPENSSL" pkeyutl -verify -pubin -inkey "$WORK/key.pem" -rawin \
    -in "$WORK/app.dmg" -sigfile "$WORK/sig.bin" > /dev/null 2>&1 \
    && pass "Sparkle signature verifies against the public key in the repository" \
    || fail "Sparkle signature does NOT verify - every installed copy would reject this update"

echo "All checks passed."
