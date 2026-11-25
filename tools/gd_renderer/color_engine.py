"""
Geometry Dash Color Engine
Handles channel colors, header kS38 decoding, copy colors, HSV shifts,
and accurate sprite tinting with alpha & additive blending.
"""

from typing import Dict, Optional, Tuple, NamedTuple
import numpy as np
from PIL import Image

class HSVShift(NamedTuple):
    hue: float = 0.0
    sat: float = 1.0
    val: float = 1.0
    sat_add: bool = False
    val_add: bool = False

    @staticmethod
    def parse(hsv_str: Optional[str]) -> "HSVShift":
        if not hsv_str:
            return HSVShift()
        try:
            parts = hsv_str.split("a")
            if len(parts) >= 5:
                return HSVShift(
                    hue=float(parts[0]),
                    sat=float(parts[1]),
                    val=float(parts[2]),
                    sat_add=(int(parts[3]) == 1),
                    val_add=(int(parts[4]) == 1)
                )
        except Exception:
            pass
        return HSVShift()

    def is_empty(self) -> bool:
        return (
            self.hue == 0.0 and
            abs(self.sat - (0.0 if self.sat_add else 1.0)) < 0.001 and
            abs(self.val - (0.0 if self.val_add else 1.0)) < 0.001
        )

def rgb_to_hsv(r: float, g: float, b: float) -> Tuple[float, float, float]:
    """Converts 0..1 RGB to (H: 0..360, S: 0..1, V: 0..1)."""
    mx = max(r, g, b)
    mn = min(r, g, b)
    df = mx - mn
    if mx == mn:
        h = 0.0
    elif mx == r:
        h = (60 * ((g - b) / df) + 360) % 360
    elif mx == g:
        h = (60 * ((b - r) / df) + 120) % 360
    elif mx == b:
        h = (60 * ((r - g) / df) + 240) % 360
    else:
        h = 0.0
    s = 0.0 if mx == 0 else (df / mx)
    v = mx
    return h, s, v

def hsv_to_rgb(h: float, s: float, v: float) -> Tuple[float, float, float]:
    """Converts (H: 0..360, S: 0..1, V: 0..1) to 0..1 RGB."""
    h = h % 360
    c = v * s
    x = c * (1 - abs((h / 60) % 2 - 1))
    m = v - c
    if 0 <= h < 60:
        r, g, b = c, x, 0.0
    elif 60 <= h < 120:
        r, g, b = x, c, 0.0
    elif 120 <= h < 180:
        r, g, b = 0.0, c, x
    elif 180 <= h < 240:
        r, g, b = 0.0, x, c
    elif 240 <= h < 300:
        r, g, b = x, 0.0, c
    else:
        r, g, b = c, 0.0, x
    return r + m, g + m, b + m

def apply_hsv_shift(r: int, g: int, b: int, shift: HSVShift) -> Tuple[int, int, int]:
    """Applies an HSV shift to an integer RGB color (0..255)."""
    if shift.is_empty():
        return r, g, b
    
    h, s, v = rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    h = (h + shift.hue) % 360
    if shift.sat_add:
        s = max(0.0, min(1.0, s + shift.sat))
    else:
        s = max(0.0, min(1.0, s * shift.sat))
        
    if shift.val_add:
        v = max(0.0, min(1.0, v + shift.val))
    else:
        v = max(0.0, min(1.0, v * shift.val))
        
    nr, ng, nb = hsv_to_rgb(h, s, v)
    return int(round(nr * 255)), int(round(ng * 255)), int(round(nb * 255))


class ChannelColor:
    def __init__(self, r: int = 255, g: int = 255, b: int = 255,
                 opacity: float = 1.0, blending: bool = False,
                 copy_id: int = 0, copy_hsv: Optional[HSVShift] = None,
                 copy_opacity: bool = False):
        self.r = r
        self.g = g
        self.b = b
        self.opacity = opacity
        self.blending = blending
        self.copy_id = copy_id
        self.copy_hsv = copy_hsv or HSVShift()
        self.copy_opacity = copy_opacity

    @property
    def rgb(self) -> Tuple[int, int, int]:
        return (self.r, self.g, self.b)

    @property
    def rgba(self) -> Tuple[int, int, int, int]:
        return (self.r, self.g, self.b, int(round(self.opacity * 255)))


