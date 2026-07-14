#!/usr/bin/env bash
#
# Downloads and extracts the proprietary firmware required by the Xbox
# Wireless Adapter (dongle). The firmware is NOT redistributable; this
# script fetches it straight from Microsoft's Windows Update servers, the
# same source used by xow on Linux.
#
# The firmware is covered by the Microsoft Terms of Use:
#   https://www.microsoft.com/en-us/legal/terms-of-use
#
# Usage:
#   scripts/get-firmware.sh [DESTINATION]
#
# DESTINATION defaults to /usr/local/share/gamepadbridge/xow_dongle.bin
# (matches the CMake default). If you cannot write there, pass a path in
# your home directory and run the driver with:
#   XOW_FIRMWARE=/path/to/xow_dongle.bin ./gamepadbridge

set -euo pipefail

DEST="${1:-/usr/local/share/gamepadbridge/xow_dongle.bin}"

# Microsoft Xbox Wireless Adapter driver package (same as used by xow).
URL="http://download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/07/1cd6a87c-623f-4407-a52d-c31be49e925c_e19f60808bdcbfbd3c3df6be3e71ffc52e43261e.cab"
CAB_MEMBER="FW_ACC_00U.bin"
SHA256="48084d9fa53b9bb04358f3bb127b7495dc8f7bb0b3ca1437bd24ef2b6eabdf66"

fail() { echo "ERROR: $1" >&2; exit 1; }

command -v curl >/dev/null 2>&1 || fail "curl not found"
command -v shasum >/dev/null 2>&1 || fail "shasum not found"
command -v cabextract >/dev/null 2>&1 || \
    fail "cabextract not found — install it with: brew install cabextract"

echo "This downloads firmware covered by the Microsoft Terms of Use:"
echo "  https://www.microsoft.com/en-us/legal/terms-of-use"
printf "Continue? [y/N]: "
read -r answer
case "$answer" in
    y|Y|yes|YES) ;;
    *) fail "Aborted." ;;
esac

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading driver package..."
curl -L --fail -o "$TMP/driver.cab" "$URL"

echo "Extracting $CAB_MEMBER..."
cabextract -d "$TMP" -F "$CAB_MEMBER" "$TMP/driver.cab"

echo "Verifying checksum..."
echo "$SHA256  $TMP/$CAB_MEMBER" | shasum -a 256 -c - \
    || fail "Checksum mismatch — refusing to install."

DEST_DIR="$(dirname "$DEST")"
if [ ! -d "$DEST_DIR" ]; then
    echo "Creating $DEST_DIR (may require your password)..."
    if ! mkdir -p "$DEST_DIR" 2>/dev/null; then
        sudo mkdir -p "$DEST_DIR"
    fi
fi

if ! cp "$TMP/$CAB_MEMBER" "$DEST" 2>/dev/null; then
    echo "Installing to $DEST (may require your password)..."
    sudo cp "$TMP/$CAB_MEMBER" "$DEST"
fi

echo "Firmware installed: $DEST"
