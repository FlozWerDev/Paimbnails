"""Renderer regressions; optionally exercise a real saved editor level.

python tests/gd_renderer_regression.py --level "gemini here" --out build-regressions/gd-renderer
"""

import argparse
import base64
import gzip
import importlib
import json
import math
import os
import plistlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from PIL import Image
from tools.gd_renderer import AssetManager, ColorEngine, GDLevel, GDRenderer, LevelObject, LevelParser
from tools.gd_renderer.geometry import sprite_corners
from image_to_editor_visual_regression import compare_pixels, interior_color_defects


class MemoryAssets:
    """Small textures make pixel assertions independent of the game installation."""

    def __init__(self, sprites=None):
        self.sprites = sprites or {"solid.png": Image.new("RGBA", (30, 30), "white")}

    def get_sprite(self, name):
        return self.sprites.get(name)

    def get_background(self, background_id):
        return Image.new("RGBA", (8, 8), "white")

    def get_ground(self, ground_id):
        return None, None


def renderer_with_sprite(texture="solid.png", **sprite_properties):
    renderer = GDRenderer(asset_manager=MemoryAssets())
    renderer.object_defs = {"211": {"sprites": [{
        "texture": texture, "position": [-15, -15], "contentSize": [30, 30],
        "colorType": "base", **sprite_properties,
    }]}}
    return renderer


def render_objects(renderer, objects, **options):
    level = GDLevel()
    level.objects = objects
    return renderer.render_range(level, 0, 120, 0, 120, draw_bg=False,
                                 draw_ground=False, **options)


class ParserRegression(unittest.TestCase):
    def test_independent_axis_rotations_are_preserved(self):
        obj = LevelParser.parse_level_string("kA6,1;1,211,131,25,132,-35;").objects[0]
        self.assertEqual(obj.rotation_x, 25)
        self.assertEqual(obj.rotation_y, -35)

    def test_b5_sorts_behind_b4(self):
        level = LevelParser.parse_level_string("kA6,1;1,211,24,-5;1,211,24,-3;")
        self.assertLess(level.objects[0].z_layer, level.objects[1].z_layer)

    def test_zero_uses_object_default_layer_and_color(self):
        level = LevelParser.parse_level_string("kA6,1;1,211;1,211,24,0,21,0,22,0;")
        default, explicit = level.objects
        self.assertEqual(default.z_layer, explicit.z_layer)
        self.assertEqual(default.base_color_channel, explicit.base_color_channel)
        self.assertEqual(default.detail_color_channel, explicit.detail_color_channel)

    def test_headerless_string_keeps_first_object(self):
        level = LevelParser.parse_level_string("1,211,2,60,3,60;1,5,2,80,3,80;")
        self.assertEqual([obj.id for obj in level.objects], [211, 5])

    def test_compressed_level_matches_plain_level(self):
        plain = "kA6,1;1,211,2,60,3,60;"
        encoded = base64.urlsafe_b64encode(gzip.compress(plain.encode())).decode().rstrip("=")
        self.assertEqual(LevelParser.parse_level_string(encoded).objects[0].x, 60)

    def test_bounds_include_scaled_sprite_extents(self):
        level = LevelParser.parse_level_string("kA6,1;1,211,2,60,3,60,128,4,129,2;")
        self.assertEqual(level.get_bounds(), (0, 30, 120, 90))

    def test_invalid_level_data_reports_an_error(self):
        with self.assertRaises(ValueError):
            LevelParser.parse_level_string("this is not a GD level")

    def test_encrypted_dat_file_loads_the_first_real_level(self):
        plain = "kA6,1;1,211,2,60,3,60;"
        record = {"k2": "fixture", "k4": plain, "k5": "tester"}
        xml = plistlib.dumps({"LLM_01": {"_isArr": True, "k_0": record}})
        encoded = base64.urlsafe_b64encode(gzip.compress(xml))
        encrypted = bytes(value ^ 11 for value in encoded)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "CCLocalLevels.dat"
            path.write_bytes(encrypted)
            level = LevelParser.parse_file(str(path))
            self.assertEqual((level.name, level.author, level.object_count), ("fixture", "tester", 1))

    def test_gzip_gmd_preserves_author_and_level_name(self):
        xml = plistlib.dumps({"k2": "fixture", "k4": "kA6,1;1,211;", "k5": "tester"})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.gmd"
            path.write_bytes(gzip.compress(xml))
            level = LevelParser.parse_file(str(path))
            self.assertEqual((level.name, level.author, level.object_count), ("fixture", "tester", 1))

    def test_encrypted_dat_accepts_the_cocos_null_terminator(self):
        xml = plistlib.dumps({"LLM_01": {"k_0": {"k2": "terminated", "k4": "1,211;"}}})
        encoded = base64.urlsafe_b64encode(gzip.compress(xml)) + b"\0"
        encrypted = bytes(value ^ 11 for value in encoded)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "CCLocalLevels.dat"
            path.write_bytes(encrypted)
            level = LevelParser.parse_file(str(path))
            self.assertEqual((level.name, level.object_count), ("terminated", 1))


