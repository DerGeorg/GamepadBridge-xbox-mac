#!/usr/bin/env bash
#
# Publishes a built disk image as a GitLab release.
#
# The token is read from the environment and never passed on the command
# line, so it stays out of your shell history and out of the process list:
#
#   export GITLAB_TOKEN=...        # a project access token with api scope
#   scripts/gitlab-release.sh 1.0.0
#
set -euo pipefail

HOST="https://gitlab.dergeorg.at"
PROJECT="mac%2Fgamepadbridge"

VERSION="${1:-}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

die() { printf '\nERROR: %s\n' "$1" >&2; exit 1; }
step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }

[ -n "$VERSION" ] || die "usage: $(basename "$0") <version>"
[ -n "${GITLAB_TOKEN:-}" ] || die "set GITLAB_TOKEN to a token with api scope"

DMG="$ROOT/dist/GamepadBridge-$VERSION.dmg"
[ -f "$DMG" ] || die "not found: $DMG (run scripts/release.sh first)"

API="$HOST/api/v4/projects/$PROJECT"
TAG="v$VERSION"

# The generic package registry gives the file a stable URL that does not
# change when the release is edited, which is what the Homebrew cask needs.
step "Uploading $(basename "$DMG")"

PACKAGE_URL="$API/packages/generic/gamepadbridge/$VERSION/GamepadBridge.dmg"

curl --fail --silent --show-error \
    --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
    --upload-file "$DMG" "$PACKAGE_URL" > /dev/null

step "Creating release $TAG"

curl --fail --silent --show-error --request POST \
    --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
    --header "Content-Type: application/json" \
    --data @- "$API/releases" <<JSON > /dev/null
{
  "name": "GamepadBridge $VERSION",
  "tag_name": "$TAG",
  "ref": "main",
  "description": "See the changelog in the repository.",
  "assets": {
    "links": [
      {
        "name": "GamepadBridge.dmg",
        "url": "$PACKAGE_URL",
        "direct_asset_path": "/GamepadBridge.dmg",
        "link_type": "package"
      }
    ]
  }
}
JSON

step "Done"
echo "  $HOST/mac/gamepadbridge/-/releases/$TAG"
echo "  download: $HOST/mac/gamepadbridge/-/releases/$TAG/downloads/GamepadBridge.dmg"
echo
echo "  sha256: $(shasum -a 256 "$DMG" | cut -d' ' -f1)"
