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
#   scripts/release.sh --keychain-profile gamepadbridge --publish
#   scripts/release.sh --keychain-profile gamepadbridge   # build, don't publish
#   scripts/release.sh --skip-notarize                    # local dry run
#   scripts/release.sh --skip-build ...                   # reuse the build
#
# --publish is the whole release from this side: it uploads the disk image,
# writes packaging/pending-release.json and pushes a release commit. The
# pipeline then checks everything again (release:verify) and publishes on one
# click (release:publish): GitLab release, appcast, Homebrew tap. GitHub
# follows through the mirror.
#
# This script deliberately does not touch appcast.xml. The moment that file
# reaches main, every installed copy is offered the update — so it is written
# by the pipeline, after the checks, and not here.
#
# Before a release: set XOW_RELEASE_VERSION in CMakeLists.txt and write
# packaging/release-notes/<version>.md. Those two files, and nothing else, may
# be uncommitted; they go into the release commit.
#
# Re-running is cheap: an app that is already notarized and stapled is kept
# as it is. Only the step that actually failed repeats.
#
set -euo pipefail

TEAM_ID="6WKZ2V3YFU"
BUNDLE_ID="at.dergeorg.gamepadbridge"
IDENTITY="Developer ID Application"
PROFILE_NAME="GamepadBridge Developer ID"
VERSION=""
KEYCHAIN_PROFILE=""
SKIP_NOTARIZE=0
SKIP_BUILD=0
PUBLISH=0

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-release"
DIST="$ROOT/dist"
SPARKLE="$ROOT/third_party/sparkle"
SPARKLE_KEY_FILE="$ROOT/packaging/sparkle_public_key.txt"

# Where --publish uploads the disk image, and what the appcast will point at.
# scripts/verify-release.sh checks the two agree before anything is published:
# otherwise the appcast points at a file that is not there and updates fail
# for everyone at once.
DOWNLOAD_BASE="https://gitlab.dergeorg.at/api/v4/projects/mac%2Fgamepadbridge/packages/generic/gamepadbridge"