class GeometryRegression(unittest.TestCase):
    def test_negative_child_scale_preserves_its_center(self):
        sprite = {"position": [15, -15], "contentSize": [30, 30], "scale": [-1, 1]}
        corners = sprite_corners(LevelObject(211, 60, 60), sprite)
        self.assertEqual(corners, [(75, 75), (45, 75), (45, 45), (75, 45)])

    def test_nonuniform_parent_scale_is_applied_after_child_rotation(self):
        obj = LevelObject(211)
        obj.scale_x, obj.scale_y = 2, 1
        sprite = {"position": [-5, 10], "contentSize": [20, 10], "rotation": 90}
        corners = sprite_corners(obj, sprite)
        expected = [(10, 10), (10, -10), (-10, -10), (-10, 10)]
        for actual, target in zip(corners, expected):
            self.assertAlmostEqual(actual[0], target[0])
            self.assertAlmostEqual(actual[1], target[1])


class AssetRegression(unittest.TestCase):
    @staticmethod
    def write_atlas(directory, suffix, texture, color):
        atlas_path = Path(directory) / ("test" + suffix)
        Image.new("RGBA", (4, 4), color).save(atlas_path.with_suffix(".png"))
        frame = {"textureRect": "{{0,0},{4,4}}", "spriteSize": "{4,4}",
                 "spriteSourceSize": "{4,4}", "spriteOffset": "{0,0}", "textureRotated": False}
        with atlas_path.with_suffix(".plist").open("wb") as stream:
            plistlib.dump({"frames": {texture: frame}}, stream)

    def test_missing_uhd_sheet_falls_back_per_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            self.write_atlas(directory, "-uhd", "uhd.png", "red")
            self.write_atlas(directory, "-hd", "hd.png", "green")
            assets = AssetManager(directory, "uhd")
            self.assertIsNotNone(assets.get_sprite("uhd.png"))
            self.assertIsNotNone(assets.get_sprite("hd.png"))

    def test_sd_resources_work_when_no_high_quality_sheet_exists(self):
        with tempfile.TemporaryDirectory() as directory:
            self.write_atlas(directory, "", "sd.png", "red")
            Image.new("RGBA", (4, 4), "blue").save(Path(directory) / "game_bg_01_001.png")
            assets = AssetManager(directory, "uhd")
            self.assertIsNotNone(assets.get_sprite("sd.png"))
            self.assertIsNotNone(assets.get_background(0))

    def test_rotated_frame_is_unpacked_and_offset_is_not_applied_twice(self):
        with tempfile.TemporaryDirectory() as directory:
            original = Image.new("RGBA", (2, 3))
            original.putdata([(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255),
                              (255, 255, 0, 255), (0, 255, 255, 255), (255, 0, 255, 255)])
            original.transpose(Image.Transpose.ROTATE_270).save(Path(directory) / "test-uhd.png")
            frame = {"textureRect": "{{0,0},{2,3}}", "spriteSize": "{2,3}",
                     "spriteSourceSize": "{6,7}", "spriteOffset": "{1,-1}", "textureRotated": True}
            with (Path(directory) / "test-uhd.plist").open("wb") as stream:
                plistlib.dump({"frames": {"rotated.png": frame}}, stream)
            sprite = AssetManager(directory).get_sprite("rotated.png")
            self.assertEqual(sprite.size, (6, 7))
            self.assertEqual(sprite.crop((3, 3, 5, 6)).tobytes(), original.tobytes())

    def test_invalid_explicit_resources_path_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileNotFoundError):
                AssetManager(str(Path(directory) / "missing"))

    def test_half_pixel_offsets_are_parsed_as_two_numbers(self):
        from tools.gd_renderer.asset_manager import SpriteFrameInfo
        frame = SpriteFrameInfo("test.plist", "test.png", {
            "textureRect": "{{0,0},{2,2}}", "spriteSize": "{2,2}",
            "spriteSourceSize": "{3,3}", "spriteOffset": "{-0.5,0.5}"})
        self.assertEqual((frame.offset_x, frame.offset_y), (-0.5, 0.5))

    def test_cocos_format_two_frames_can_be_loaded(self):
        with tempfile.TemporaryDirectory() as directory:
            Image.new("RGBA", (4, 4), "red").save(Path(directory) / "test-uhd.png")
            frame = {"frame": "{{0,0},{4,4}}", "offset": "{0,0}", "rotated": False,
                     "sourceColorRect": "{{0,0},{4,4}}", "sourceSize": "{4,4}"}
            with (Path(directory) / "test-uhd.plist").open("wb") as stream:
                plistlib.dump({"frames": {"format_two.png": frame}}, stream)
            self.assertEqual(AssetManager(directory).get_sprite("format_two.png").getpixel((0, 0)),
                             (255, 0, 0, 255))


