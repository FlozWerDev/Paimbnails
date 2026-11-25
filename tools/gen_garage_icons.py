"""Generates the glyphs for the icon kit (garage) buttons. Each one is the top of a Geode
CircleButtonSprite, so it has no base of its own:

- resources/paim_garage_hub.png: three mini cubes plus a gold "+" (Icon Kit Extras hub, pink base).
- resources/paim_garage_copiedIcons.png: a white cube with a gold copy on top (Copied Icons, blue base).
- resources/paim_garage_gradients.png: a gradient cube over a gradient bar with stops (Gradients, gray base).
- resources/paim_garage_swap2p.png: cubes "1" and "2" with two arrows going around them (Swap 2P Kit, green base).
- resources/paim_garage_paimonIcons.png: a rainbow cube being painted by a brush (Paimon Icons, cyan base).

Run from the repo root: python tools/gen_garage_icons.py
"""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from gen_gd_ui_icons import (  # noqa: E402
    N, S, canvas, dilate, erode, fill, finish, gold_part, paint, paste_masked, shape_mask,
)
from gen_translate_button import FONT_LATIN, glyph_mask  # noqa: E402

BLACK = (0, 0, 0)
WHITE_TOP = (255, 255, 255)
WHITE_BOTTOM = (205, 222, 232)

MAGENTA = (255, 70, 205)
YELLOW = (255, 222, 50)
CYAN_STOP = (40, 215, 255)

RIM = 10        # outline of every main shape, same as the other paim_ui glyphs
FACE_RIM = 6    # outline of the inner square of a cube face


def sc(box):
    return tuple(v * S for v in box)


def lerp_stops(stops, t):
    t = min(max(t, 0.0), 1.0) * (len(stops) - 1)
    i = min(int(t), len(stops) - 2)
    f = t - i
    a, b = stops[i], stops[i + 1]
    return tuple(int(a[k] + (b[k] - a[k]) * f) for k in range(3)) + (255,)


def gradient_image(stops, diagonal):
    """Full-canvas multi-stop gradient: left to right, or top-left to bottom-right."""
    g = Image.new("RGBA", (N, N))
    d = ImageDraw.Draw(g)
    if diagonal:
        for c in range(2 * N):
            d.line([(c, 0), (0, c)], fill=lerp_stops(stops, c / (2 * N - 1)), width=2)
    else:
        for x in range(N):
            d.line([(x, 0), (x, N)], fill=lerp_stops(stops, x / (N - 1)))
    return g


def masked_gradient(img, mask, stops, diagonal, box):
    """Paints the gradient so its stops span `box` (256 units), clipped by `mask`."""
    x0, y0, x1, y1 = sc(box)
    full = gradient_image(stops, diagonal).resize((x1 - x0, y1 - y0), Image.BILINEAR)
    paste_masked(img, full, mask, (x0, y0))


def bevel(img, mask, shade=BLACK, shade_a=0.28, gloss_a=0.55):
    """Same bevel as gold_part: dark lower-right edge, light upper-left edge."""
    inner = erode(mask, 5 * S)
    paint(img, ImageChops.subtract(inner, ImageChops.offset(inner, -9 * S, -9 * S)), shade, shade_a)
    paint(img, ImageChops.subtract(inner, ImageChops.offset(inner, 8 * S, 8 * S)), (255, 255, 255), gloss_a)


def rect_mask(box, radius):
    return shape_mask(lambda d: d.rounded_rectangle(sc(box), radius * S, fill=255))


def cube(img, box, paint_body, face=True, radius=14):
    """GD cube: outlined block with the classic inner square."""
    body = rect_mask(box, radius)
    paint(img, dilate(body, RIM * S), BLACK)
    paint_body(img, body)
    if not face:
        return body
    x0, y0, x1, y1 = box
    inset = (x1 - x0) * 0.27
    inner = rect_mask((x0 + inset, y0 + inset, x1 - inset, y1 - inset), 5)
    paint(img, dilate(inner, FACE_RIM * S), BLACK)
    fill(img, inner, WHITE_TOP, WHITE_BOTTOM)
    return body


def solid(top, bottom, shade):
    def painter(img, mask):
        fill(img, mask, top, bottom)
        bevel(img, mask, shade)
    return painter


