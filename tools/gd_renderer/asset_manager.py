"""
Geometry Dash Asset Manager
Handles finding Geometry Dash resources, parsing Cocos2d-x plists,
and extracting individual sprite frames accurately with rotation and offset fixes.
"""

import os
import glob
import re
import plistlib
import warnings
from typing import Dict, Tuple, Optional, Any
from PIL import Image

# Default paths to search for GD Resources
COMMON_GD_PATHS = [
    r"C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\Resources",
    r"C:\Program Files\Steam\steamapps\common\Geometry Dash\Resources",
    r"D:\SteamLibrary\steamapps\common\Geometry Dash\Resources",
    r"E:\SteamLibrary\steamapps\common\Geometry Dash\Resources",
    r"F:\SteamLibrary\steamapps\common\Geometry Dash\Resources",
]

def find_gd_resources_dir(custom_path: Optional[str] = None) -> str:
    """Finds the Geometry Dash Resources directory."""
    if custom_path:
        if not os.path.isdir(custom_path):
            raise FileNotFoundError(f"Geometry Dash Resources directory not found: {custom_path}")
        return custom_path
    
    for path in COMMON_GD_PATHS:
        if os.path.isdir(path):
            return path
            
    raise FileNotFoundError(
        "Could not find Geometry Dash Resources folder. "
        "Please provide the path using --resources <path>."
    )

def _parse_number_list(value: str) -> list[float]:
    """Cocos offsets include half pixels; splitting them into integers moves frames."""
    return [float(number) for number in re.findall(r"[-+]?(?:\d*\.\d+|\d+)", value)]

class SpriteFrameInfo:
    def __init__(self, plist_path: str, png_path: str, raw_frame: dict):
        self.plist_path = plist_path
        self.png_path = png_path
        
        # TexturePacker/Cocos plist formats 3 and 2, respectively.
        rect = _parse_number_list(raw_frame.get("textureRect") or raw_frame["frame"])
        self.x, self.y, self.w, self.h = (round(number) for number in rect)
        self.rotated = raw_frame.get("textureRotated", raw_frame.get("rotated", False))
        
        offset = _parse_number_list(raw_frame.get("spriteOffset", raw_frame.get("offset", "{0,0}")))
        self.offset_x, self.offset_y = offset[0], offset[1]
        
        size = _parse_number_list(raw_frame.get("spriteSize", f"{{{self.w},{self.h}}}"))
        self.trim_w, self.trim_h = (round(number) for number in size)
        
        source_size = _parse_number_list(raw_frame.get("spriteSourceSize", raw_frame.get("sourceSize",
                                         raw_frame.get("spriteSize", f"{{{self.w},{self.h}}}"))))
        self.source_w, self.source_h = (round(number) for number in source_size)


