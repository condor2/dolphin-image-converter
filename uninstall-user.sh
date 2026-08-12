#!/usr/bin/env bash
set -euo pipefail

PREFIX="${HOME}/.local"
rm -f "$PREFIX/bin/dolphin-image-converter"
rm -f "$PREFIX/share/kio/servicemenus/dolphin-image-converter.desktop"
echo "Dolphin Image Converter removed from $PREFIX"
