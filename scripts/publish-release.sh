#!/usr/bin/env bash
#
# Publishes a release that scripts/verify-release.sh has already checked.
#
# Runs as the manual release:publish job, after the Mac pushed a pending
# release (scripts/release.sh --publish). In order:
#
#   1. GitLab release and tag      the push mirror carries the tag to GitHub,
#                                  where a workflow mirrors the release
#   2. appcast, cask, pending file one commit on main — from this moment every
#                                  installed copy is offered the update
#   3. Homebrew tap                brew upgrade picks it up
#
# The appcast is written HERE and not on the Mac on purpose: the moment it
# reaches main, installed copies see the update. Writing it only after the
# checks have passed means a broken release reaches nobody.
#
# Every step first checks whether it is already done, so a run that stops half
# way can simply be started again.
#
#   scripts/publish-release.sh [--dry-run]
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PENDING="$ROOT/packaging/pending-release.json"
CASK="$ROOT/packaging/homebrew/gamepadbridge.rb"

HOST="https://gitlab.dergeorg.at"
API="$HOST/api/v4/projects/mac%2Fgamepadbridge"
TAP_API="$HOST/api/v4/projects/mac%2Fhomebrew-tap"

DRY_RUN=0
[ "${1:-}" = "--dry-run" ] && DRY_RUN=1

die() { printf '\nERROR: %s\n' "$1" >&2; exit 1; }
step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }

# In CI the token comes from the instance variable. Locally, read just the one
# line of ~/.homelab/config rather than sourcing it: sourcing executes whatever
# else is in there.
TOKEN="${GITLAB_CI_BOT_TOKEN:-${GITLAB_TOKEN:-}}"
if [ -z "$TOKEN" ] && [ -f "$HOME/.homelab/config" ]; then
    TOKEN="$(sed -n 's/^[[:space:]]*\(export[[:space:]][[:space:]]*\)\{0,1\}GITLAB_TOKEN=//p' \
        "$HOME/.homelab/config" | tail -1 | sed "s/^[\"']//; s/[\"']$//")"
fi
[ -n "$TOKEN" ] || die "no token: set GITLAB_CI_BOT_TOKEN or GITLAB_TOKEN"

[ -f "$PENDING" ] || die "no pending release at $PENDING"

field() {
    python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' \
        "$PENDING" "$1"
}

VERSION="$(field version)"
URL="$(field url)"
LENGTH="$(field length)"
SIGNATURE="$(field edSignature)"
SHA256="$(field sha256)"
TAG="v$VERSION"
REF="${CI_COMMIT_SHA:-$(git -C "$ROOT" rev-parse HEAD)}"
NOTES="$ROOT/packaging/release-notes/$VERSION.md"

