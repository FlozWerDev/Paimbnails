"""
Geometry Dash Level Parser
Reads Geometry Dash levels from CCLocalLevels.dat, .gmd files, .txt files, or raw level strings.
Accurately parses level headers, color definitions, and object properties.
"""

import os
import zlib
import base64
import binascii
import json
import xml.etree.ElementTree as ET
from typing import Dict, List, Optional, Any, Tuple
from .color_engine import HSVShift
from .geometry import object_bounds

DATA_DIR = os.path.join(os.path.dirname(__file__), "data")
OBJECTS_JSON_PATH = os.path.join(DATA_DIR, "objects.json")

# Z-Layer mapping in Geometry Dash
ZLAYER_VALUES = {
    -5: -1, # B5 (GD 2.2)
    -3: 0, # B4
    -1: 1, # B3
     1: 2, # B2
     3: 3, # B1
     4: 3.5, # Back of portals, immediately behind the player
     5: 4, # T1
     7: 5, # T2
     9: 6, # T3
    11: 7, # T4
}

class LevelObject:
    """Represents a single placed object in a Geometry Dash level."""
    def __init__(self, obj_id: int = 1, x: float = 0.0, y: float = 0.0):
        self.id = obj_id
        self.x = x
        self.y = y
        self.flip_x: bool = False
        self.flip_y: bool = False
        self.rotation: float = 0.0
        self.rotation_x: Optional[float] = None
        self.rotation_y: Optional[float] = None
        self.scale: float = 1.0
        self.scale_x: float = 1.0
        self.scale_y: float = 1.0
        
        self.z_layer: float = 3 # Default B1
        self.z_order: int = 0
        
        self.base_color_channel: int = 1004 # Default OBJ
        self.detail_color_channel: int = 1
        
        self.base_hsv_enabled: bool = False
        self.detail_hsv_enabled: bool = False
        self.base_hsv_shift: HSVShift = HSVShift()
        self.detail_hsv_shift: HSVShift = HSVShift()
        
        self.editor_layer: int = 0
        self.groups: List[int] = []
        self.raw_props: Dict[int, str] = {}


class GDLevel:
    """Represents a complete Geometry Dash level."""
    def __init__(self, name: str = "Unnamed"):
        self.name = name
        self.author: str = ""
        self.description: str = ""
        self.bg_id: int = 1
        self.ground_id: int = 1
        self.ks38: str = ""
        self.objects: List[LevelObject] = []
        self.header_props: Dict[str, str] = {}

    @property
    def object_count(self) -> int:
        return len(self.objects)

    def get_bounds(self) -> Tuple[float, float, float, float]:
        """Bounds include sprite extents, scale, flips and independent axis rotations."""
        if not self.objects:
            return (0.0, 0.0, 1000.0, 600.0)
        definitions = LevelParser.get_object_defs()
        bounds = [object_bounds(obj, definitions.get(str(obj.id), {})) for obj in self.objects]
        return (min(b[0] for b in bounds), min(b[1] for b in bounds),
                max(b[2] for b in bounds), max(b[3] for b in bounds))


