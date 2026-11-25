"""Generates GD-styled button icons: resources/paim_ui_showThumb.png, resources/paim_ui_reviews.png,
resources/paim_ui_requests.png and resources/paim_ui_info.png.

Run from the repo root: python tools/gen_gd_ui_icons.py
"""
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

S = 4            # supersampling factor
N = 256 * S      # canvas size while drawing
OUT = os.path.join(os.path.dirname(__file__), "..", "resources")


def canvas():
    return Image.new("RGBA", (N, N), (0, 0, 0, 0))


def vgradient(box, top, bottom):
    x0, y0, x1, y1 = box
    w, h = max(x1 - x0, 1), max(y1 - y0, 1)
    g = Image.new("RGBA", (w, h))
    px = ImageDraw.Draw(g)
    for y in range(h):
        t = y / max(h - 1, 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
        px.line([(0, y), (w, y)], fill=c)
    return g


def paste_masked(dst, fill_img, mask, offset=(0, 0)):
    layer = Image.new("RGBA", dst.size, (0, 0, 0, 0))
    layer.paste(fill_img, offset)
    layer.putalpha(ImageChops.multiply(layer.getchannel("A"), mask))
    dst.alpha_composite(layer)


def dilate(mask, r):
    """Round dilation: keeps star tips and corners smooth instead of boxy."""
    out = mask.copy()
    for k in (1.0, 0.66, 0.33):
        for i in range(48):
            a = i * math.tau / 48
            out = ImageChops.lighter(out, ImageChops.offset(mask, round(r * k * math.cos(a)), round(r * k * math.sin(a))))
    return out


def erode(mask, r):
    return ImageChops.invert(dilate(ImageChops.invert(mask), r))


def shape_mask(draw_fn):
    m = Image.new("L", (N, N), 0)
    draw_fn(ImageDraw.Draw(m))
    return m


def paint(dst, mask, rgb, alpha=1.0):
    layer = Image.new("RGBA", dst.size, tuple(rgb) + (255,))
    layer.putalpha(mask.point(lambda v: int(v * alpha)))
    dst.alpha_composite(layer)


def fill(dst, mask, top, bottom=None):
    bb = mask.getbbox()
    paste_masked(dst, vgradient(bb, top, bottom or top), mask, (bb[0], bb[1]))


def finish(img, name):
    img = img.resize((256, 256), Image.LANCZOS)
    path = os.path.join(OUT, name)
    img.save(path)
    print("wrote", os.path.normpath(path))


def star_points(cx, cy, r_outer, r_inner, rot=-math.pi / 2):
    return [
        (cx + (r_outer if i % 2 == 0 else r_inner) * math.cos(rot + i * math.pi / 5),
         cy + (r_outer if i % 2 == 0 else r_inner) * math.sin(rot + i * math.pi / 5))
        for i in range(10)
    ]


def dgradient(box, light, dark):
    """Top-left to bottom-right ramp, the direction vanilla GD icons are lit from."""
    x0, y0, x1, y1 = box
    w, h = max(x1 - x0, 1), max(y1 - y0, 1)
    ramp = Image.linear_gradient("L")
    t = ImageChops.add(ramp.rotate(90).resize((w, h)), ramp.resize((w, h)), scale=2.0)
    a = Image.new("RGBA", (w, h), tuple(light) + (255,))
    b = Image.new("RGBA", (w, h), tuple(dark) + (255,))
    return Image.composite(b, a, t)


def gd_part(dst, mask, light, dark, rim=None, rim_px=5, gloss=0.5, gloss_px=4):
    """Flat GD shading: diagonal ramp, thin gloss on top-left edges, darker rim on bottom-right edges."""
    bb = mask.getbbox()
    paste_masked(dst, dgradient(bb, light, dark), mask, (bb[0], bb[1]))
    if gloss:
        paint(dst, ImageChops.subtract(mask, ImageChops.offset(mask, gloss_px * S, gloss_px * S)), (255, 255, 255), gloss)
    if rim:
        paint(dst, ImageChops.subtract(mask, ImageChops.offset(mask, -rim_px * S, -rim_px * S)), rim, 0.85)


# palettes sampled from vanilla sprites: folder (gold), info icon (cyan), circle base (green)
FRAME_LIGHT, FRAME_DARK, FRAME_RIM = (255, 226, 64), (250, 170, 24), (148, 96, 8)
SKY_TOP, SKY_BOTTOM = (40, 255, 246), (0, 176, 255)
HILL_LIGHT, HILL_LIGHT_LOW = (178, 255, 72), (128, 232, 48)
HILL_DARK, HILL_DARK_LOW = (92, 196, 34), (62, 160, 22)


def make_show_thumb():
    img = canvas()
    # gold picture frame around a cyan sky, sun and two-tone hills, outlined like vanilla icons
    frame_box = (20 * S, 47 * S, 236 * S, 209 * S)
    hole_box = (40 * S, 66 * S, 216 * S, 190 * S)
    pic_box = (47 * S, 73 * S, 209 * S, 183 * S)

    frame = shape_mask(lambda d: d.rounded_rectangle(frame_box, 15 * S, fill=255))
    hole = shape_mask(lambda d: d.rounded_rectangle(hole_box, 7 * S, fill=255))
    pic = shape_mask(lambda d: d.rectangle(pic_box, fill=255))

    outline = dilate(frame, 9 * S)
    paint(img, ImageChops.offset(outline, 5 * S, 6 * S), (0, 0, 0), 0.32)
    paint(img, outline, (0, 0, 0))
    gd_part(img, ImageChops.subtract(frame, hole), FRAME_LIGHT, FRAME_DARK, FRAME_RIM)
    paint(img, hole, (0, 0, 0))

    # picture contents are drawn unclipped, then cut to the window so outlines meet the inner border
    scene = canvas()
    fill(scene, pic, SKY_TOP, SKY_BOTTOM)
    paint(scene, ImageChops.subtract(pic, ImageChops.offset(pic, 0, 7 * S)), (0, 70, 170), 0.35)

    sun = shape_mask(lambda d: d.ellipse((157 * S, 88 * S, 190 * S, 121 * S), fill=255))
    paint(scene, dilate(sun, 6 * S), (0, 0, 0))
    gd_part(scene, sun, (255, 250, 120), (255, 184, 0), FRAME_RIM, rim_px=4, gloss=0.6, gloss_px=3)

    def hill(peak, left, right, base):
        px, py = peak
        whole = shape_mask(lambda d: d.polygon([(left * S, base * S), (px * S, py * S), (right * S, base * S)], fill=255))
        lit = shape_mask(lambda d: d.polygon([(left * S, base * S), (px * S, py * S), (px * S, base * S)], fill=255))
        paint(scene, dilate(whole, 6 * S), (0, 0, 0))
        fill(scene, ImageChops.subtract(whole, lit), HILL_DARK, HILL_DARK_LOW)
        fill(scene, lit, HILL_LIGHT, HILL_LIGHT_LOW)
        paint(scene, ImageChops.subtract(lit, ImageChops.offset(lit, 4 * S, 0)), (255, 255, 255), 0.45)

    hill((97, 102), 14, 181, 202)
    hill((163, 135), 110, 234, 202)

    scene.putalpha(ImageChops.multiply(scene.getchannel("A"), pic))
    img.alpha_composite(scene)
    finish(img, "paim_ui_showThumb.png")


def make_reviews():
    img = canvas()

    def bubble(d):
        d.rounded_rectangle((26 * S, 34 * S, 230 * S, 178 * S), 50 * S, fill=255)
        d.polygon([(62 * S, 160 * S), (56 * S, 226 * S), (122 * S, 172 * S)], fill=255)

    b = shape_mask(bubble)
    fill(img, dilate(b, 10 * S), (0, 0, 0))
    fill(img, b, (255, 255, 255), (205, 222, 232))

    star = shape_mask(lambda d: d.polygon(star_points(128 * S, 108 * S, 54 * S, 26 * S), fill=255))
    fill(img, dilate(star, 6 * S), (150, 80, 0))
    fill(img, star, (255, 248, 120), (255, 176, 0))
    finish(img, "paim_ui_reviews.png")


def make_requests():
    img = canvas()

    def bubble(d):
        d.rounded_rectangle((26 * S, 30 * S, 230 * S, 182 * S), 46 * S, fill=255)
        d.polygon([(150 * S, 166 * S), (186 * S, 230 * S), (96 * S, 176 * S)], fill=255)

    b = shape_mask(bubble)
    fill(img, dilate(b, 10 * S), (0, 0, 0))
    fill(img, b, (255, 255, 255), (205, 222, 232))

    # queue rows: gold bullet + purple bar, so it reads as "incoming list" next to the review bubble
    for i, y in enumerate((62, 98, 134)):
        bar_end = (200, 178, 190)[i]
        bullet = shape_mask(lambda d, y=y: d.ellipse((52 * S, (y - 11) * S, 74 * S, (y + 11) * S), fill=255))
        bar = shape_mask(lambda d, y=y, e=bar_end: d.rounded_rectangle((88 * S, (y - 9) * S, e * S, (y + 9) * S), 9 * S, fill=255))
        fill(img, dilate(bullet, 3 * S), (150, 80, 0))
        fill(img, bullet, (255, 248, 120), (255, 176, 0))
        fill(img, dilate(bar, 3 * S), (52, 22, 96))
        fill(img, bar, (160, 110, 230), (110, 60, 190))
    finish(img, "paim_ui_requests.png")


# gold ramp sampled from the vanilla account button icons (bell, friends)
GOLD_TOP = (255, 246, 92)
GOLD_BOTTOM = (255, 148, 28)
GOLD_SHADE = (176, 84, 0)


def gold_part(img, mask):
    """One chunky gold shape: gradient body, bevel on the lower right, gloss on the upper left."""
    fill(img, mask, GOLD_TOP, GOLD_BOTTOM)
    inner = erode(mask, 5 * S)
    paint(img, ImageChops.subtract(inner, ImageChops.offset(inner, -9 * S, -9 * S)), GOLD_SHADE, 0.38)
    paint(img, ImageChops.subtract(inner, ImageChops.offset(inner, 8 * S, 8 * S)), (255, 255, 255), 0.62)


def make_info():
    img = canvas()
    # serif "i" (dot, flagged stem, foot) so it reads as "info" without a round badge around it
    dot = shape_mask(lambda d: d.ellipse((98 * S, 14 * S, 158 * S, 74 * S), fill=255))

    def body_shape(d):
        d.rounded_rectangle((98 * S, 116 * S, 158 * S, 218 * S), 10 * S, fill=255)
        d.rounded_rectangle((60 * S, 116 * S, 158 * S, 150 * S), 12 * S, fill=255)
        d.rounded_rectangle((52 * S, 190 * S, 204 * S, 232 * S), 14 * S, fill=255)

    body = shape_mask(body_shape)
    outline = dilate(ImageChops.lighter(dot, body), 9 * S)

    shadow = ImageChops.offset(outline, 5 * S, 7 * S).filter(ImageFilter.GaussianBlur(2 * S))
    paint(img, shadow, (0, 0, 0), 0.4)
    paint(img, outline, (0, 0, 0))
    gold_part(img, dot)
    gold_part(img, body)
    finish(img, "paim_ui_info.png")


if __name__ == "__main__":
    make_show_thumb()
    make_reviews()
    make_requests()
    make_info()
