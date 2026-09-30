#!/usr/bin/env python3
"""grayscale bodies keep black outlines when the runtime multiplies the tint."""

import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SS = 4                      # supersampling factor
OUT = os.path.join(os.path.dirname(__file__), "..", "..")

BLACK = (0, 0, 0, 255)
BODY = (196, 196, 196, 255)
FACE = (232, 232, 232, 255)
GLOSS = (255, 255, 255, 255)
SHADE = (150, 150, 150, 255)
# the recessed face stays dark after the tint, so a white number or a game icon
# on top of it always has contrast.
DARK = (84, 84, 92, 255)


def canvas(size):
    return Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))


def save(img, name, size):
    out = img.resize((size, size), Image.LANCZOS)
    path = os.path.abspath(os.path.join(OUT, name))
    out.save(path)
    print("wrote", os.path.relpath(path, OUT), out.size)


def scaled(points, center, factor):
    cx, cy = center
    return [(cx + (x - cx) * factor, cy + (y - cy) * factor) for x, y in points]


def circle_points(cx, cy, r, count=96, start=0.0):
    return [
        (cx + math.cos(start + i * 2 * math.pi / count) * r,
         cy + math.sin(start + i * 2 * math.pi / count) * r)
        for i in range(count)
    ]


def rounded_square_points(cx, cy, half, radius, count=14):
    pts = []
    corners = [
        (cx + half - radius, cy + half - radius, 0.0),
        (cx - half + radius, cy + half - radius, math.pi * 0.5),
        (cx - half + radius, cy - half + radius, math.pi),
        (cx + half - radius, cy - half + radius, math.pi * 1.5),
    ]
    for ox, oy, start in corners:
        for i in range(count + 1):
            a = start + math.pi * 0.5 * i / count
            pts.append((ox + math.cos(a) * radius, oy + math.sin(a) * radius))
    return pts


def rounded_rect_points(cx, cy, half_w, half_h, radius, count=10):
    pts = []
    corners = [
        (cx + half_w - radius, cy + half_h - radius, 0.0),
        (cx - half_w + radius, cy + half_h - radius, math.pi * 0.5),
        (cx - half_w + radius, cy - half_h + radius, math.pi),
        (cx + half_w - radius, cy - half_h + radius, math.pi * 1.5),
    ]
    for ox, oy, start in corners:
        for i in range(count + 1):
            a = start + math.pi * 0.5 * i / count
            pts.append((ox + math.cos(a) * radius, oy + math.sin(a) * radius))
    return pts


def star_points(cx, cy, outer, inner, spikes=5, start=math.pi * 0.5):
    pts = []
    for i in range(spikes * 2):
        r = outer if i % 2 == 0 else inner
        a = start + i * math.pi / spikes
        pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    return pts


def quad(a, ctrl, b, steps=14):
    out = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1.0 - t
        out.append((
            u * u * a[0] + 2 * u * t * ctrl[0] + t * t * b[0],
            u * u * a[1] + 2 * u * t * ctrl[1] + t * t * b[1],
        ))
    return out


def shield_points(cx, cy, half):
    # y grows downwards in pil, so the tip is at +y.
    left = (cx - half, cy - half * 0.42)
    right = (cx + half, cy - half * 0.42)
    tip = (cx, cy + half)
    pts = [left]
    pts += quad(left, (cx, cy - half * 1.16), right)
    pts += quad(right, (cx + half * 0.86, cy + half * 0.46), tip)
    pts += quad(tip, (cx - half * 0.86, cy + half * 0.46), left)
    return pts


def crown_points(cx, cy, half):
    w, h = half, half * 0.96
    return [
        (cx - w * 0.90, cy + h * 0.88),
        (cx + w * 0.90, cy + h * 0.88),
        (cx + w * 0.98, cy - h * 0.28),
        (cx + w * 0.52, cy + h * 0.24),
        (cx + w * 0.00, cy - h * 0.90),
        (cx - w * 0.52, cy + h * 0.24),
        (cx - w * 0.98, cy - h * 0.28),
    ]


def hexagon_points(cx, cy, r):
    return [
        (cx + math.cos(math.pi * 0.5 + i * math.pi / 3) * r,
         cy + math.sin(math.pi * 0.5 + i * math.pi / 3) * r)
        for i in range(6)
    ]


def medal(points, center, size, face=0.60, face_points=None, face_center=None,
          gloss_top=True):
    """black rim, lit body, recessed dark face - the gd medal recipe."""
    img = canvas(size)
    d = ImageDraw.Draw(img)

    d.polygon(points, fill=BLACK)
    d.polygon(scaled(points, center, 0.90), fill=SHADE)
    d.polygon(scaled(points, center, 0.84), fill=BODY)
    d.polygon(scaled(points, center, 0.80), fill=FACE)

    recess = face_points if face_points is not None else scaled(points, center, face)
    origin = face_center if face_center is not None else center
    d.polygon(scaled(recess, origin, 1.07), fill=BLACK)
    d.polygon(recess, fill=DARK)

    if gloss_top:
        gloss = canvas(size)
        gd = ImageDraw.Draw(gloss)
        gd.polygon(scaled(points, center, 0.82), fill=GLOSS)
        cx, cy = center
        gd.rectangle([0, cy, size * SS, size * SS], fill=(0, 0, 0, 0))
        gloss = gloss.filter(ImageFilter.GaussianBlur(size * SS * 0.05))
        gloss.putalpha(gloss.getchannel("A").point(lambda a: int(a * 0.42)))
        img.alpha_composite(gloss)

    return img


