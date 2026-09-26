#!/usr/bin/env bash
#
# Embeds Sparkle into the app bundle and signs it with the identity Xcode is
# about to sign the app with.
#
# Runs as the CMake POST_BUILD step, an Xcode Run Script phase, so Xcode's
# build settings (the signing identity among them) are in the environment.
#
# Without this the framework keeps Sparkle's ad-hoc signature, which has no
# team identifier. The app is built with the hardened runtime, whose library
# validation refuses to load code from a different team, so a development build
# died in dyld before main() ever ran:
#
#   mapping process and mapped file (non-platform) have different Team IDs
#
# Release builds are signed again by scripts/release.sh with the Developer ID,
# the hardened runtime and a secure timestamp, as notarization requires. This
# step signs without a timestamp so that a build needs no network.
#
#   embed-sparkle.sh <Sparkle.framework> <App.app/Contents>
#
set -euo pipefail

SOURCE="$1"
CONTENTS="$2"
DEST="$CONTENTS/Frameworks/Sparkle.framework"

mkdir -p "$CONTENTS/Frameworks"

# A fresh copy each time, so an earlier build's signatures cannot linger.
rm -rf "$DEST"

# ditto, not cp: the Versions/Current symlinks have to survive.
ditto "$SOURCE" "$DEST"

IDENTITY="${EXPANDED_CODE_SIGN_IDENTITY:-}"

if [ -z "$IDENTITY" ]; then
    echo "embed-sparkle: no signing identity in this build, Sparkle stays ad-hoc"
    exit 0
fi

CURRENT="$DEST/Versions/Current"

# Inside out: a bundle's signature records what it contains, so everything
# nested has to be signed before the thing that contains it.
for nested in \
    XPCServices/Downloader.xpc \
    XPCServices/Installer.xpc \
    Updater.app \
    Autoupdate
do
    [ -e "$CURRENT/$nested" ] || continue

    codesign --force --options runtime --timestamp=none \
        --sign "$IDENTITY" "$CURRENT/$nested"
done

codesign --force --options runtime --timestamp=none --sign "$IDENTITY" "$DEST"

# Seal the app again over the framework as it now is. Relying on Xcode to do it
# does not work: in an incremental build where nothing was relinked, Xcode skips
# its own signing step entirely, while this step still runs — and the app's
# existing seal then describes the framework this script just replaced:
#
#   nested code is modified or invalid
#
# --preserve-metadata keeps the entitlements Xcode put in (application
# identifier, team, the virtual HID entitlement) instead of replacing them.
# If the app is not signed yet, Xcode signs it afterwards anyway.
APP="$(dirname "$CONTENTS")"

codesign --force --timestamp=none --sign "$IDENTITY" \
    --preserve-metadata=entitlements,requirements,flags "$APP"
