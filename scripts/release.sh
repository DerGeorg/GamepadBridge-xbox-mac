#!/usr/bin/env bash
#
# Builds, signs, notarizes and packages GamepadBridge for distribution.
#
# Everything here is one command because the steps are easy to get subtly
# wrong on their own — a missing hardened runtime, an unstapled app inside a
# stapled disk image — and each mistake only shows up on someone else's Mac,
# as a Gatekeeper dialog rather than an error you can read.
#
# Prerequisites, all one-time:
#   1. A "Developer ID Application" certificate in the keychain.
#   2. A Developer ID provisioning profile for the bundle ID carrying the
#      com.apple.developer.hid.virtual.device entitlement, installed in
#      ~/Library/Developer/Xcode/UserData/Provisioning Profiles/
#   3. Notarization credentials stored in the keychain, which you create
#      yourself so no password passes through this script:
#
#        xcrun notarytool store-credentials gamepadbridge \
#            --apple-id you@example.com --team-id 6WKZ2V3YFU
#
# Usage:
#   scripts/release.sh --keychain-profile gamepadbridge [--version 1.0.0]
#   scripts/release.sh --skip-notarize            # local dry run
#
set -euo pipefail

TEAM_ID="6WKZ2V3YFU"
BUNDLE_ID="at.dergeorg.gamepadbridge"
IDENTITY="Developer ID Application"
PROFILE_NAME="GamepadBridge Developer ID"
VERSION=""
KEYCHAIN_PROFILE=""
SKIP_NOTARIZE=0

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-release"
DIST="$ROOT/dist"

die() { printf '\nERROR: %s\n' "$1" >&2; exit 1; }
step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --keychain-profile) KEYCHAIN_PROFILE="$2"; shift 2 ;;
        --version)          VERSION="$2"; shift 2 ;;
        --profile-name)     PROFILE_NAME="$2"; shift 2 ;;
        --skip-notarize)    SKIP_NOTARIZE=1; shift ;;
        -h|--help)          sed -n '2,30p' "$0"; exit 0 ;;
        *)                  die "unknown argument: $1" ;;
    esac
done

