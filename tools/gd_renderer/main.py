"""
CLI Entry Point for Geometry Dash Level Renderer & Interactive Viewer.

Usage:
  # Launch interactive viewer:
  python tools/gd_renderer/main.py
  python tools/gd_renderer/main.py --level "gemini here"
  python tools/gd_renderer/main.py --file "path/to/level.gmd"

  # Headless high-res render to image:
  python tools/gd_renderer/main.py --level "gemini here" --render --out render.png --scale 1.5

  # List local editor levels:
  python tools/gd_renderer/main.py --list
"""

import sys
import os
import argparse

# Add parent path so tools.gd_renderer can be imported cleanly
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.abspath(os.path.join(current_dir, "..", ".."))
if parent_dir not in sys.path:
    sys.path.insert(0, parent_dir)

from tools.gd_renderer.level_parser import LevelParser, GDLevel
from tools.gd_renderer.renderer import GDRenderer
from tools.gd_renderer.asset_manager import AssetManager

def main():
    parser = argparse.ArgumentParser(
        description="Geometry Dash Precise Level Renderer & Interactive Editor Viewer"
    )
    parser.add_argument("--file", "-f", type=str, help="Path to level file (.gmd, .txt, .level, .dat)")
    parser.add_argument("--level", "-l", type=str, help="Name or index of level from CCLocalLevels.dat")
    parser.add_argument("--list", action="store_true", help="List all local levels found in CCLocalLevels.dat")
    parser.add_argument("--render", action="store_true", help="Perform offline render instead of launching interactive GUI")
    parser.add_argument("--out", "-o", type=str, default="level_render.png", help="Output PNG path for offline render")
    parser.add_argument("--scale", "-s", type=float, default=1.0, help="Pixels per GD world unit (default: 1.0, 1 block = 30px)")
    parser.add_argument("--quality", "-q", type=str, default="uhd", choices=["uhd", "hd"], help="Texture quality: uhd (4x) or hd (2x)")
    parser.add_argument("--resources", "-r", type=str, help="Custom path to Geometry Dash Resources folder")
    parser.add_argument("--no-bg", action="store_true", help="Disable background in offline render")
    parser.add_argument("--no-ground", action="store_true", help="Disable ground in offline render")
    parser.add_argument("--no-glow", action="store_true", help="Disable glow sprites")
    parser.add_argument("--antialias", type=int, choices=[1, 2, 4], default=2,
                        help="Internal sampling multiplier for smooth thin objects (default: 2)")
    parser.add_argument("--preview-x", type=float,
                        help="Opt into approximate instantaneous color-trigger preview at this world X")
    parser.add_argument("--grid", action="store_true", help="Enable 30x30 editor grid in render")
    parser.add_argument("--x-start", type=float, help="Custom X start coordinate for render")
    parser.add_argument("--x-end", type=float, help="Custom X end coordinate for render")
    parser.add_argument("--y-bottom", type=float, help="Custom Y bottom coordinate for render (default: auto)")
    parser.add_argument("--y-top", type=float, help="Custom Y top coordinate for render (default: auto)")
    
    args = parser.parse_args()

    # List local levels if requested
    if args.list:
        levels = LevelParser.list_local_levels()
        print(f"\nFound {len(levels)} local levels in CCLocalLevels.dat:\n")
        print(f"{'#':<4} {'Level Name':<35} {'Author':<15} {'Revision'}")
        print("-" * 65)
        for i, lvl in enumerate(levels[:50]):
            print(f"{i:<4} {lvl['name']:<35} {lvl['author']:<15} {lvl['revision']}")
        if len(levels) > 50:
            print(f"... and {len(levels) - 50} more levels.")
        return

    # Load target level
    level = None
    if args.file:
        print(f"Loading level file: {args.file}...")
        level = LevelParser.parse_file(args.file)
    elif args.level:
        print(f"Loading local level: '{args.level}'...")
        try:
            idx = int(args.level)
            level = LevelParser.load_local_level(idx)
        except ValueError:
            level = LevelParser.load_local_level(args.level)
        if not level:
            print(f"Error: Could not find level '{args.level}' in CCLocalLevels.dat.")
            sys.exit(1)
    else:
        # Default: try loading the most recently modified level from CCLocalLevels
        local_levels = LevelParser.list_local_levels()
        if local_levels:
            # First non-empty level
            for lvl_info in local_levels:
                if len(lvl_info.get("k4", "")) > 100:
                    level = LevelParser.parse_level_string(lvl_info["k4"], name=lvl_info["name"])
                    print(f"Loaded most recent local level: '{level.name}' ({level.object_count} objects)")
                    break
        if not level:
            level = GDLevel("Empty Level")

    # Offline render mode
    if args.render:
        print(f"Rendering level '{level.name}' ({level.object_count} objects)...")
        assets = AssetManager(resources_dir=args.resources, quality=args.quality)
        renderer = GDRenderer(asset_manager=assets)
        
        bounds = level.get_bounds()
        x_start = args.x_start if args.x_start is not None else (bounds[0] - 60)
        x_end = args.x_end if args.x_end is not None else (bounds[2] + 60)
        y_bottom = args.y_bottom if args.y_bottom is not None else min(0.0, bounds[1] - 60)
        y_top = args.y_top if args.y_top is not None else (bounds[3] + 60)

        img = renderer.render_range(
            level=level,
            x_start=x_start,
            x_end=x_end,
            y_bottom=y_bottom,
            y_top=y_top,
            scale=args.scale,
            draw_bg=not args.no_bg,
            draw_ground=not args.no_ground,
            draw_grid=args.grid,
            draw_glow=not args.no_glow,
            antialias=args.antialias,
            preview_x=args.preview_x
        )
        img.save(args.out)
        print(f"Successfully saved render to {args.out} ({img.width}x{img.height})")
        return

    # Interactive Viewer mode
    from tools.gd_renderer.viewer import GDViewer

    print("Launching Geometry Dash Interactive Viewer...")
    viewer = GDViewer(initial_level=level, quality=args.quality, resources_dir=args.resources,
                      antialias=args.antialias, preview_x=args.preview_x)
    viewer.run()

if __name__ == "__main__":
    main()