api() {  # api <METHOD> <URL> [json-body]
    if [ $# -ge 3 ]; then
        curl --fail --silent --show-error --request "$1" \
            --header "PRIVATE-TOKEN: $TOKEN" \
            --header "Content-Type: application/json" --data "$3" "$2"
    else
        curl --fail --silent --show-error --request "$1" \
            --header "PRIVATE-TOKEN: $TOKEN" "$2"
    fi
}

exists() {  # exists <URL> -> HTTP 200?
    [ "$(curl --silent --output /dev/null --write-out '%{http_code}' \
        --header "PRIVATE-TOKEN: $TOKEN" "$1")" = "200" ]
}

# Writes happen only for real; a dry run shows what would be sent.
write() {  # write <what> <METHOD> <URL> <json-body>
    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [dry run] would $1"
        printf '%s' "$4" | python3 -c 'import json,sys; d=json.load(sys.stdin); print("  " + json.dumps(d, ensure_ascii=False)[:400])'
    else
        api "$2" "$3" "$4" > /dev/null
        echo "  $1"
    fi
}

echo "Publishing GamepadBridge $VERSION (ref ${REF:0:8})"

# ---------------------------------------------------------------------------
step "1. GitLab release $TAG"
# ---------------------------------------------------------------------------
if exists "$API/releases/$TAG"; then
    echo "  already exists — skipping"
else
    BODY="$(TAG="$TAG" VERSION="$VERSION" REF="$REF" URL="$URL" NOTES="$NOTES" python3 -c '
import json, os
e = os.environ
print(json.dumps({
    "name": "GamepadBridge " + e["VERSION"],
    "tag_name": e["TAG"],
    "ref": e["REF"],
    "description": open(e["NOTES"]).read(),
    "assets": {"links": [{
        "name": "GamepadBridge.dmg (macOS 15+, signed & notarized)",
        "url": e["URL"],
        "direct_asset_path": "/GamepadBridge.dmg",
        # "package" links are filed away under their own heading on the
        # release page, where nobody looking for a download finds them.
        "link_type": "other"
    }]}
}))')"
    write "create release $TAG at ${REF:0:8}" POST "$API/releases" "$BODY"
fi

# ---------------------------------------------------------------------------
step "2. Appcast and cask on main"
# ---------------------------------------------------------------------------
if ! exists "$API/repository/files/packaging%2Fpending-release.json?ref=main" \
    && [ "$DRY_RUN" -eq 0 ]; then
    echo "  pending file is gone from main — already published, skipping"
else
    if [ "$DRY_RUN" -eq 1 ] \
        && ! exists "$API/repository/files/packaging%2Fpending-release.json?ref=main"; then
        echo "  (no pending file on main right now — a real run would skip this;"
        echo "   shown anyway so the dry run exercises it)"
    fi

    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT

    # Work from the files on main, not this checkout: main may have moved on
    # since the release commit, and this commit replaces them wholesale.
    for f in appcast.xml packaging/homebrew/gamepadbridge.rb; do
        enc="$(python3 -c 'import sys,urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "$f")"
        api GET "$API/repository/files/$enc/raw?ref=main" > "$WORK/$(basename "$f")"
    done

    python3 "$ROOT/scripts/make-appcast.py" "$WORK/appcast.xml" "$VERSION" \
        "$URL" "$LENGTH" "$SIGNATURE" | sed 's/^/  /'

    python3 - "$WORK/gamepadbridge.rb" "$VERSION" "$SHA256" <<'PY'
import re, sys
path, version, sha = sys.argv[1:4]
text = open(path).read()
text, v = re.subn(r'version "[^"]*"', f'version "{version}"', text, count=1)
text, s = re.subn(r'sha256 "[^"]*"', f'sha256 "{sha}"', text, count=1)
if not (v and s):
    sys.exit("could not find version and sha256 in the cask")
open(path, "w").write(text)
PY
    echo "  cask: version $VERSION, sha256 ${SHA256:0:16}…"

    BODY="$(VERSION="$VERSION" WORK="$WORK" python3 -c '
import json, os
w = os.environ["WORK"]
print(json.dumps({
    "branch": "main",
    # [skip ci]: this commit must not start another pipeline.
    "commit_message": "chore(release): publish " + os.environ["VERSION"] + " [skip ci]",
    "actions": [
        {"action": "update", "file_path": "appcast.xml",
         "content": open(w + "/appcast.xml").read()},
        {"action": "update", "file_path": "packaging/homebrew/gamepadbridge.rb",
         "content": open(w + "/gamepadbridge.rb").read()},
        {"action": "delete", "file_path": "packaging/pending-release.json"},
    ]
}))')"
    write "commit appcast, cask and remove the pending file on main" \
        POST "$API/repository/commits" "$BODY"
fi

# ---------------------------------------------------------------------------
step "3. Homebrew tap"
# ---------------------------------------------------------------------------
CURRENT="$(api GET "$TAP_API/repository/files/Casks%2Fgamepadbridge.rb/raw?ref=main" \
    | sed -n 's/^  version "\(.*\)"/\1/p')"

if [ "$CURRENT" = "$VERSION" ]; then
    echo "  tap already at $VERSION — skipping"
else
    CASK_NOW="$(api GET "$API/repository/files/packaging%2Fhomebrew%2Fgamepadbridge.rb/raw?ref=main")"

    # In a dry run the commit in step 2 did not happen, so main still has the
    # old cask; show the change that would be pushed instead.
    if [ "$DRY_RUN" -eq 1 ]; then
        CASK_NOW="$(printf '%s' "$CASK_NOW" | sed "s/version \"[^\"]*\"/version \"$VERSION\"/; s/sha256 \"[^\"]*\"/sha256 \"$SHA256\"/")"
    fi

    BODY="$(VERSION="$VERSION" CASK_NOW="$CASK_NOW" python3 -c '
import json, os
print(json.dumps({
    "branch": "main",
    "commit_message": "gamepadbridge " + os.environ["VERSION"],
    "actions": [{"action": "update", "file_path": "Casks/gamepadbridge.rb",
                 "content": os.environ["CASK_NOW"]}]
}))')"
    write "update the tap from $CURRENT to $VERSION" POST "$TAP_API/repository/commits" "$BODY"
fi

step "Done"
echo "  $HOST/mac/gamepadbridge/-/releases/$TAG"