def white_body(img, mask):
    fill(img, mask, WHITE_TOP, WHITE_BOTTOM)
    bevel(img, mask, (90, 110, 130), 0.30, 0.0)


GOLD = gold_part
CYAN = solid((120, 240, 255), (0, 165, 235), (0, 70, 130))
GREEN = solid((190, 255, 90), (70, 200, 30), (20, 90, 0))
PURPLE = solid((215, 150, 255), (140, 70, 230), (60, 20, 120))


def make_hub():
    """Icon Kit Extras: three icons of the kit and a gold '+' for "more"."""
    img = canvas()
    cube(img, (22, 22, 118, 118), GREEN)
    cube(img, (138, 22, 234, 118), CYAN)
    cube(img, (22, 138, 118, 234), PURPLE)

    def plus(d):
        d.rounded_rectangle(sc((169, 136, 203, 234)), 9 * S, fill=255)
        d.rounded_rectangle(sc((137, 168, 235, 202)), 9 * S, fill=255)

    p = shape_mask(plus)
    paint(img, dilate(p, RIM * S), BLACK)
    gold_part(img, p)
    finish(img, "paim_garage_hub.png")


def make_copied_icons():
    """Copied Icons: the copy symbol drawn with two cubes."""
    img = canvas()
    cube(img, (18, 18, 156, 156), white_body)
    cube(img, (100, 100, 238, 238), GOLD)
    finish(img, "paim_garage_copiedIcons.png")


def make_gradients():
    """Gradients: a cube painted with a gradient, and the stop bar that defines it."""
    img = canvas()
    stops = [MAGENTA, YELLOW, CYAN_STOP]
    cube_box = (58, 14, 198, 154)

    def gradient_body(im, mask):
        masked_gradient(im, mask, stops, True, cube_box)
        bevel(im, mask, BLACK, 0.22, 0.5)

    cube(img, cube_box, gradient_body)

    bar_box = (24, 190, 232, 220)
    bar = rect_mask(bar_box, 15)
    paint(img, dilate(bar, RIM * S), BLACK)
    masked_gradient(img, bar, stops, False, bar_box)
    bevel(img, bar, BLACK, 0.22, 0.4)

    # one handle per color stop, wider than the bar like a slider knob
    for cx, rgb in ((46, MAGENTA), (128, YELLOW), (210, CYAN_STOP)):
        knob = shape_mask(lambda d, cx=cx: d.ellipse(sc((cx - 21, 184, cx + 21, 226)), fill=255))
        paint(img, dilate(knob, 7 * S), BLACK)
        fill(img, knob, WHITE_TOP, WHITE_BOTTOM)
        dot = shape_mask(lambda d, cx=cx: d.ellipse(sc((cx - 13, 192, cx + 13, 218)), fill=255))
        paint(img, dilate(dot, 3 * S), BLACK, 0.55)
        fill(img, dot, rgb)
        bevel(img, dot, BLACK, 0.25, 0.5)
    finish(img, "paim_garage_gradients.png")


def arrow_mask():
    """Clockwise arrow from cube 1 (top-left) to cube 2 (bottom-right) along the top-right."""
    cx, cy, r, w = 146, 122, 64, 20
    end_deg = 328
    m = Image.new("L", (N, N), 0)
    d = ImageDraw.Draw(m)
    ro = r + w / 2
    d.arc(sc((cx - ro, cy - ro, cx + ro, cy + ro)), 270, end_deg, fill=255, width=w * S)

    a = math.radians(end_deg)
    ex, ey = cx + r * math.cos(a), cy + r * math.sin(a)
    tx, ty = -math.sin(a), math.cos(a)       # tangent, direction of travel
    nx, ny = math.cos(a), math.sin(a)        # outward normal
    back, half, length = 4, 24, 30
    bx, by = ex - tx * back, ey - ty * back
    head = [
        (bx + nx * half, by + ny * half),
        (bx - nx * half, by - ny * half),
        (ex + tx * length, ey + ty * length),
    ]
    d.polygon([(x * S, y * S) for x, y in head], fill=255)
    return m