class RenderingRegression(unittest.TestCase):
    def test_texture_alpha_is_preserved_for_square_frame(self):
        renderer = renderer_with_sprite("square_05_001.png")
        renderer.asset_manager.sprites["square_05_001.png"] = Image.new("RGBA", (30, 30), (255, 255, 255, 64))
        image = render_objects(renderer, [LevelObject(211, 60, 60)])
        self.assertEqual(image.getpixel((60, 60))[3], 64)

    def test_translucent_sprite_composites_without_squaring_alpha(self):
        renderer = renderer_with_sprite()
        renderer.asset_manager.sprites["solid.png"] = Image.new("RGBA", (30, 30), (255, 0, 0, 128))
        image = render_objects(renderer, [LevelObject(211, 60, 60)])
        self.assertEqual(image.getpixel((60, 60)), (255, 0, 0, 128))

    def test_additive_channel_blends_even_without_glow_texture(self):
        renderer = renderer_with_sprite()
        level = GDLevel()
        level.ks38 = "1_80_2_60_3_40_6_1_5_1_7_0.5|"
        obj = LevelObject(211, 60, 60)
        obj.base_color_channel = 1
        level.objects = [obj]
        image = renderer.render_range(level, 0, 120, 0, 120, draw_ground=False)
        self.assertEqual(image.getpixel((60, 60))[:3], (80, 155, 255))
        self.assertEqual(image.getpixel((60, 60))[3], 255)

    def test_additive_sprite_remains_visible_on_transparent_export(self):
        renderer = renderer_with_sprite(colorType="glow")
        image = render_objects(renderer, [LevelObject(211, 60, 60)])
        self.assertEqual(image.getpixel((60, 60)), (255, 255, 255, 255))

    def test_independent_rotations_change_geometry(self):
        renderer = renderer_with_sprite()
        level = LevelParser.parse_level_string("kA6,1;1,211,2,60,3,60,131,0,132,60;")
        image = render_objects(renderer, level.objects)
        self.assertGreater(image.getpixel((60, 50))[3], 200)
        self.assertEqual(image.getpixel((72, 60))[3], 0)

    def test_large_object_intersecting_viewport_is_not_culled(self):
        renderer = renderer_with_sprite()
        obj = LevelObject(211, 300, 60)
        obj.scale_x = 20
        image = render_objects(renderer, [obj])
        self.assertEqual(image.getpixel((60, 60))[3], 255)

    def test_zero_scale_does_not_leave_a_one_pixel_sprite(self):
        renderer = renderer_with_sprite()
        obj = LevelObject(211, 60, 60)
        obj.scale_x = 0
        self.assertIsNone(render_objects(renderer, [obj]).getbbox())

    def test_glow_can_be_disabled_in_export(self):
        renderer = renderer_with_sprite(colorType="glow")
        image = render_objects(renderer, [LevelObject(211, 60, 60)], draw_glow=False)
        self.assertIsNone(image.getbbox())

    def test_grid_does_not_make_opaque_background_translucent(self):
        image = GDRenderer(asset_manager=MemoryAssets()).render_range(
            GDLevel(), 0, 120, 0, 120, draw_ground=False, draw_grid=True)
        self.assertEqual(image.getpixel((30, 30))[3], 255)

    def test_invalid_viewport_is_rejected(self):
        renderer = renderer_with_sprite()
        for options in ({"scale": 0}, {"scale": float("nan")}, {"x_end": -1}, {"y_top": 0}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                renderer.render_range(GDLevel(), **{
                    "x_start": 0, "x_end": 120, "y_bottom": 0, "y_top": 120, **options})


class ColorRegression(unittest.TestCase):
    def test_copy_chain_applies_each_hsv_and_copy_opacity(self):
        engine = ColorEngine()
        engine.parse_header_colors(
            "1_200_2_100_3_50_6_1_7_0.4|"
            "6_2_9_1_10_0a1a0.5a0a0_7_0.8|"
            "6_3_9_2_10_0a1a0.5a0a0_17_1|")
        self.assertEqual(engine.get_color(3).rgb, (50, 25, 12))
        self.assertEqual(engine.get_color(3).opacity, 0.8)

    def test_editor_render_keeps_header_colors_until_preview_is_requested(self):
        renderer = renderer_with_sprite()
        level = LevelParser.parse_level_string(
            "kS38,1_255_2_0_3_0_6_1|;1,211,2,60,3,60,21,1;"
            "1,899,2,20,23,1,7,0,8,255,9,0;")
        image = renderer.render_range(level, 0, 120, 0, 120, draw_bg=False, draw_ground=False)
        self.assertEqual(image.getpixel((60, 60))[:3], (255, 0, 0))

    def test_color_copy_cycle_is_stable(self):
        engine = ColorEngine()
        engine.parse_header_colors("1_200_6_1_9_2|1_50_6_2_9_1|")
        self.assertEqual(engine.get_color(1).r, 200)
        self.assertEqual(engine.get_color(2).r, 50)

    def test_preview_skips_spawn_and_touch_triggers(self):
        engine = ColorEngine()
        level = LevelParser.parse_level_string(
            "kA6,1;1,899,2,10,23,1,7,255,8,0,9,0,87,1;"
            "1,899,2,20,23,1,7,0,8,255,9,0,62,1;")
        engine.apply_color_triggers(level.objects, 100)
        self.assertEqual(engine.get_color(1).rgb, (255, 255, 255))


class EntryPointRegression(unittest.TestCase):
    def test_headless_import_does_not_load_gui_dependencies(self):
        result = subprocess.run([sys.executable, "-c",
            "import sys; from tools.gd_renderer import GDRenderer; "
            "assert 'pygame' not in sys.modules; assert 'tkinter' not in sys.modules"],
            cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_cli_uses_resources_and_automatic_vertical_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            AssetRegression.write_atlas(directory, "-uhd", "block005b_05_001.png", "white")
            level_path, output = directory / "high.level", directory / "render.png"
            level_path.write_text("kA6,1;1,211,2,60,3,900;", encoding="utf-8")
            result = subprocess.run([sys.executable, str(ROOT / "tools/gd_renderer/main.py"),
                "--file", str(level_path), "--resources", str(directory), "--render",
                "--no-bg", "--no-ground", "--out", str(output)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with Image.open(output) as image:
                self.assertEqual(image.size, (150, 975))
                self.assertIsNotNone(image.getbbox())


class ConversionInteriorRegression(unittest.TestCase):
    def test_wrong_color_in_opaque_region_is_detected(self):
        expected = Image.new("RGBA", (24, 24), (237, 209, 182, 255))
        rendered = expected.copy()
        rendered.putpixel((12, 12), (35, 35, 39, 255))
        with tempfile.TemporaryDirectory() as directory:
            grid = Path(directory) / "grid.png"
            expected.save(grid)
            self.assertEqual(compare_pixels(expected, rendered)["uncovered_interior_pixels"], 0)
            self.assertEqual(interior_color_defects(grid, rendered), 1)

    def test_antialiased_boundaries_are_not_interior_defects(self):
        expected = Image.new("RGBA", (24, 24), (237, 209, 182, 255))
        expected.paste((35, 35, 39, 255), (12, 0, 24, 24))
        rendered = expected.copy()
        rendered.putpixel((11, 12), (136, 122, 110, 255))
        rendered.putpixel((0, 12), (237, 209, 182, 128))
        with tempfile.TemporaryDirectory() as directory:
            grid = Path(directory) / "grid.png"
            expected.save(grid)
            self.assertEqual(interior_color_defects(grid, rendered), 0)


class ViewerRegression(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec("pygame") and importlib.util.find_spec("tkinter"),
                         "The interactive viewer requires pygame and tkinter")
    def test_viewer_matches_export_and_reuses_idle_frame(self):
        with patch.dict(os.environ, {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy",
                                     "PYGAME_HIDE_SUPPORT_PROMPT": "1"}):
            from tools.gd_renderer import viewer
            level = LevelParser.parse_level_string("kA6,1;1,211,2,60,3,60,131,20,132,-10;")
            assets = MemoryAssets({"block005b_05_001.png": Image.new("RGBA", (30, 30), "white")})
            with patch.object(viewer, "AssetManager", return_value=assets):
                window = viewer.GDViewer(level)
            try:
                window.show_hud = False
                window.notification_time = 0
                window.draw()
                first_frame = window._frame_surface
                left, top = window.screen_to_world(0, 0)
                right, bottom = window.screen_to_world(window.width, window.height)
                expected = window.renderer.render_range(level, left, right, bottom, top,
                    draw_grid=True, draw_ground=True, scale=window.zoom).convert("RGB")
                self.assertEqual(viewer.pygame.image.tobytes(window.screen, "RGB"), expected.tobytes())
                window.draw()
                self.assertIs(window._frame_surface, first_frame)
                window.draw_glow = False
                window.draw()
                self.assertIsNot(window._frame_surface, first_frame)
            finally:
                viewer.pygame.quit()


def exercise_local_level(name, output):
    level = LevelParser.load_local_level(name)
    if level is None:
        raise AssertionError(f"Saved level not found: {name}")
    renderer = GDRenderer()
    missing = sorted({obj.id for obj in level.objects if str(obj.id) not in renderer.object_defs})
    if missing:
        raise AssertionError(f"Missing definitions: {missing}")
    textures = {sprite["texture"] for obj in level.objects
                for sprite in renderer.object_defs[str(obj.id)].get("sprites", [])}
    missing_textures = sorted(texture for texture in textures
                              if renderer.asset_manager.get_sprite(texture) is None)
    if missing_textures:
        raise AssertionError(f"Missing textures: {missing_textures}")
    bounds = level.get_bounds()
    viewport = (math.floor(bounds[0] - 60), math.ceil(bounds[2] + 60),
                math.floor(min(0, bounds[1] - 60)), math.ceil(bounds[3] + 60))
    output.mkdir(parents=True, exist_ok=True)
    image = renderer.render_range(level, *viewport, scale=2, draw_grid=False)
    image.save(output / "gemini_here.png")
    transparent = renderer.render_range(level, *viewport, scale=2, draw_bg=False, draw_ground=False)
    transparent.save(output / "gemini_here_transparent.png")
    if transparent.getbbox() is None:
        raise AssertionError("Real level rendered empty")
    if transparent.getextrema()[3][1] != 255:
        raise AssertionError("Real level lost its opaque geometry")
    # Cropping must produce the same pixels as rendering that world region directly.
    left, right, bottom, top = viewport
    crop = renderer.render_range(level, left + 100, right - 100, bottom + 100, top - 100,
                                 scale=2, draw_bg=False, draw_ground=False)
    expected = transparent.crop((200, 200, transparent.width - 200, transparent.height - 200))
    if crop.tobytes() != expected.tobytes():
        raise AssertionError("Viewport culling or transforms differ from full render")
    report = {"level": level.name, "objects": level.object_count, "bounds": bounds,
              "independent_rotations": sum(131 in obj.raw_props or 132 in obj.raw_props for obj in level.objects),
              "missing_definitions": missing, "missing_textures": missing_textures,
              "image_size": image.size}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--level")
    parser.add_argument("--out", type=Path, default=ROOT / "build-regressions/gd-renderer")
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.level:
        exercise_local_level(args.level, args.out)