class AssetManager:
    """Manages all Geometry Dash sprite sheets and textures."""
    def __init__(self, resources_dir: Optional[str] = None, quality: str = "uhd"):
        self.resources_dir = find_gd_resources_dir(resources_dir)
        self.quality = quality.lower() # "uhd", "hd", or ""
        if self.quality not in ("uhd", "hd", ""):
            raise ValueError("Texture quality must be uhd, hd or an empty string for SD")
        self.scale_factor = 4.0 if self.quality == "uhd" else (2.0 if self.quality == "hd" else 1.0)
        
        self.frame_registry: Dict[str, SpriteFrameInfo] = {}
        self.atlas_cache: Dict[str, Image.Image] = {}
        self.sprite_cache: Dict[str, Image.Image] = {}
        self.pygame_cache: Dict[str, Any] = {}
        self.texture_cache: Dict[str, Image.Image] = {}
        
        self._index_plists()
        
    def _index_plists(self):
        """Index low quality first; requested-quality frames override fallbacks."""
        priorities = {"": 0, "hd": 1, "uhd": 2}
        candidates = []
        for path in glob.glob(os.path.join(self.resources_dir, "*.plist")):
            quality = "uhd" if path.endswith("-uhd.plist") else ("hd" if path.endswith("-hd.plist") else "")
            if priorities[quality] <= priorities[self.quality]:
                candidates.append((priorities[quality], path))
        for _, plist_path in sorted(candidates):
            png_path = plist_path[:-len(".plist")] + ".png"
            if not os.path.exists(png_path):
                continue
                
            try:
                with open(plist_path, "rb") as f:
                    data = plistlib.load(f)
                    frames = data.get("frames", {})
                    for frame_name, frame_dict in frames.items():
                        self.frame_registry[frame_name] = SpriteFrameInfo(plist_path, png_path, frame_dict)
            except (OSError, ValueError, KeyError, TypeError, plistlib.InvalidFileException) as error:
                warnings.warn(f"Could not index sprite atlas {plist_path}: {error}", stacklevel=2)

    def get_atlas(self, png_path: str) -> Image.Image:
        """Loads and caches full atlas PNG."""
        if png_path not in self.atlas_cache:
            self.atlas_cache[png_path] = Image.open(png_path).convert("RGBA")
        return self.atlas_cache[png_path]

    def get_sprite(self, frame_name: str) -> Optional[Image.Image]:
        """
        Extracts, unrotates, and un-trims a sprite frame from its sheet.
        Returns a PIL Image with full source dimensions and transparent background.
        """
        if not frame_name:
            return None
            
        if frame_name in self.sprite_cache:
            return self.sprite_cache[frame_name]
            
        frame_info = self.frame_registry.get(frame_name)
        if not frame_info:
            return None
            
        atlas = self.get_atlas(frame_info.png_path)
        
        # Crop accurately from atlas
        rx, ry, rw, rh = frame_info.x, frame_info.y, frame_info.w, frame_info.h
        if frame_info.rotated:
            # TexturePacker rotated it 90 deg clockwise into the sheet,
            # so dimensions in texture are (rh, rw).
            crop_box = (rx, ry, rx + rh, ry + rw)
            cropped = atlas.crop(crop_box)
            # Restore original orientation: 90 deg counter-clockwise in PIL
            cropped = cropped.transpose(Image.Transpose.ROTATE_90)
        else:
            crop_box = (rx, ry, rx + rw, ry + rh)
            cropped = atlas.crop(crop_box)
            
        # Reconstruct full untrimmed canvas
        sw, sh = frame_info.source_w, frame_info.source_h
        tw, th = frame_info.trim_w, frame_info.trim_h
        ox, oy = frame_info.offset_x, frame_info.offset_y
        
        canvas = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
        paste_x = round((sw - tw) / 2 + ox)
        paste_y = round((sh - th) / 2 - oy) # Cocos2d-x Y is inverted
        
        canvas.paste(cropped, (paste_x, paste_y))
        self.sprite_cache[frame_name] = canvas
        return canvas

    def get_background(self, bg_id: int) -> Optional[Image.Image]:
        """Loads background image."""
        return self._load_texture(f"game_bg_{bg_id:02d}_001", "game_bg_01_001")

    def get_ground(self, ground_id: int) -> Tuple[Optional[Image.Image], Optional[Image.Image]]:
        """Loads ground texture and ground line texture."""
        ground = self._load_texture(f"groundSquare_{ground_id:02d}_001", "groundSquare_01_001")
        line = self._load_texture(f"groundSquare_{ground_id:02d}_2_001", "groundSquare_01_2_001")
        return ground, line

    def _load_texture(self, name: str, fallback: str) -> Optional[Image.Image]:
        qualities = {"uhd": ("-uhd", "-hd", ""), "hd": ("-hd", ""), "": ("",)}
        for texture in dict.fromkeys((name, fallback)):
            for suffix in qualities[self.quality]:
                path = os.path.join(self.resources_dir, texture + suffix + ".png")
                if path in self.texture_cache:
                    return self.texture_cache[path]
                if os.path.isfile(path):
                    with Image.open(path) as image:
                        self.texture_cache[path] = image.convert("RGBA")
                    return self.texture_cache[path]
        return None