def make_swap_2p():
    """Swap 2P Kit: P1 and P2 cubes trading places."""
    img = canvas()
    for box, painter, digit in (((14, 14, 122, 122), GOLD, "1"), ((134, 134, 242, 242), CYAN, "2")):
        cube(img, box, painter, face=False, radius=16)
        cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
        num = glyph_mask(digit, FONT_LATIN, 76, (cx, cy + 2))
        paint(img, dilate(num, 7 * S), BLACK)
        fill(img, num, WHITE_TOP, WHITE_BOTTOM)

    first = arrow_mask()
    arrows = ImageChops.lighter(first, first.rotate(180))
    paint(img, dilate(arrows, 9 * S), BLACK)
    fill(img, arrows, WHITE_TOP, WHITE_BOTTOM)
    finish(img, "paim_garage_swap2p.png")


RAINBOW = [(255, 70, 70), (255, 160, 40), (255, 230, 60), (90, 220, 80), (40, 200, 255), (170, 90, 255)]


def make_paimon_icons():
    """Paimon Icons: recolor and create icons, so a rainbow cube with a brush on it."""
    img = canvas()
    cube_box = (16, 16, 172, 172)

    def rainbow_body(im, mask):
        stripes = Image.new("RGBA", (N, N), (0, 0, 0, 0))
        d = ImageDraw.Draw(stripes)
        x0, y0, x1, y1 = sc(cube_box)
        lo, hi = x0 + y0, x1 + y1
        step = (hi - lo) / len(RAINBOW)
        for i, rgb in enumerate(RAINBOW):
            a, b = lo + step * i - 1, lo + step * (i + 1) + 1
            d.polygon([(a, 0), (b, 0), (0, b), (0, a)], fill=rgb + (255,))
        paste_masked(im, stripes, mask)
        bevel(im, mask, BLACK, 0.22, 0.5)

    cube(img, cube_box, rainbow_body)

    # brush laid on the diagonal: handle end at the bottom-right, tip on the cube face
    ox, oy = 230, 230
    ux, uy = -1 / math.sqrt(2), -1 / math.sqrt(2)   # towards the tip
    vx, vy = 1 / math.sqrt(2), -1 / math.sqrt(2)    # across the brush

    def pt(t, s):
        return ((ox + ux * t + vx * s) * S, (oy + uy * t + vy * s) * S)

    def poly(points):
        return shape_mask(lambda d: d.polygon([pt(t, s) for t, s in points], fill=255))

    handle = poly([(0, -13), (70, -16), (70, 16), (0, 13)])
    cap = shape_mask(lambda d: d.ellipse((pt(0, 0)[0] - 13 * S, pt(0, 0)[1] - 13 * S,
                                          pt(0, 0)[0] + 13 * S, pt(0, 0)[1] + 13 * S), fill=255))
    handle = ImageChops.lighter(handle, cap)
    ferrule = poly([(66, -20), (98, -20), (98, 20), (66, 20)])
    tip = [(96, -19), (114, -21), (130, -18), (145, -10), (158, 0), (145, 10), (130, 18), (114, 21), (96, 19)]
    bristles = poly(tip)

    brush = ImageChops.lighter(ImageChops.lighter(handle, ferrule), bristles)
    paint(img, dilate(brush, 9 * S), BLACK)

    fill(img, handle, (210, 120, 60), (140, 70, 30))
    bevel(img, handle, (60, 25, 0), 0.35, 0.45)
    fill(img, ferrule, (235, 240, 248), (150, 160, 175))
    bevel(img, ferrule, (40, 50, 70), 0.35, 0.6)
    fill(img, bristles, (255, 120, 220), (230, 40, 170))
    bevel(img, bristles, (110, 0, 70), 0.3, 0.5)

    # black separators between the brush parts
    for t in (68, 97):
        line = poly([(t - 2, -22), (t + 2, -22), (t + 2, 22), (t - 2, 22)])
        paint(img, ImageChops.multiply(line, dilate(brush, 1 * S)), BLACK)
    finish(img, "paim_garage_paimonIcons.png")


if __name__ == "__main__":
    make_hub()
    make_copied_icons()
    make_gradients()
    make_swap_2p()
    make_paimon_icons()
