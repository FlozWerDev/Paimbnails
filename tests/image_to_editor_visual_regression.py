"""Convert a real image through production C++, export GD objects and render them.

python tests/image_to_editor_visual_regression.py --image path/to/image.jpg
"""

import argparse
import base64
from collections import Counter
import json
import gzip
import math
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from tools.gd_renderer import GDRenderer, LevelParser


def rotated_rectangles(level):
    return sum(obj.id == 211 and obj.rotation_x is None and obj.rotation_y is None and
               min(obj.rotation % 90, 90 - obj.rotation % 90) > 0.01
               for obj in level.objects)


def compare_pixels(reference, rendered):
    source = np.asarray(reference.convert("RGBA"), dtype=np.float64)
    expected = source[:, :, :3] * (source[:, :, 3:4] / 255)
    actual = np.asarray(rendered.convert("RGBA"), dtype=np.float64)
    actual_rgb = actual[:, :, :3] * (actual[:, :, 3:4] / 255)
    difference = actual_rgb - expected
    mse = float(np.mean(difference ** 2))
    weights = np.array([0.2126, 0.7152, 0.0722])
    expected_luma, actual_luma = expected @ weights, actual_rgb @ weights
    edge_error = sum(float(np.mean(np.abs(np.diff(expected_luma, axis=axis) -
                                           np.diff(actual_luma, axis=axis)))) for axis in (0, 1)) / 2
    uncovered = (actual[:, :, 3] < 250) & (source[:, :, 3] >= 250)
    return {"rgb_mae": float(np.mean(np.abs(difference))),
            "psnr_db": 10 * math.log10(255 ** 2 / mse) if mse else 100,
            "edge_mae": edge_error,
            "uncovered_pixels": int(np.count_nonzero(uncovered)),
            "uncovered_interior_pixels": int(np.count_nonzero(uncovered[2:-2, 2:-2]))}


def interior_color_defects(grid_path, rendered):
    with Image.open(grid_path) as grid:
        expected = np.asarray(grid.convert("RGBA").resize(rendered.size, Image.Resampling.NEAREST))
    actual = np.asarray(rendered.convert("RGBA"))
    difference = np.max(np.abs(expected[:, :, :3].astype(float) - actual[:, :, :3]), axis=2)
    defects = np.zeros(difference.shape, dtype=bool)
    for color in np.unique(expected.reshape(-1, 4), axis=0):
        if color[3] < 250:
            continue
        region = np.all(expected == color, axis=2)
        interior = np.asarray(Image.fromarray(region.astype("uint8") * 255).filter(ImageFilter.MinFilter(5))) > 0
        defects |= interior & ((difference > 16) | (actual[:, :, 3] < 250))
    # The native canvas border is antialiased against transparency.
    defects[:2] = defects[-2:] = False
    defects[:, :2] = defects[:, -2:] = False
    return int(np.count_nonzero(defects))


def black_background(image):
    return Image.alpha_composite(Image.new("RGBA", image.size, (0, 0, 0, 255)),
                                 image.convert("RGBA")).convert("RGB")


def render_level(renderer, level, width, height, target_width):
    return renderer.render_range(level, 0, width, 0, height, scale=target_width / width,
                                 draw_bg=False, draw_ground=False, antialias=4)