if [ -z "$VERSION" ]; then
    VERSION="$(sed -n 's/^set(XOW_RELEASE_VERSION "\([^"]*\)".*/\1/p' \
        "$ROOT/CMakeLists.txt")"
fi

[ -n "$VERSION" ] || die "could not determine the version"

if [ "$SKIP_NOTARIZE" -eq 0 ] && [ -z "$KEYCHAIN_PROFILE" ]; then
    die "--keychain-profile is required (or pass --skip-notarize for a dry run)"
fi

APP="$BUILD/Release/GamepadBridge.app"
DMG="$DIST/GamepadBridge-$VERSION.dmg"

# ---------------------------------------------------------------------------
step "Checking prerequisites"
# ---------------------------------------------------------------------------
security find-identity -v -p codesigning \
    | grep -q "$IDENTITY: .*($TEAM_ID)" \
    || die "no '$IDENTITY' certificate for team $TEAM_ID in the keychain"

PROFILE_DIR="$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"
found=""

for candidate in "$PROFILE_DIR"/*.provisionprofile; do
    [ -f "$candidate" ] || continue

    plist="$(security cms -D -i "$candidate" 2>/dev/null || true)"

    # A Developer ID profile is the one that provisions all devices; a
    # development profile is tied to the Macs you registered.
    if printf '%s' "$plist" | grep -q "hid.virtual.device" \
        && printf '%s' "$plist" | grep -q "ProvisionsAllDevices"; then
        found="$(printf '%s' "$plist" | plutil -extract Name raw - 2>/dev/null)"
        break
    fi
done

[ -n "$found" ] || die "no Developer ID provisioning profile with the \
hid.virtual.device entitlement found in:
  $PROFILE_DIR
Download it from developer.apple.com and double-click it to install."

PROFILE_NAME="$found"
echo "certificate: $IDENTITY ($TEAM_ID)"
echo "profile:     $PROFILE_NAME"
echo "version:     $VERSION"

# ---------------------------------------------------------------------------
step "Building"
# ---------------------------------------------------------------------------
rm -rf "$BUILD" "$DIST"
mkdir -p "$DIST"

cmake -G Xcode \
    -DXOW_BACKEND=corehid \
    -DXOW_MACOS_APP=ON \
    -DXOW_TEAM_ID="$TEAM_ID" \
    -DXOW_BUNDLE_ID="$BUNDLE_ID" \
    -DXOW_RELEASE_VERSION="$VERSION" \
    -DXOW_SIGN_STYLE=Manual \
    -DXOW_CODESIGN_IDENTITY="$IDENTITY" \
    -DXOW_PROVISIONING_PROFILE="$PROFILE_NAME" \
    -DXOW_STATIC_LIBUSB=ON \
    -S "$ROOT" -B "$BUILD" > /dev/null

xcodebuild -project "$BUILD/gamepadbridge.xcodeproj" \
    -target gamepadbridge -configuration Release \
    -allowProvisioningUpdates > "$BUILD/build.log" 2>&1 \
    || { tail -40 "$BUILD/build.log"; die "build failed"; }

[ -d "$APP" ] || die "expected $APP"

# ---------------------------------------------------------------------------
step "Verifying the signature"
# ---------------------------------------------------------------------------
codesign --verify --strict --verbose=2 "$APP" 2>&1 | sed 's/^/  /'

codesign -d --entitlements - "$APP" 2>&1 \
    | grep -q "com.apple.developer.hid.virtual.device" \
    || die "the built app is missing the hid.virtual.device entitlement"

codesign -d --verbose=2 "$APP" 2>&1 | grep -q "flags=.*runtime" \
    || die "the hardened runtime is not enabled - notarization would reject it"

if otool -L "$APP/Contents/MacOS/GamepadBridge" | grep -q "/opt/homebrew"; then
    die "the app still links against Homebrew - it would not run elsewhere"
fi

echo "  entitlement, hardened runtime and dependencies all check out"

# ---------------------------------------------------------------------------
step "Notarizing the app"
# ---------------------------------------------------------------------------
if [ "$SKIP_NOTARIZE" -eq 1 ]; then
    echo "  skipped (--skip-notarize)"
else
    # The app is notarized and stapled first, and only then wrapped in the
    # disk image: Homebrew installs the app out of the image, so the ticket
    # has to travel with the app rather than only with the image.
    ditto -c -k --keepParent "$APP" "$BUILD/GamepadBridge.zip"

    xcrun notarytool submit "$BUILD/GamepadBridge.zip" \
        --keychain-profile "$KEYCHAIN_PROFILE" --wait \
        || die "notarization of the app failed"

    xcrun stapler staple "$APP"
fi

# ---------------------------------------------------------------------------
step "Building the disk image"
# ---------------------------------------------------------------------------
STAGE="$BUILD/dmg"
rm -rf "$STAGE"
mkdir -p "$STAGE"

cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"

hdiutil create -volname "GamepadBridge" -srcfolder "$STAGE" \
    -ov -format UDZO "$DMG" > /dev/null

if [ "$SKIP_NOTARIZE" -eq 0 ]; then
    step "Notarizing the disk image"

    xcrun notarytool submit "$DMG" \
        --keychain-profile "$KEYCHAIN_PROFILE" --wait \
        || die "notarization of the disk image failed"

    xcrun stapler staple "$DMG"

    step "Verifying as Gatekeeper sees it"
    spctl -a -t open --context context:primary-signature -v "$DMG" 2>&1 \
        | sed 's/^/  /'
fi

# ---------------------------------------------------------------------------
step "Done"
# ---------------------------------------------------------------------------
SHA="$(shasum -a 256 "$DMG" | cut -d' ' -f1)"

echo "  $DMG"
echo "  sha256: $SHA"
echo
echo "Update the Homebrew cask with:"
echo "  version \"$VERSION\""
echo "  sha256 \"$SHA\""
