#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
PREFIX="${HOME}/.local"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX"

cmake --build "$BUILD_DIR" -j"$(nproc)"
cmake --install "$BUILD_DIR"

SERVICE_MENU="$PREFIX/share/kio/servicemenus/dolphin-image-converter.desktop"

# Dolphin is started by the graphical session, whose PATH may not include ~/.local/bin.
# Store the absolute executable path in the Service Menu to avoid "cannot find" errors.
sed -i "s|Exec=dolphin-image-converter|Exec=$PREFIX/bin/dolphin-image-converter|g" "$SERVICE_MENU"
chmod +x "$SERVICE_MENU"

echo
echo "Installed:"
echo "  $PREFIX/bin/dolphin-image-converter"
echo "  $PREFIX/share/kio/servicemenus/dolphin-image-converter.desktop"
echo
echo "Restart Dolphin to reload the Service Menu."
