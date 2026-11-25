"""Render static GD editor geometry with Cocos transforms and RGBA compositing."""

import math
from typing import Optional

import numpy as np
from PIL import Image, ImageDraw

from .asset_manager import AssetManager
from .color_engine import ColorEngine, apply_hsv_shift
from .geometry import object_bounds, sprite_corners
from .level_parser import GDLevel, LevelParser


class GDRenderer:
    """The same drawing implementation serves PNG exports and the viewer."""

    def __init__(self, asset_manager: Optional[AssetManager] = None, quality: str = "uhd"):
        self.asset_manager = asset_manager or AssetManager(quality=quality)
        self.color_engine = ColorEngine()
        self.object_defs = LevelParser.get_object_defs()
        self.tinted_cache = {}

    def get_tinted_sprite(self, frame_name, r, g, b, a=255, is_glow=False):
        # Glow changes compositing, not the tint itself.
        key = (frame_name, r, g, b, a)
        if key not in self.tinted_cache:
            sprite = self.asset_manager.get_sprite(frame_name)
            if sprite is None:
                return None
            self.tinted_cache[key] = self.color_engine.tint_image(sprite, r, g, b, a / 255)
        return self.tinted_cache[key]

    def _resolve_sprite_color(self, obj, color_type):
        if color_type == "black":
            return 0, 0, 0, 255
        if color_type == "white":
            return 255, 255, 255, 255
        detail = color_type == "detail"
        channel_id = obj.detail_color_channel if detail else obj.base_color_channel
        color = self.color_engine.get_color(channel_id)
        rgb = color.rgb
        enabled = obj.detail_hsv_enabled if detail else obj.base_hsv_enabled
        if enabled:
            shift = obj.detail_hsv_shift if detail else obj.base_hsv_shift
            rgb = apply_hsv_shift(*rgb, shift)
        return *rgb, round(color.opacity * 255)

    def visible_objects(self, level, x_start, x_end, y_bottom, y_top):
        visible = []
        for obj in level.objects:
            bounds = object_bounds(obj, self.object_defs.get(str(obj.id), {}))
            if bounds[0] <= x_end and bounds[2] >= x_start and bounds[1] <= y_top and bounds[3] >= y_bottom:
                visible.append(obj)
        return sorted(visible, key=lambda obj: (obj.z_layer, obj.z_order))

    def render_range(self, level: GDLevel, x_start, x_end, y_bottom=0.0, y_top=600.0,
                     scale=1.0, draw_bg=True, draw_ground=True, draw_grid=False,
                     draw_glow=True, antialias=2, preview_x=None):
        """Render world bounds. Static editor colors are used by default.

        antialias is the internal sampling multiplier (1, 2 or 4).
        preview_x explicitly opts into an instantaneous color-trigger preview;
        it does not simulate timed, spawn, move, pulse or shader effects.
        """
        if not all(math.isfinite(value) for value in (x_start, x_end, y_bottom, y_top, scale)):
            raise ValueError("Viewport coordinates and scale must be finite")
        if x_end <= x_start or y_top <= y_bottom or scale <= 0:
            raise ValueError("Viewport dimensions and scale must be positive")
        if antialias not in (1, 2, 4):
            raise ValueError("antialias must be 1, 2 or 4")

        self.color_engine.reset_defaults()
        self.color_engine.parse_header_colors(level.ks38)
        if preview_x is not None:
            self.color_engine.apply_color_triggers(level.objects, at_x=preview_x)

        size = (max(1, round((x_end - x_start) * scale)),
                max(1, round((y_top - y_bottom) * scale)))
        # Lanczos needs neighboring samples outside the requested viewport.
        # Render a halo so a crop matches the same region of a larger export.
        halo = 3 if antialias > 1 else 0
        render_left, render_top = x_start - halo / scale, y_top + halo / scale
        render_right = x_start + (size[0] + halo) / scale
        render_bottom = y_top - (size[1] + halo) / scale
        sample_scale = scale * antialias
        sample_size = ((size[0] + 2 * halo) * antialias, (size[1] + 2 * halo) * antialias)
        canvas = self._draw_background(level, sample_size, render_left, render_top, sample_scale, draw_bg)
        if draw_grid:
            self._draw_grid(canvas, render_left, render_right, render_bottom, render_top, sample_scale, antialias)

        visible = self.visible_objects(level, render_left, render_right, render_bottom, render_top)
        for obj in visible:
            if obj.z_layer < 4:
                self._draw_object(canvas, obj, render_left, render_top, sample_scale, draw_glow)
        if draw_ground and render_bottom < 92:
            self._draw_ground(canvas, level.ground_id, render_left, render_top, sample_scale)
        for obj in visible:
            if obj.z_layer >= 4:
                self._draw_object(canvas, obj, render_left, render_top, sample_scale, draw_glow)
        if antialias > 1:
            canvas = canvas.resize((size[0] + 2 * halo, size[1] + 2 * halo), Image.Resampling.LANCZOS)
            canvas = canvas.crop((halo, halo, halo + size[0], halo + size[1]))
        return canvas

    def _draw_background(self, level, size, x_start, y_top, scale, enabled):
        if not enabled:
            return Image.new("RGBA", size)
        color = self.color_engine.get_color(ColorEngine.CHANNEL_BG)
        canvas = Image.new("RGBA", size, (*color.rgb, 255))
        background = self.asset_manager.get_background(level.bg_id)
        if background is not None:
            tinted = self.color_engine.tint_image(background, *color.rgb)
            tile_size = max(1, round(512 * scale))
            tile = tinted.resize((tile_size, tile_size), Image.Resampling.BILINEAR)
            # Anchor the tiles in world space, including vertical camera movement.
            offset_x = -round((x_start % 512) * scale)
            offset_y = round((y_top - 90) * scale) % tile_size - tile_size
            for x in range(offset_x, canvas.width, tile_size):
                for y in range(offset_y, canvas.height, tile_size):
                    canvas.alpha_composite(tile, (x, y))
        return canvas

    @staticmethod
    def _draw_grid(canvas, x_start, x_end, y_bottom, y_top, scale, line_width):
        overlay = Image.new("RGBA", canvas.size)
        draw = ImageDraw.Draw(overlay)
        for x in range(math.ceil(x_start / 30), math.floor(x_end / 30) + 1):
            px = round((x * 30 - x_start) * scale)
            draw.line((px, 0, px, canvas.height), fill=(255, 255, 255, 40), width=line_width)
        for y in range(math.ceil(y_bottom / 30), math.floor(y_top / 30) + 1):
            py = round((y_top - y * 30) * scale)
            draw.line((0, py, canvas.width, py), fill=(255, 255, 255, 40), width=line_width)
        canvas.alpha_composite(overlay)

    def _draw_ground(self, canvas, ground_id, x_start, y_top, scale):
        ground, line = self.asset_manager.get_ground(ground_id)
        ground_y = round((y_top - 90) * scale)
        tile_size = max(1, round(128 * scale))
        offset_x = -round((x_start % 128) * scale)
        if ground is not None and ground_y < canvas.height:
            color = self.color_engine.get_color(ColorEngine.CHANNEL_G1)
            tile = self.color_engine.tint_image(ground, *color.rgb).resize(
                (tile_size, tile_size), Image.Resampling.BILINEAR)
            first_y = ground_y if ground_y >= 0 else ground_y % tile_size - tile_size
            for x in range(offset_x, canvas.width, tile_size):
                for y in range(first_y, canvas.height, tile_size):
                    canvas.alpha_composite(tile, (x, y))
        if line is None:
            line = self.asset_manager.get_sprite("floorLine_001.png")
        line_height = max(1, round(4 * scale))
        if line is not None and -line_height < ground_y < canvas.height + line_height:
            color = self.color_engine.get_color(ColorEngine.CHANNEL_LINE)
            tile = self.color_engine.tint_image(line, *color.rgb).resize(
                (tile_size, line_height), Image.Resampling.BILINEAR)
            for x in range(offset_x, canvas.width, tile_size):
                canvas.alpha_composite(tile, (x, ground_y - line_height // 2))

    def _draw_object(self, canvas, obj, cam_x, cam_top_y, scale, draw_glow=True):
        sprites = self.object_defs.get(str(obj.id), {}).get("sprites", [])
        # Keep database order within each group; glow goes behind its object.
        for sprite in sorted(sprites, key=lambda sprite: sprite.get("colorType") != "glow"):
            color_type = sprite.get("colorType", "base")
            is_glow = color_type == "glow"
            if is_glow and (not draw_glow or obj.raw_props.get(96) == "1"):
                continue
            texture = sprite.get("texture")
            if not texture:
                continue
            rgba = self._resolve_sprite_color(obj, color_type)
            if rgba[3] <= 0:
                continue
            corners = [((x - cam_x) * scale, (cam_top_y - y) * scale)
                       for x, y in sprite_corners(obj, sprite)]
            image = self.get_tinted_sprite(texture, *rgba)
            if image is None:
                continue
            transformed = self._transform_sprite(image, corners, canvas.size)
            if transformed is None:
                continue
            tile, x, y = transformed
            channel_id = obj.detail_color_channel if color_type == "detail" else obj.base_color_channel
            additive = is_glow or (color_type not in ("black", "white") and
                                    self.color_engine.get_color(channel_id).blending)
            if additive:
                self._blend_additive(canvas, tile, x, y)
            else:
                canvas.alpha_composite(tile, (x, y))

    @staticmethod
    def _transform_sprite(image, corners, canvas_size):
        """Sample the full affine parallelogram without rounding its world geometry."""
        px, py = corners[0]
        ux = (corners[1][0] - px) / image.width
        uy = (corners[1][1] - py) / image.width
        vx = (corners[3][0] - px) / image.height
        vy = (corners[3][1] - py) / image.height
        determinant = ux * vy - uy * vx
        if abs(determinant) < 1e-12:
            return None
        xs, ys = zip(*corners)
        left, top = max(0, math.floor(min(xs))), max(0, math.floor(min(ys)))
        right, bottom = min(canvas_size[0], math.ceil(max(xs))), min(canvas_size[1], math.ceil(max(ys)))
        if right <= left or bottom <= top:
            return None
        a, b, d, e = vy / determinant, -vx / determinant, -uy / determinant, ux / determinant
        coefficients = (a, b, a * (left - px) + b * (top - py),
                        d, e, d * (left - px) + e * (top - py))
        tile = image.transform((right - left, bottom - top), Image.Transform.AFFINE,
                               coefficients, resample=Image.Resampling.BILINEAR)
        return tile, left, top

    @staticmethod
    def _blend_additive(canvas, sprite, x, y):
        """Add premultiplied RGB; retain coverage when exporting a transparent PNG."""
        left, top = max(0, x), max(0, y)
        right, bottom = min(canvas.width, x + sprite.width), min(canvas.height, y + sprite.height)
        if right <= left or bottom <= top:
            return
        dest = np.asarray(canvas.crop((left, top, right, bottom)), dtype=np.float32) / 255
        source = np.asarray(sprite.crop((left - x, top - y, right - x, bottom - y)), dtype=np.float32) / 255
        src_alpha, dest_alpha = source[:, :, 3:4], dest[:, :, 3:4]
        alpha = src_alpha + dest_alpha * (1 - src_alpha)
        premultiplied = dest[:, :, :3] * dest_alpha + source[:, :, :3] * src_alpha
        rgb = np.divide(premultiplied, alpha, out=np.zeros_like(premultiplied), where=alpha > 0)
        result = np.concatenate((np.clip(rgb, 0, 1), alpha), axis=2)
        canvas.paste(Image.fromarray(np.rint(result * 255).astype(np.uint8)), (left, top))
