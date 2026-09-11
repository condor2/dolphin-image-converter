#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

SYSTEM_BIN="/usr/local/bin/dolphin-image-converter"
SERVICE_FILE="$HOME/.local/share/kio/servicemenus/dolphin-image-converter.desktop"
LEGACY_BIN="$HOME/.local/bin/dolphin-image-converter"

if [[ ${EUID} -eq 0 ]]; then
    echo "Run this script as your normal desktop user, not with sudo." >&2
    exit 1
fi
if ! command -v sudo >/dev/null 2>&1; then
    echo "sudo is required to remove $SYSTEM_BIN." >&2
    exit 1
fi

rm -f "$SERVICE_FILE" "$LEGACY_BIN"
sudo rm -f "$SYSTEM_BIN"

if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
fi

echo "Dolphin Image Converter was removed."
echo "Restart Dolphin to refresh the context menu:"
echo "  kquitapp6 dolphin 2>/dev/null || true"
echo "  dolphin &"
