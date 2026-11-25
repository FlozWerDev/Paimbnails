"""Generates the translate button art:

- resources/paim_ui_translate.png: two GD-style speech bubbles ("A" white, "文" gold) used as
  the top of a Geode AccountButtonSprite.
- resources/paim_ui_accountBaseGreen.png: the Geode blue account blank recolored to GD lime
  green, shown over the blue base while a translation is on screen.

Run from the repo root: python tools/gen_translate_button.py
The green base reads $GEODE_SDK/loader/resources/blanks/baseAccount_Normal_Blue.png.
"""
import colorsys
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(__file__))
from gen_gd_ui_icons import (  # noqa: E402
    OUT, S, canvas, dilate, fill, finish, gold_part, paint, shape_mask,
)

FONT_LATIN = r"C:\Windows\Fonts\ariblk.ttf"
FONT_CJK = r"C:\Windows\Fonts\msyhbd.ttc"


def glyph_mask(text, font_path, size, center):
    font = ImageFont.truetype(font_path, size * S)
    mask = shape_mask(lambda d: None)
    ImageDraw.Draw(mask).text((center[0] * S, center[1] * S), text, font=font, fill=255, anchor="mm")
    return mask


def make_translate():
    img = canvas()

    def back_shape(d):
        d.rounded_rectangle((12 * S, 18 * S, 160 * S, 134 * S), 36 * S, fill=255)
        d.polygon([(36 * S, 116 * S), (24 * S, 176 * S), (92 * S, 128 * S)], fill=255)

    def front_shape(d):
        d.rounded_rectangle((108 * S, 100 * S, 242 * S, 206 * S), 34 * S, fill=255)
        d.polygon([(206 * S, 188 * S), (226 * S, 242 * S), (162 * S, 200 * S)], fill=255)

    back = shape_mask(back_shape)
    front = shape_mask(front_shape)

    # white bubble with a black "A", same ramp as the vanilla envelope
    paint(img, dilate(back, 10 * S), (0, 0, 0))
    fill(img, back, (255, 255, 255), (205, 222, 232))
    paint(img, glyph_mask("A", FONT_LATIN, 84, (76, 70)), (0, 0, 0))

    # gold bubble on top; its outline separates it from the white one
    paint(img, dilate(front, 10 * S), (0, 0, 0))
    gold_part(img, front)
    paint(img, glyph_mask("文", FONT_CJK, 80, (176, 150)), (0, 0, 0))

    finish(img, "paim_ui_translate.png")


# GD lime green, sampled from Geode's green circle blank (top and bottom of the body)
GREEN_TOP = (85 / 360, 0.77, 0.90)
GREEN_BOTTOM = (95 / 360, 0.82, 0.80)
# hue range of the blue account blank body, top to bottom
BLUE_HUE_TOP = 204 / 360
BLUE_HUE_BOTTOM = 212 / 360


def make_green_base():
    sdk = os.environ.get("GEODE_SDK")
    if not sdk:
        raise SystemExit("GEODE_SDK is not set")
    src = os.path.join(sdk, "loader", "resources", "blanks", "baseAccount_Normal_Blue.png")
    base = Image.open(src).convert("RGBA")
    px = base.load()
    for y in range(base.height):
        for x in range(base.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if s <= 0.0:
                continue
            # black outline and white rim keep their colour: saturation and value scale them
            t = min(max((h - BLUE_HUE_TOP) / (BLUE_HUE_BOTTOM - BLUE_HUE_TOP), 0.0), 1.0)
            th = GREEN_TOP[0] + (GREEN_BOTTOM[0] - GREEN_TOP[0]) * t
            ts = GREEN_TOP[1] + (GREEN_BOTTOM[1] - GREEN_TOP[1]) * t
            tv = GREEN_TOP[2] + (GREEN_BOTTOM[2] - GREEN_TOP[2]) * t
            nr, ng, nb = colorsys.hsv_to_rgb(th, s * ts, v * tv)
            px[x, y] = (round(nr * 255), round(ng * 255), round(nb * 255), a)
    path = os.path.join(OUT, "paim_ui_accountBaseGreen.png")
    base.save(path)
    print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    make_translate()
    make_green_base()