class ColorEngine:
    """Manages level color channels and computes object tinting."""
    
    # Special channels in Geometry Dash
    CHANNEL_BG = 1000
    CHANNEL_G1 = 1001
    CHANNEL_LINE = 1002
    CHANNEL_3DL = 1003
    CHANNEL_OBJ = 1004
    CHANNEL_P1 = 1005
    CHANNEL_P2 = 1006
    CHANNEL_LBG = 1007
    CHANNEL_G2 = 1009
    CHANNEL_BLACK = 1010
    CHANNEL_WHITE = 1011

    def __init__(self):
        self.channels: Dict[int, ChannelColor] = {}
        self.reset_defaults()

    def reset_defaults(self):
        """Sets standard Geometry Dash default channel colors."""
        self.channels = {
            self.CHANNEL_BG: ChannelColor(40, 125, 255),       # Classic blue
            self.CHANNEL_G1: ChannelColor(0, 102, 255),        # Classic darker blue
            self.CHANNEL_LINE: ChannelColor(255, 255, 255),    # White line
            self.CHANNEL_3DL: ChannelColor(255, 255, 255),
            self.CHANNEL_OBJ: ChannelColor(255, 255, 255),     # White default for objects
            self.CHANNEL_P1: ChannelColor(125, 255, 0),        # Player primary (lime)
            self.CHANNEL_P2: ChannelColor(0, 255, 255),        # Player secondary (cyan)
            self.CHANNEL_LBG: ChannelColor(0, 0, 0),
            self.CHANNEL_G2: ChannelColor(0, 102, 255),
            self.CHANNEL_BLACK: ChannelColor(0, 0, 0),
            self.CHANNEL_WHITE: ChannelColor(255, 255, 255),
        }
        # Channels 1-999 default to pure white
        for c in range(1, 1000):
            self.channels[c] = ChannelColor(255, 255, 255)

    def parse_header_colors(self, ks38_str: str):
        """Parses the kS38 property string from a Geometry Dash level header."""
        if not ks38_str:
            return
            
        for channel_str in ks38_str.split("|"):
            if not channel_str:
                continue
            parts = channel_str.split("_")
            props: Dict[int, str] = {}
            for i in range(0, len(parts) - 1, 2):
                try:
                    props[int(parts[i])] = parts[i + 1]
                except ValueError:
                    pass
                    
            channel_id = int(props.get(6, 1))
            r = int(props.get(1, 255))
            g = int(props.get(2, 255))
            b = int(props.get(3, 255))
            opacity = float(props.get(7, 1.0))
            blending = (props.get(5, "0") == "1")
            copy_id = int(props.get(9, 0))
            copy_hsv = HSVShift.parse(props.get(10))
            
            # Player color references
            player_color = int(props.get(4, -1))
            if player_color == 1:
                copy_id = self.CHANNEL_P1
            elif player_color == 2:
                copy_id = self.CHANNEL_P2
                
            self.channels[channel_id] = ChannelColor(
                r=r, g=g, b=b,
                opacity=max(0.0, min(1.0, opacity)),
                blending=blending,
                copy_id=copy_id,
                copy_hsv=copy_hsv,
                copy_opacity=(props.get(17, "0") == "1")
            )
            
        self._resolve_copied_channels()

    def _resolve_copied_channels(self):
        """Resolve dependencies in order, applying every link's HSV and opacity.

        Use an iterative walk so long copy chains cannot overflow Python's stack.
        Channels in a cycle retain their explicit RGB values.
        """
        resolved = set()
        for channel_id in self.channels:
            path = []
            visiting = set()
            current = channel_id
            while current in self.channels and current not in resolved:
                if current in visiting:
                    cycle_start = path.index(current)
                    resolved.update(path[cycle_start:])
                    path = path[:cycle_start]
                    break
                visiting.add(current)
                path.append(current)
                target = self.channels[current].copy_id
                if not target:
                    break
                current = target
            for current in reversed(path):
                color = self.channels[current]
                target = self.channels.get(color.copy_id)
                if target is not None and color.copy_id != current:
                    color.r, color.g, color.b = apply_hsv_shift(*target.rgb, color.copy_hsv)
                    if color.copy_opacity:
                        color.opacity = target.opacity
                resolved.add(current)

    def apply_color_triggers(self, triggers: list, at_x: float):
        """Apply positional color triggers instantly; duration is not simulated."""
        legacy_targets = {29: self.CHANNEL_BG, 30: self.CHANNEL_G1,
                          104: self.CHANNEL_LINE, 105: self.CHANNEL_OBJ,
                          221: 1, 717: 2, 718: 3, 743: 4, 744: self.CHANNEL_3DL}
        sorted_triggers = sorted(
            [t for t in triggers if t.id in (29, 30, 104, 105, 221, 717, 718, 743, 744, 899)
             and t.x <= at_x and t.raw_props.get(62) != "1" and t.raw_props.get(87) != "1"],
            key=lambda t: t.x
        )
        for t in sorted_triggers:
            props = t.raw_props
            target_id = legacy_targets.get(t.id, int(props.get(23, 1)))
                
            r = int(props.get(7, 255))
            g = int(props.get(8, 255))
            b = int(props.get(9, 255))
            opacity = max(0.0, min(1.0, float(props.get(35, 1.0))))
            blending = (props.get(48, "0") == "1")
            copy_id = int(props.get(52, 0))
            copy_hsv = HSVShift.parse(props.get(53))
            
            # Apply to channel
            if target_id not in self.channels:
                self.channels[target_id] = ChannelColor(r, g, b, opacity, blending, copy_id, copy_hsv)
            else:
                ch = self.channels[target_id]
                ch.r = r
                ch.g = g
                ch.b = b
                ch.opacity = opacity
                ch.blending = blending
                ch.copy_id = copy_id
                ch.copy_hsv = copy_hsv
            self.channels[target_id].copy_opacity = props.get(60) == "1"
                
        self._resolve_copied_channels()

    def get_color(self, channel_id: int) -> ChannelColor:
        """Returns the ChannelColor for given channel ID."""
        return self.channels.get(channel_id, ChannelColor(255, 255, 255))

    def tint_image(self, img: Image.Image, r: int, g: int, b: int,
                   opacity: float = 1.0, is_glow: bool = False) -> Image.Image:
        """
        Tints an RGBA PIL Image accurately using NumPy vector operations.
        If r=g=b=255 and opacity=1.0, returns the original image.
        """
        if img is None:
            return None
        if r == 255 and g == 255 and b == 255 and opacity >= 0.999:
            return img
            
        arr = np.array(img, dtype=np.float32)
        if arr.size == 0:
            return img
            
        # Multiply RGB
        arr[:, :, 0] *= (r / 255.0)
        arr[:, :, 1] *= (g / 255.0)
        arr[:, :, 2] *= (b / 255.0)
        
        # Multiply Alpha
        if opacity < 0.999:
            arr[:, :, 3] *= opacity
            
        arr = np.rint(np.clip(arr, 0, 255)).astype(np.uint8)
        return Image.fromarray(arr)