# geode treats these files as the uhd variant and downscales the rest, so the
# canvas is four times the biggest size the medal is ever drawn at.
def make_tier_frames():
    size = 288
    c = (size * SS / 2, size * SS / 2)
    half = size * SS * 0.47

    save(medal(circle_points(c[0], c[1], half), c, size), "paim_progTierRound.png", size)
    save(medal(hexagon_points(c[0], c[1], half), c, size), "paim_progTierHex.png", size)
    save(medal(shield_points(c[0], c[1], half * 0.92), c, size), "paim_progTierShield.png", size)
    save(medal(star_points(c[0], c[1], half, half * 0.72, 8), c, size), "paim_progTierStar.png", size)
    # the crown has no middle to recess, so its face is the band across the
    # base where the level number sits.
    band_c = (c[0], c[1] + size * SS * 0.24)
    save(medal(crown_points(c[0], c[1], half), c, size,
               face_points=rounded_rect_points(band_c[0], band_c[1],
                                               size * SS * 0.34, size * SS * 0.15,
                                               size * SS * 0.07),
               face_center=band_c),
         "paim_progTierCrown.png", size)


def make_ring():
    size = 360
    img = canvas(size)
    d = ImageDraw.Draw(img)
    c = size * SS / 2
    outer, inner = size * SS * 0.5, size * SS * 0.5 - size * SS * 0.10
    line = size * SS * 0.022

    d.ellipse([c - outer, c - outer, c + outer, c + outer], fill=BLACK)
    d.ellipse([c - outer + line, c - outer + line,
               c + outer - line, c + outer - line], fill=GLOSS)
    d.ellipse([c - inner, c - inner, c + inner, c + inner], fill=BLACK)
    d.ellipse([c - inner + line, c - inner + line,
               c + inner - line, c + inner - line], fill=(0, 0, 0, 0))
    save(img, "paim_progRing.png", size)


# the tile is one flat rounded square: black rim, body tinted with the rarity at
# runtime, and a separate ring and fill drawn on the same footprint so the three
# sprites line up when scaled to the same size.
TILE_HALF = 0.44
TILE_RADIUS = 0.13
TILE_INNER = 0.93


def tile_outline(U):
    c = U / 2
    return rounded_square_points(c, c, U * TILE_HALF, U * TILE_RADIUS)


def make_tile(size=288):
    img = canvas(size)
    d = ImageDraw.Draw(img)
    U = size * SS
    c = (U / 2, U / 2)

    outer = tile_outline(U)
    inner = scaled(outer, c, TILE_INNER)
    d.polygon(outer, fill=BLACK)
    d.polygon(inner, fill=GLOSS)

    # vertical falloff, so a flat tint still has a top edge to read against.
    body = canvas(size)
    ImageDraw.Draw(body).polygon(inner, fill=BLACK)
    ramp = Image.linear_gradient("L").resize((U, U)).point(lambda v: int(v * 0.34))
    shade = Image.new("RGBA", (U, U), (0, 0, 0, 255))
    shade.putalpha(ImageChops.multiply(ramp, body.getchannel("A")))
    img.alpha_composite(shade)

    save(img, "paim_progTile.png", size)


def make_tile_fill(size=288):
    """body without the rim, for the ccprogresstimer that fills a locked tile."""
    img = canvas(size)
    U = size * SS
    c = (U / 2, U / 2)
    ImageDraw.Draw(img).polygon(scaled(tile_outline(U), c, TILE_INNER), fill=GLOSS)
    save(img, "paim_progTileFill.png", size)


def make_tile_ring(size=288):
    img = canvas(size)
    d = ImageDraw.Draw(img)
    U = size * SS
    c = (U / 2, U / 2)
    d.polygon(scaled(tile_outline(U), c, TILE_INNER), fill=GLOSS)
    d.polygon(scaled(tile_outline(U), c, 0.855), fill=(0, 0, 0, 0))
    save(img, "paim_progTileRing.png", size)


def make_glow():
    # a smooth falloff gains nothing from supersampling, so it is drawn 1:1.
    size = 192
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = img.load()
    c = size / 2
    for y in range(size):
        for x in range(size):
            t = math.hypot(x - c, y - c) / c
            if t >= 1.0:
                continue
            px[x, y] = (255, 255, 255, int(255 * (1.0 - t) ** 2.4))
    img.save(os.path.abspath(os.path.join(OUT, "paim_progGlow.png")))
    print("wrote paim_progGlow.png", img.size)


def make_spark():
    size = 96
    img = canvas(size)
    d = ImageDraw.Draw(img)
    c = size * SS / 2
    d.polygon(star_points(c, c, c, c * 0.10, 4), fill=GLOSS)
    d.polygon(star_points(c, c, c * 0.55, c * 0.07, 4, math.pi * 0.25), fill=GLOSS)
    img = img.filter(ImageFilter.GaussianBlur(size * SS * 0.012))
    save(img, "paim_progSpark.png", size)


if __name__ == "__main__":
    make_tier_frames()
    make_ring()
    make_tile()
    make_tile_fill()
    make_tile_ring()
    make_glow()
    make_spark()
