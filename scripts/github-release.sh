#!/usr/bin/env bash
#
# Mirrors a release to GitHub, disk image and all.
#
# NOTE: .github/workflows/mirror-release.yml now does this automatically when
# the mirror pushes a v* tag, without a token and without this machine. Keep
# this script for the cases the workflow cannot cover: backfilling a tag that
# predates the workflow (the manual run in Actions handles that too), or
# publishing when GitHub Actions is unavailable. Do not run both for the same
# tag expecting different results — the workflow is the normal path.
#
# GitLab's push mirroring carries branches and tags but not releases, and a
# repository whose Releases page is empty looks abandoned — which defeats the
# point of being on GitHub at all. GitLab stays the source of truth; this only
# copies what the mirror leaves behind.
#
# Needs a token with contents write access, from the environment or
# ~/.homelab/config as GITHUB_TOKEN. Never passed as an argument, so it stays
# out of shell history and the process list.
#
#   scripts/github-release.sh 1.0.6 [notes-file]
#
set -euo pipefail

REPO="${GITHUB_REPO:-DerGeorg/GamepadBridge-xbox-mac}"

VERSION="${1:-}"
NOTES_FILE="${2:-}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

die() { printf '\nERROR: %s\n' "$1" >&2; exit 1; }
step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }

# Read just the one line rather than sourcing the file: sourcing executes
# everything in it, and a value containing an unquoted $ is enough to abort
# the script under `set -u`.
read_token() {
    local config="$HOME/.homelab/config"

    [ -f "$config" ] || return 0

    GITHUB_TOKEN="$(sed -n \
        's/^[[:space:]]*\(export[[:space:]][[:space:]]*\)\{0,1\}GITHUB_TOKEN=//p' \
        "$config" | tail -1 | sed "s/^[\"']//; s/[\"']$//")"
}

[ -n "$VERSION" ] || die "usage: $(basename "$0") <version> [notes-file]"

if [ -z "${GITHUB_TOKEN:-}" ]; then
    read_token
fi

[ -n "${GITHUB_TOKEN:-}" ] || die "no GITHUB_TOKEN in the environment or \
~/.homelab/config (needs contents write access)"

DMG="$ROOT/dist/GamepadBridge-$VERSION.dmg"
[ -f "$DMG" ] || die "not found: $DMG (run scripts/release.sh first)"

API="https://api.github.com/repos/$REPO"
TAG="v$VERSION"

if [ -n "$NOTES_FILE" ]; then
    [ -f "$NOTES_FILE" ] || die "no such notes file: $NOTES_FILE"
    NOTES="$(cat "$NOTES_FILE")"
else
    NOTES="See the repository for what changed."
fi

# Through json.dumps: the notes are multi-line and would otherwise have to be
# escaped by hand, which is how a description ends up truncated at its first
# quotation mark.
BODY="$(NOTES="$NOTES" python3 -c \
    'import json, os; print(json.dumps(os.environ["NOTES"]))')"

step "Creating release $TAG on $REPO"

# The tag has to be on GitHub already, which the push mirror takes care of.
RESPONSE="$(curl --fail --silent --show-error --request POST \
    --header "Authorization: Bearer $GITHUB_TOKEN" \
    --header "Accept: application/vnd.github+json" \
    --data @- "$API/releases" <<JSON || true
{
  "tag_name": "$TAG",
  "name": "GamepadBridge $VERSION",
  "body": $BODY
}
JSON
)"

RELEASE_ID="$(printf '%s' "$RESPONSE" \
    | python3 -c 'import json,sys; print(json.load(sys.stdin).get("id",""))' \
    2>/dev/null || true)"

if [ -z "$RELEASE_ID" ]; then
    # Most likely it already exists, which is fine - reuse it rather than
    # failing a re-run.
    RELEASE_ID="$(curl --fail --silent \
        --header "Authorization: Bearer $GITHUB_TOKEN" \
        "$API/releases/tags/$TAG" \
        | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')" \
        || die "could not create or find the release - is the tag pushed?"

    echo "  reusing the existing release"
fi

step "Uploading GamepadBridge.dmg"

curl --fail --silent --show-error --request POST \
    --header "Authorization: Bearer $GITHUB_TOKEN" \
    --header "Content-Type: application/octet-stream" \
    --data-binary @"$DMG" \
    "https://uploads.github.com/repos/$REPO/releases/$RELEASE_ID/assets?name=GamepadBridge.dmg" \
    > /dev/null || die "asset upload failed (already uploaded?)"

step "Done"
echo "  https://github.com/$REPO/releases/tag/$TAG"