class LevelParser:
    """Parses level strings and level files into GDLevel objects."""
    
    _object_defs: Optional[Dict[str, Any]] = None

    @classmethod
    def get_object_defs(cls) -> Dict[str, Any]:
        """Loads and caches the objects.json database."""
        if cls._object_defs is None:
            if os.path.exists(OBJECTS_JSON_PATH):
                with open(OBJECTS_JSON_PATH, "r", encoding="utf-8") as f:
                    cls._object_defs = json.load(f)
            else:
                cls._object_defs = {}
        return cls._object_defs

    @staticmethod
    def decompress_level_string(raw: str) -> str:
        """Decompresses a GD level string (handles plain text, base64, and gzip)."""
        raw = raw.strip().lstrip("\ufeff")
        if not raw or raw.startswith(("kS", "kA", "1,")):
            return raw
            
        try:
            # URL-safe base64 cleanup
            b64 = raw.replace("-", "+").replace("_", "/")
            b64 += "=" * ((4 - len(b64) % 4) % 4)
            data = base64.b64decode(b64.encode("ascii"), validate=True)
            # gzip decompress (15 | 32 enables automatic zlib/gzip header detection)
            return zlib.decompress(data, 15 | 32).decode("utf-8")
        except (binascii.Error, UnicodeError, zlib.error) as error:
            raise ValueError("Invalid GD level string: expected object text or base64 gzip/zlib data") from error

    @classmethod
    def parse_level_string(cls, level_string: str, name: str = "Unnamed") -> GDLevel:
        """Parses a decompressed or raw level string into a GDLevel."""
        plain = cls.decompress_level_string(level_string)
        level = GDLevel(name=name)
        
        sections = plain.split(";")
        if not sections:
            return level
            
        # Object-only exports have no header; keep their first object.
        has_header = not sections[0].startswith("1,")
        header_raw = sections[0].split(",") if has_header else []
        header_props: Dict[str, str] = {}
        for i in range(0, len(header_raw) - 1, 2):
            header_props[header_raw[i]] = header_raw[i + 1]
            
        level.header_props = header_props
        level.bg_id = int(header_props.get("kA6", 1))
        level.ground_id = int(header_props.get("kA7", 1))
        level.ks38 = header_props.get("kS38", "")
        
        obj_defs = cls.get_object_defs()
        
        # Parse Objects (sections 1..N)
        for obj_str in sections[1 if has_header else 0:]:
            if not obj_str.strip():
                continue
            parts = obj_str.split(",")
            props: Dict[int, str] = {}
            for i in range(0, len(parts) - 1, 2):
                try:
                    props[int(parts[i])] = parts[i + 1]
                except ValueError:
                    pass
                    
            obj_id = int(props.get(1, 1))
            obj = LevelObject(
                obj_id=obj_id,
                x=float(props.get(2, 0.0)),
                y=float(props.get(3, 0.0))
            )
            obj.raw_props = props
            obj.flip_x = (props.get(4, "0") == "1")
            obj.flip_y = (props.get(5, "0") == "1")
            obj.rotation = float(props.get(6, 0.0))
            obj.rotation_x = float(props[131]) if 131 in props else None
            obj.rotation_y = float(props[132]) if 132 in props else None
            obj.scale = float(props.get(32, 1.0))
            obj.scale_x = float(props.get(128, 1.0))
            obj.scale_y = float(props.get(129, 1.0))
            obj.editor_layer = int(props.get(20, 0))
            
            # Lookup default properties from objects.json
            definition = obj_defs.get(str(obj_id), {})
            
            # Z Layer
            raw_zlayer = int(props.get(24, 0)) or definition.get("defaultZLayer", 3)
            obj.z_layer = ZLAYER_VALUES.get(raw_zlayer, 3)
                
            # Z Order
            if 25 in props:
                obj.z_order = int(props[25])
            else:
                obj.z_order = definition.get("defaultZOrder", 0)
                
            # Color Channels
            obj.base_color_channel = int(props.get(21, 0)) or definition.get("defaultBaseColorChannel", 1004)
            obj.detail_color_channel = int(props.get(22, 0)) or definition.get("defaultDetailColorChannel", 1)
                
            # HSV Shifts
            if props.get(41, "0") == "1":
                obj.base_hsv_enabled = True
                obj.base_hsv_shift = HSVShift.parse(props.get(43))
            if props.get(42, "0") == "1":
                obj.detail_hsv_enabled = True
                obj.detail_hsv_shift = HSVShift.parse(props.get(44))
                
            # Groups
            if 57 in props and props[57]:
                try:
                    obj.groups = [int(g) for g in props[57].split(".") if g]
                except ValueError:
                    pass
                    
            level.objects.append(obj)
            
        return level

    @staticmethod
    def _parse_plist_dictionary(node):
        """Read both Apple's long plist tags and GD's abbreviated d/k/s tags."""
        result = {}
        children = list(node)
        for index in range(0, len(children) - 1, 2):
            key, value = children[index], children[index + 1]
            if key.tag not in ("k", "key"):
                continue
            result[key.text] = (LevelParser._parse_plist_dictionary(value)
                                if value.tag in ("d", "dict") else value.text or "")
        return result

    @classmethod
    def _decode_save(cls, content):
        if content.lstrip().startswith(b"<"):
            xml = content
        elif content.startswith((b"\x1f\x8b", b"\x78")):
            xml = zlib.decompress(content, 15 | 32)
        else:
            # Cocos can persist the encoded string's trailing NUL before XOR.
            encoded = bytes(value ^ 11 for value in content).strip(b"\0 \t\r\n")
            encoded += b"=" * (-len(encoded) % 4)
            xml = zlib.decompress(base64.b64decode(encoded, altchars=b"-_", validate=True), 15 | 32)
        root = ET.fromstring(xml)
        if root.tag == "plist":
            root = next((child for child in root if child.tag in ("dict", "d")), None)
        if root is None or root.tag not in ("dict", "d"):
            raise ValueError("GD save does not contain a level dictionary")
        return cls._parse_plist_dictionary(root)

    @classmethod
    def _level_from_record(cls, record, fallback_name="Unnamed"):
        level = cls.parse_level_string(record.get("k4", ""), name=record.get("k2", fallback_name))
        level.author = record.get("k5", "")
        description = record.get("k3", "")
        if description:
            try:
                description = base64.urlsafe_b64decode(description + "=" * (-len(description) % 4)).decode("utf-8")
            except (binascii.Error, UnicodeError):
                pass  # Older saves can contain unencoded descriptions.
        level.description = description
        return level

    @classmethod
    def parse_file(cls, filepath: str) -> GDLevel:
        """Load raw text, XML/gzip GMD or an encrypted CCLocalLevels.dat save."""
        with open(filepath, "rb") as stream:
            content = stream.read()
        filename = os.path.splitext(os.path.basename(filepath))[0]
        ext = os.path.splitext(filepath)[1].lower()
        try:
            if ext == ".dat" or content.lstrip().startswith(b"<") or content.startswith(b"\x1f\x8b"):
                record = cls._decode_save(content)
                if "LLM_01" in record:
                    levels = record["LLM_01"]
                    record = next((value for value in levels.values()
                                   if isinstance(value, dict) and value.get("k4")), None)
                    if record is None:
                        raise ValueError("The save contains no nonempty editor levels")
                if "k4" not in record:
                    raise ValueError("Level dictionary is missing k4")
                return cls._level_from_record(record, filename)
            return cls.parse_level_string(content.decode("utf-8-sig"), name=filename)
        except (ValueError, UnicodeError, zlib.error, ET.ParseError) as error:
            raise ValueError(f"Could not load GD level {filepath}: {error}") from error

    @classmethod
    def list_local_levels(cls) -> List[Dict[str, Any]]:
        """List saved editor levels without treating the array marker as a level."""
        path = os.path.expandvars(r"%LOCALAPPDATA%\GeometryDash\CCLocalLevels.dat")
        if not os.path.exists(path):
            return []
        try:
            with open(path, "rb") as stream:
                saved = cls._decode_save(stream.read())
            levels = []
            for record in saved.get("LLM_01", {}).values():
                if not isinstance(record, dict) or not ("k2" in record or "k4" in record):
                    continue
                levels.append({"name": record.get("k2", "Unnamed"),
                               "author": record.get("k5", "-"),
                               "description": record.get("k3", ""),
                               "revision": record.get("k21", "0"),
                               "k4": record.get("k4", "")})
            return levels
        except (ValueError, zlib.error, ET.ParseError) as error:
            raise ValueError(f"Could not read local GD levels from {path}: {error}") from error

    @classmethod
    def load_local_level(cls, name_or_index: Any) -> Optional[GDLevel]:
        """Loads a specific level from CCLocalLevels.dat by name or index."""
        levels = cls.list_local_levels()
        if not levels:
            return None
            
        if isinstance(name_or_index, int):
            if 0 <= name_or_index < len(levels):
                lvl = levels[name_or_index]
                return cls._level_from_record({"k2": lvl["name"], "k4": lvl["k4"],
                                               "k5": lvl["author"], "k3": lvl["description"]})
            return None
            
        target = str(name_or_index).lower()
        for lvl in levels:
            if lvl["name"].lower() == target:
                return cls._level_from_record({"k2": lvl["name"], "k4": lvl["k4"],
                                               "k5": lvl["author"], "k3": lvl["description"]})
                
        # Substring match if exact match not found
        for lvl in levels:
            if target in lvl["name"].lower():
                return cls._level_from_record({"k2": lvl["name"], "k4": lvl["k4"],
                                               "k5": lvl["author"], "k3": lvl["description"]})
                
        return None
