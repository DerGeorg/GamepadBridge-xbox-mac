#!/usr/bin/env bash
#
# Publishes a built disk image as a GitLab release.
#
# The token comes from the environment or from ~/.homelab/config, never from
# an argument, so it stays out of your shell history and out of the process
# list:
#
#   scripts/gitlab-release.sh 1.0.0 [notes.md]
#
set -euo pipefail

HOST="https://gitlab.dergeorg.at"
PROJECT="mac%2Fgamepadbridge"

VERSION="${1:-}"
NOTES_FILE="${2:-}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

die() { printf '\nERROR: %s\n' "$1" >&2; exit 1; }
step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }

[ -n "$VERSION" ] || die "usage: $(basename "$0") <version> [notes-file]"

# Note that GITLAB_PROJECT_ID in that file points at a different project, so
# only the token is taken from it; the project is pinned above.
if [ -z "${GITLAB_TOKEN:-}" ] && [ -f "$HOME/.homelab/config" ]; then
    # shellcheck disable=SC1091
    . "$HOME/.homelab/config"
fi

[ -n "${GITLAB_TOKEN:-}" ] || die "no GITLAB_TOKEN in the environment or \
~/.homelab/config (needs api scope)"

DMG="$ROOT/dist/GamepadBridge-$VERSION.dmg"
[ -f "$DMG" ] || die "not found: $DMG (run scripts/release.sh first)"

API="$HOST/api/v4/projects/$PROJECT"
TAG="v$VERSION"

# The generic package registry gives the file a stable URL that does not
# change when the release is edited, which is what the Homebrew cask needs.
#
# The link is typed "other" rather than "package": GitLab files a "package"
# link away under its own heading in the release page, where nobody looking
# for a download thinks to look.
step "Uploading $(basename "$DMG")"

PACKAGE_URL="$API/packages/generic/gamepadbridge/$VERSION/GamepadBridge.dmg"

curl --fail --silent --show-error \
    --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
    --upload-file "$DMG" "$PACKAGE_URL" > /dev/null

step "Creating release $TAG"

if [ -n "$NOTES_FILE" ]; then
    [ -f "$NOTES_FILE" ] || die "no such notes file: $NOTES_FILE"
    NOTES="$(cat "$NOTES_FILE")"
else
    NOTES="See the repository for what changed."
fi

# Through jq-free JSON: the notes are multi-line and would otherwise have to
# be escaped by hand, which is how a release description ends up truncated at
# the first quotation mark.
DESCRIPTION="$(NOTES="$NOTES" python3 -c \
    'import json, os; print(json.dumps(os.environ["NOTES"]))')"

curl --fail --silent --show-error --request POST \
    --header "PRIVATE-TOKEN: $GITLAB_TOKEN" \
    --header "Content-Type: application/json" \
    --data @- "$API/releases" <<JSON > /dev/null
{
  "name": "GamepadBridge $VERSION",
  "tag_name": "$TAG",
  "ref": "main",
  "description": $DESCRIPTION,
  "assets": {
    "links": [
      {
        "name": "GamepadBridge.dmg (macOS 15+, signed & notarized)",
        "url": "$PACKAGE_URL",
        "direct_asset_path": "/GamepadBridge.dmg",
        "link_type": "other"
      }
    ]
  }
}
JSON

step "Done"
echo "  $HOST/mac/gamepadbridge/-/releases/$TAG"
# Not the /downloads/ permalink: GitLab answers that with an interstitial
# warning page for absolute asset URLs, so anything scripted gets HTML.
echo "  download: $PACKAGE_URL"
echo
echo "  sha256: $(shasum -a 256 "$DMG" | cut -d' ' -f1)"
