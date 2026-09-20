#!/usr/bin/env bash
#
# Updates the Homebrew cask, in this repository and in the tap.
#
# Two copies have to agree: packaging/homebrew/gamepadbridge.rb is the one
# kept under review here, and the tap is what Homebrew actually reads. Editing
# them by hand is how they drift, and a stale sha256 in the tap fails as a
# checksum mismatch — which reads like a corrupted download rather than a
# release someone forgot to finish.
#
#   scripts/update-cask.sh 1.0.1 <sha256>
#
set -euo pipefail

HOST="https://gitlab.dergeorg.at"
TAP_PROJECT="mac%2Fhomebrew-tap"

VERSION="${1:-}"
SHA="${2:-}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CASK="$ROOT/packaging/homebrew/gamepadbridge.rb"

die() { printf '\nERROR: %s\n' "$1" >&2; exit 1; }
step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }

[ -n "$VERSION" ] && [ -n "$SHA" ] \
    || die "usage: $(basename "$0") <version> <sha256>"

[ ${#SHA} -eq 64 ] || die "that does not look like a sha256: $SHA"

if [ -z "${GITLAB_TOKEN:-}" ] && [ -f "$HOME/.homelab/config" ]; then
    # shellcheck disable=SC1091
    . "$HOME/.homelab/config"
fi

[ -n "${GITLAB_TOKEN:-}" ] || die "no GITLAB_TOKEN in the environment or \
~/.homelab/config"

step "Updating $CASK"

python3 - "$CASK" "$VERSION" "$SHA" <<'PY'
import re, sys, pathlib

path, version, sha = sys.argv[1], sys.argv[2], sys.argv[3]
cask = pathlib.Path(path)
text = cask.read_text()

text, versions = re.subn(r'version "[^"]*"', f'version "{version}"', text, count=1)
text, hashes = re.subn(r'sha256 "[^"]*"', f'sha256 "{sha}"', text, count=1)

if not versions or not hashes:
    sys.exit("could not find version/sha256 in the cask")

cask.write_text(text)
print(f"  version {version}")
print(f"  sha256  {sha}")
PY

step "Pushing to the tap"

python3 - "$CASK" "$VERSION" <<PY
import json, os, subprocess, sys, pathlib

content = pathlib.Path(sys.argv[1]).read_text()
version = sys.argv[2]

payload = {
    "branch": "main",
    "commit_message": f"gamepadbridge {version}",
    "actions": [{"action": "update",
                 "file_path": "Casks/gamepadbridge.rb",
                 "content": content}],
}

result = subprocess.run(
    ["curl", "-sf", "--request", "POST",
     "--header", "PRIVATE-TOKEN: ${GITLAB_TOKEN}",
     "--header", "Content-Type: application/json", "--data", "@-",
     "${HOST}/api/v4/projects/${TAP_PROJECT}/repository/commits"],
    input=json.dumps(payload), capture_output=True, text=True)

if result.returncode != 0:
    sys.exit(f"tap update failed: {result.stdout[:300]}{result.stderr[:200]}")

print("  commit", json.loads(result.stdout)["short_id"])
PY

step "Done"
echo "  brew upgrade --cask gamepadbridge"
