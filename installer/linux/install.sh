#!/usr/bin/env bash
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}Paimbnails Installer for Linux${NC}"
echo ""

# Find Geometry Dash installation
find_gd() {
    local DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
    local paths=(
        "$DATA_HOME/Steam/steamapps/common/Geometry Dash"
        "$HOME/Steam/steamapps/common/Geometry Dash"
        "$HOME/.steam/debian-installation/steamapps/common/Geometry Dash"
        "$HOME/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/common/Geometry Dash"
        "$HOME/snap/steam/common/.steam/steamapps/common/Geometry Dash"
    )

    for p in "${paths[@]}"; do
        if [ -f "$p/GeometryDash.exe" ] || [ -f "$p/libcocos2d.dll" ]; then
            echo "$p"
            return 0
        fi
    done
    return 1
}

GD_PATH=$(find_gd)

if [ -z "$GD_PATH" ]; then
    echo -e "${YELLOW}Could not automatically find Geometry Dash.${NC}"
    read -rp "Please enter the path to your Geometry Dash folder: " GD_PATH
    if [ ! -f "$GD_PATH/GeometryDash.exe" ] && [ ! -f "$GD_PATH/libcocos2d.dll" ]; then
        echo -e "${RED}Error: Geometry Dash not found at that path.${NC}"
        exit 1
    fi
fi

MODS_DIR="$GD_PATH/geode/mods"
mkdir -p "$MODS_DIR"

# Find the .geode file next to this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GEODE_FILE=$(find "$SCRIPT_DIR" -maxdepth 1 -name "*.geode" | head -n 1)

if [ -z "$GEODE_FILE" ]; then
    echo -e "${RED}Error: No .geode file found next to this script.${NC}"
    exit 1
fi

cp "$GEODE_FILE" "$MODS_DIR/"
GEODE_NAME=$(basename "$GEODE_FILE")

echo -e "${GREEN}Paimbnails installed successfully!${NC}"
echo "Location: $MODS_DIR/$GEODE_NAME"
echo "Restart Geometry Dash to use the mod."