die() { printf '\nERROR: %s\n' "$1" >&2; exit 1; }
step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --keychain-profile) KEYCHAIN_PROFILE="$2"; shift 2 ;;
        --version)          VERSION="$2"; shift 2 ;;
        --profile-name)     PROFILE_NAME="$2"; shift 2 ;;
        --skip-notarize)    SKIP_NOTARIZE=1; shift ;;
        --skip-build)       SKIP_BUILD=1; shift ;;
        --publish)          PUBLISH=1; shift ;;
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
if [ "$PUBLISH" -eq 1 ]; then
    [ "$SKIP_NOTARIZE" -eq 0 ] || die "--publish needs a notarized build"

    CMAKE_VERSION="$(sed -n 's/^set(XOW_RELEASE_VERSION "\([^"]*\)".*/\1/p' \
        "$ROOT/CMakeLists.txt")"
    [ "$VERSION" = "$CMAKE_VERSION" ] || die "--publish takes the version from \
CMakeLists.txt ($CMAKE_VERSION), not --version $VERSION, so the repository \
cannot disagree with the app"

    [ -f "$ROOT/packaging/release-notes/$VERSION.md" ] \
        || die "write packaging/release-notes/$VERSION.md first"

    [ "$(git -C "$ROOT" symbolic-ref --short HEAD)" = "main" ] \
        || die "releases are made from main"

    git -C "$ROOT" fetch -q origin main
    [ "$(git -C "$ROOT" rev-parse HEAD)" = "$(git -C "$ROOT" rev-parse origin/main)" ] \
        || die "main is not in step with origin/main - push or pull first, so \
the release commit contains exactly what was built"

    # Only the two files a release edits may be uncommitted. Anything else
    # would be swept into the release commit — which is how a disk image once
    # ended up in this repository's history.
    unexpected="$(git -C "$ROOT" status --porcelain \
        | grep -vE "^.. (CMakeLists.txt|packaging/release-notes/$VERSION.md)$" || true)"
    [ -z "$unexpected" ] || die "uncommitted changes that do not belong in a release:
$unexpected"

    if git -C "$ROOT" ls-remote --exit-code --tags origin "refs/tags/v$VERSION" \
        > /dev/null 2>&1; then
        die "v$VERSION is already released - bump XOW_RELEASE_VERSION"
    fi

    # Read just the one line rather than sourcing the file: sourcing executes
    # everything in it, and one value with an unquoted $ aborts under set -u.
    GITLAB_TOKEN="${GITLAB_TOKEN:-$(sed -n \
        's/^[[:space:]]*\(export[[:space:]][[:space:]]*\)\{0,1\}GITLAB_TOKEN=//p' \
        "$HOME/.homelab/config" 2>/dev/null | tail -1 | sed "s/^[\"']//; s/[\"']$//")}"
    [ -n "$GITLAB_TOKEN" ] || die "no GITLAB_TOKEN to upload the disk image with"

    echo "publishing:  yes, as $VERSION"
fi

# Every check below captures its output first rather than piping into
# `grep -q`. grep exits at the matching line and closes the pipe, the writer
# takes a SIGPIPE, and `set -o pipefail` turns a *successful* match into a
# failed pipeline — which is exactly how the hardened runtime check managed
# to report the opposite of the truth.
identities="$(security find-identity -v -p codesigning 2>&1 || true)"

grep -q "$IDENTITY: .*($TEAM_ID)" <<< "$identities" \
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
[ -d "$SPARKLE/Sparkle.framework" ] \
    || die "Sparkle is missing - run scripts/get-sparkle.sh"

[ -f "$SPARKLE_KEY_FILE" ] || die "no Sparkle public key at
  $SPARKLE_KEY_FILE
Run $SPARKLE/bin/generate_keys once. It puts the private key in your
keychain and prints the public one; save that line to the file above."

SPARKLE_PUBLIC_KEY="$(tr -d '[:space:]' < "$SPARKLE_KEY_FILE")"

echo "certificate: $IDENTITY ($TEAM_ID)"
echo "profile:     $PROFILE_NAME"
echo "version:     $VERSION"

# ---------------------------------------------------------------------------
step "Building"
# ---------------------------------------------------------------------------
mkdir -p "$DIST"

if [ "$SKIP_BUILD" -eq 1 ]; then
    [ -d "$APP" ] || die "--skip-build was given but $APP does not exist"

    echo "  reusing the existing build"
else

rm -rf "$BUILD"

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
    -DXOW_SPARKLE=ON \
    -DXOW_SPARKLE_PUBLIC_KEY="$SPARKLE_PUBLIC_KEY" \
    -S "$ROOT" -B "$BUILD" > /dev/null

xcodebuild -project "$BUILD/gamepadbridge.xcodeproj" \
    -target gamepadbridge -configuration Release \
    -allowProvisioningUpdates > "$BUILD/build.log" 2>&1 \
    || { tail -40 "$BUILD/build.log"; die "build failed"; }

[ -d "$APP" ] || die "expected $APP"

fi

# ---------------------------------------------------------------------------
step "Signing the embedded framework"
# ---------------------------------------------------------------------------
SPARKLE_FW="$APP/Contents/Frameworks/Sparkle.framework"
CURRENT="$SPARKLE_FW/Versions/Current"

[ -d "$SPARKLE_FW" ] || die "Sparkle.framework was not embedded in the bundle"

# Sparkle ships ad-hoc signed, with no team identifier, so every part of it
# has to be signed again with our own identity. Inside out: a bundle's
# signature covers what it contains, so anything nested must be signed first
# or signing the container invalidates it immediately.
for nested in \
    "$CURRENT/XPCServices/Downloader.xpc" \
    "$CURRENT/XPCServices/Installer.xpc" \
    "$CURRENT/Updater.app" \
    "$CURRENT/Autoupdate"
do
    [ -e "$nested" ] || continue

    codesign --force --options runtime --timestamp \
        --sign "$IDENTITY" "$nested" 2>&1 | sed 's/^/  /'
done

codesign --force --options runtime --timestamp \
    --sign "$IDENTITY" "$SPARKLE_FW" 2>&1 | sed 's/^/  /'

# Re-signing the framework changed its code hash, and the app's seal records
# the hash of everything nested in it — so the app is sealed again over the
# framework as it now is.
codesign --force --options runtime --timestamp \
    --entitlements "$ROOT/packaging/gamepadbridge.entitlements" \
    --sign "$IDENTITY" "$APP" 2>&1 | sed 's/^/  /'

# ---------------------------------------------------------------------------
step "Verifying the signature"
# ---------------------------------------------------------------------------
codesign --verify --strict --verbose=2 "$APP" 2>&1 | sed 's/^/  /'

entitlements="$(codesign -d --entitlements - "$APP" 2>&1 || true)"

grep -q "com.apple.developer.hid.virtual.device" <<< "$entitlements" \
    || die "the built app is missing the hid.virtual.device entitlement"

# Xcode injects this when signing like a development build. It would let
# anything attach a debugger to the shipped app, and notarization says no.
if grep -q "com.apple.security.get-task-allow" <<< "$entitlements"; then
    die "the app carries get-task-allow - set CODE_SIGN_INJECT_BASE_ENTITLEMENTS=NO"
fi

signature="$(codesign -d --verbose=2 "$APP" 2>&1 || true)"

grep -q "flags=.*runtime" <<< "$signature" \
    || die "the hardened runtime is not enabled - notarization would reject it"

# Without a secure timestamp the signature cannot be verified once the
# certificate expires, so notarization refuses it.
grep -q "^Timestamp=" <<< "$signature" \
    || die "the signature has no secure timestamp - sign with --timestamp"

libraries="$(otool -L "$APP/Contents/MacOS/GamepadBridge" 2>&1 || true)"

if grep -q "/opt/homebrew" <<< "$libraries"; then
    die "the app still links against Homebrew - it would not run elsewhere"
fi

echo "  entitlements, hardened runtime, timestamp and dependencies all check out"

# ---------------------------------------------------------------------------
step "Notarizing the app"
# ---------------------------------------------------------------------------
if [ "$SKIP_NOTARIZE" -eq 1 ]; then
    echo "  skipped (--skip-notarize)"

elif xcrun stapler validate "$APP" > /dev/null 2>&1; then
    # Already notarized and stapled by an earlier run that failed later on.
    # Apple's notary service is slow enough that repeating this for nothing
    # is the difference between a retry and starting over.
    echo "  already notarized and stapled - keeping it"

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

# Sign the image itself, not only the app inside it. Notarizing an unsigned
# image still yields a valid stapled ticket, but Gatekeeper's assessment of
# the image finds no signature to evaluate and rejects it with "no usable
# signature" — while the app within is perfectly fine, which makes it a
# confusing failure to read.
codesign --sign "$IDENTITY" --timestamp "$DMG"

if [ "$SKIP_NOTARIZE" -eq 0 ]; then
    step "Notarizing the disk image"

    xcrun notarytool submit "$DMG" \
        --keychain-profile "$KEYCHAIN_PROFILE" --wait \
        || die "notarization of the disk image failed"

    xcrun stapler staple "$DMG"

    step "Signing the update for Sparkle"

    SIGNED="$("$SPARKLE/bin/sign_update" "$DMG")"

    ED_SIGNATURE="$(sed -n 's/.*edSignature="\([^"]*\)".*/\1/p' <<< "$SIGNED")"
    LENGTH="$(sed -n 's/.*length="\([^"]*\)".*/\1/p' <<< "$SIGNED")"

    [ -n "$ED_SIGNATURE" ] || die "sign_update produced no signature - is the \
private key still in your keychain?"

    echo "  length $LENGTH, signature ${ED_SIGNATURE:0:16}…"

    step "Verifying as Gatekeeper sees it"

    assessment="$(spctl -a -t open --context context:primary-signature -v \
        "$DMG" 2>&1 || true)"

    sed 's/^/  /' <<< "$assessment"

    # Report a rejection as a failure. Printing it and carrying on is how a
    # disk image nobody can open gets published.
    grep -q "source=Notarized Developer ID" <<< "$assessment" \
        || die "Gatekeeper does not accept the disk image"
fi

SHA="$(shasum -a 256 "$DMG" | cut -d' ' -f1)"
URL="$DOWNLOAD_BASE/$VERSION/GamepadBridge.dmg"

if [ "$PUBLISH" -eq 0 ]; then
    step "Done"
    echo "  $DMG"
    echo "  sha256: $SHA"
    echo
    echo "Built, not published. To publish, run again with --publish."
    exit 0
fi

# ---------------------------------------------------------------------------
step "Uploading the disk image"
# ---------------------------------------------------------------------------
# The package registry gives the file a URL that does not change when the
# release is edited. The /-/releases/.../downloads/ permalink is no good:
# GitLab answers it with an HTML interstitial for absolute asset URLs.
curl --fail --silent --show-error --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
    --upload-file "$DMG" "$URL" > /dev/null \
    || die "upload failed"
echo "  $URL"

# ---------------------------------------------------------------------------
step "Writing the pending release"
# ---------------------------------------------------------------------------
VERSION="$VERSION" URL="$URL" LENGTH="$LENGTH" SIG="$ED_SIGNATURE" SHA="$SHA" \
OUT="$ROOT/packaging/pending-release.json" python3 -c '
import json, os
e = os.environ
json.dump({"version": e["VERSION"], "url": e["URL"], "length": e["LENGTH"],
           "edSignature": e["SIG"], "sha256": e["SHA"]},
          open(e["OUT"], "w"), indent=2)
print("  packaging/pending-release.json")' 

# ---------------------------------------------------------------------------
step "Checking what was uploaded"
# ---------------------------------------------------------------------------
# The same checks the pipeline runs, here first: a failure costs a minute on
# this Mac instead of a pipeline run and a commit to undo.
"$ROOT/scripts/verify-release.sh" "$ROOT/packaging/pending-release.json" \
    | sed 's/^/  /' || die "verification failed - nothing was committed"

# ---------------------------------------------------------------------------
step "Pushing the release commit"
# ---------------------------------------------------------------------------
git -C "$ROOT" add packaging/pending-release.json CMakeLists.txt \
    "packaging/release-notes/$VERSION.md"
git -C "$ROOT" commit -q -m "chore(release): $VERSION" \
    -m "Built, signed and notarized on the Mac. The pipeline verifies it again; publishing is the manual release:publish job."
git -C "$ROOT" push -q origin main

echo "  $(git -C "$ROOT" log --oneline -1)"

step "Done"
echo "  The pipeline now checks the release again:"
echo "  https://gitlab.dergeorg.at/mac/gamepadbridge/-/pipelines"
echo
echo "  When release:verify is green, start release:publish. Until then nothing"
echo "  has reached anyone: no tag, no appcast entry, no cask."
