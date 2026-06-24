#!/usr/bin/env bash
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}Paimbnails Installer for macOS${NC}"
echo ""

# Find Geometry Dash mod directory
GD_MODS_DIR="$HOME/Library/Application Support/Geometry Dash/geode/mods"

if [ ! -d "$GD_MODS_DIR" ]; then
    echo -e "${YELLOW}Geode mods folder not found.${NC}"
    echo "Please make sure Geometry Dash with Geode is installed and launched at least once."
    exit 1
fi

# Find the .geode file next to this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GEODE_FILE=$(find "$SCRIPT_DIR" -maxdepth 1 -name "*.geode" | head -n 1)

if [ -z "$GEODE_FILE" ]; then
    echo -e "${RED}Error: No .geode file found next to this script.${NC}"
    exit 1
fi

cp "$GEODE_FILE" "$GD_MODS_DIR/"
GEODE_NAME=$(basename "$GEODE_FILE")

echo -e "${GREEN}Paimbnails installed successfully!${NC}"
echo "Location: $GD_MODS_DIR/$GEODE_NAME"
echo "Restart Geometry Dash to use the mod."