def render_detail(renderer, level, grid_size, source_size, box):
    left, top, right, bottom = box
    width, height = grid_size
    xscale, yscale = width / source_size[0], height / source_size[1]
    return renderer.render_range(level, left * xscale, right * xscale,
                                 height - bottom * yscale, height - top * yscale,
                                 scale=4 / xscale, draw_bg=False, draw_ground=False, antialias=2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build-regressions/image-to-editor/verified")
    parser.add_argument("--bench", type=Path)
    parser.add_argument("--baseline-level", type=Path)
    parser.add_argument("--require-fewer-objects", action="store_true")
    parser.add_argument("--require-clean-interiors", action="store_true")
    parser.add_argument("--reference-level")
    parser.add_argument("--colors", type=int, default=24)
    parser.add_argument("--budget", type=int, default=12000)
    parser.add_argument("--background", choices=("keep", "auto"), default="keep")
    parser.add_argument("--detail-region", action="append", default=[],
                        help="Source pixel crop: left,top,right,bottom; may be repeated")
    args = parser.parse_args()
    if args.require_fewer_objects and not args.baseline_level:
        parser.error("--require-fewer-objects needs --baseline-level")
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    bench = args.bench.resolve() if args.bench else output / "converter.exe"
    if not args.bench:
        compiler = shutil.which("clang++")
        if not compiler:
            raise RuntimeError("Clang++ is required to build the production conversion harness")
        subprocess.run([compiler, "-std=c++23", "-O2", "-pthread",
                        str(ROOT / "tests/gif_import_bench.cpp"), "-o", str(bench)], check=True, cwd=ROOT)
    result = subprocess.run([str(bench), str(args.image.resolve()), "--mode", "paint",
        "--dim", "320", "--colors", str(args.colors), "--budget", str(args.budget),
        "--auto", "1", "--background", args.background, "--dump", str(output)], check=True, capture_output=True, text=True)
    (output / "conversion.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    print(result.stdout)
    level_path = output / (args.image.stem + ".level")
    level = LevelParser.parse_file(str(level_path))
    grid_path = output / (args.image.stem + "-grid.png")
    with Image.open(grid_path) as grid:
        width, height = grid.size
    with Image.open(args.image) as source:
        original = source.convert("RGBA")
    renderer = GDRenderer()
    rendered = render_level(renderer, level, width, height, original.width)
    rendered.save(output / "native-render.png")
    reference = original.resize(rendered.size, Image.Resampling.LANCZOS)
    if args.background == "auto":
        with Image.open(grid_path) as grid:
            reference.putalpha(grid.convert("RGBA").getchannel("A").resize(reference.size, Image.Resampling.NEAREST))
    report = {"source": args.image.name, "grid": [width, height],
              "background": args.background,
              "objects": level.object_count, "object_ids": dict(Counter(obj.id for obj in level.objects)),
              "native_rotated_rectangles": rotated_rectangles(level),
              "independent_axis_rotations": sum(obj.rotation_x is not None or obj.rotation_y is not None
                                                 for obj in level.objects),
              "native_render_size": rendered.size, "metrics": compare_pixels(reference, rendered)}
    report["metrics"]["interior_color_defects"] = interior_color_defects(grid_path, rendered)
    if args.require_clean_interiors and report["metrics"]["interior_color_defects"]:
        raise AssertionError(f"Native rendering has {report['metrics']['interior_color_defects']} wrong-color interior pixels")
    if level.object_count > args.budget:
        raise AssertionError("Conversion exceeded its object budget")
    if not level.object_count or rendered.getbbox() is None:
        raise AssertionError("The exported native level is empty")
    for obj in level.objects:
        definition = renderer.object_defs.get(str(obj.id))
        if definition is None:
            raise AssertionError(f"Unknown native object {obj.id}")
        for sprite in definition.get("sprites", []):
            if renderer.asset_manager.get_sprite(sprite["texture"]) is None:
                raise AssertionError(f"Missing texture {sprite['texture']}")
    if args.background == "keep" and original.getextrema()[3] == (255, 255) and report["metrics"]["uncovered_interior_pixels"]:
        raise AssertionError("The opaque source acquired interior transparency in GD")
    panels = [("Original", black_background(reference)), ("Objetos GD", black_background(rendered))]
    baseline = None
    if args.baseline_level:
        baseline = LevelParser.parse_file(str(args.baseline_level))
        if not baseline.object_count:
            raise AssertionError("The supplied baseline has no native objects")
        # Benchmark exports use the matching -grid image as their world extent.
        with Image.open(args.baseline_level.with_name(args.baseline_level.stem + "-grid.png")) as grid:
            bw, bh = grid.size
        before = render_level(renderer, baseline, bw, bh, original.width)
        before = before.resize(rendered.size, Image.Resampling.LANCZOS)
        before.save(output / "before-native.png")
        report["before"] = {"objects": baseline.object_count,
                            "object_ids": dict(Counter(obj.id for obj in baseline.objects)),
                            "native_rotated_rectangles": rotated_rectangles(baseline),
                            "metrics": compare_pixels(reference, before)}
        report["before"]["metrics"]["interior_color_defects"] = interior_color_defects(
            args.baseline_level.with_name(args.baseline_level.stem + "-grid.png"), before)
        report["object_reduction_percent"] = 100 * (1 - level.object_count / baseline.object_count)
        if args.require_fewer_objects and level.object_count >= baseline.object_count:
            raise AssertionError("Conversion did not reduce the supplied baseline's object count")
        report["rgb_error_reduction_percent"] = 100 * (1 - report["metrics"]["rgb_mae"] /
            max(report["before"]["metrics"]["rgb_mae"], 1e-9))
        panels.insert(1, ("Antes", black_background(before)))
        if report["metrics"]["rgb_mae"] >= report["before"]["metrics"]["rgb_mae"]:
            raise AssertionError("Native rendering did not improve over the supplied baseline")
    for index, region in enumerate(args.detail_region, 1):
        box = tuple(int(value) for value in region.split(","))
        if (len(box) != 4 or not 0 <= box[0] < box[2] <= original.width or
                not 0 <= box[1] < box[3] <= original.height):
            raise ValueError(f"Invalid source crop: {region}")
        detail = render_detail(renderer, level, (width, height), original.size, box)
        cropped = [("Original", black_background(original.crop(box).resize(detail.size, Image.Resampling.LANCZOS)))]
        if baseline is not None:
            cropped.append(("Antes", black_background(render_detail(renderer, baseline, (bw, bh), original.size, box))))
        cropped.append(("Ahora", black_background(detail)))
        comparison = Image.new("RGB", (detail.width * len(cropped), detail.height + 28), (30, 30, 34))
        draw = ImageDraw.Draw(comparison)
        for panel_index, (label, panel) in enumerate(cropped):
            comparison.paste(panel, (panel_index * detail.width, 28))
            draw.text((panel_index * detail.width + 10, 7), label, fill="white")
        comparison.save(output / f"detail-{index}.png")
    if args.reference_level:
        style = LevelParser.load_local_level(args.reference_level)
        if style is None:
            raise AssertionError(f"Reference editor level not found: {args.reference_level}")
        report["reference_style"] = {"level": style.name, "objects": style.object_count,
                                    "object_ids": dict(Counter(obj.id for obj in style.objects))}
    comparison = Image.new("RGB", (rendered.width * len(panels), rendered.height + 28), (30, 30, 34))
    draw = ImageDraw.Draw(comparison)
    for index, (label, panel) in enumerate(panels):
        comparison.paste(panel, (index * rendered.width, 28))
        draw.text((index * rendered.width + 10, 7), label, fill="white")
    comparison.save(output / "comparison.png")
    # GDShare-compatible XML export; the user's existing editor save is untouched.
    with (output / (args.image.stem + ".gmd")).open("wb") as stream:
        encoded = base64.urlsafe_b64encode(gzip.compress(level_path.read_bytes(), mtime=0)).decode("ascii")
        plistlib.dump({"kCEK": 4, "k2": args.image.stem, "k4": encoded,
                       "k5": "Paimbnails", "k16": 1, "k21": 2, "k50": 47}, stream)
    exported = LevelParser.parse_file(str(output / (args.image.stem + ".gmd")))
    if exported.object_count != level.object_count or exported.ks38 != level.ks38:
        raise AssertionError("GMD round trip changed native objects or colors")
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
