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


def rotated(points, center, angle):
    cx, cy = center
    ca, sa = math.cos(angle), math.sin(angle)
    return [(cx + (x - cx) * ca - (y - cy) * sa,
             cy + (x - cx) * sa + (y - cy) * ca) for x, y in points]


def moved(points, dx, dy):
    return [(x + dx, y + dy) for x, y in points]


def circle_points(cx, cy, r, count=96, start=0.0):
    return [
        (cx + math.cos(start + i * 2 * math.pi / count) * r,
         cy + math.sin(start + i * 2 * math.pi / count) * r)
        for i in range(count)
    ]


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


def rounded_square_points(cx, cy, half, radius, count=14):
    return rounded_rect_points(cx, cy, half, half, radius, count)


def star_points(cx, cy, outer, inner, spikes=5, start=math.pi * 0.5):
    pts = []
    for i in range(spikes * 2):
        r = outer if i % 2 == 0 else inner
        a = start + i * math.pi / spikes
        pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    return pts


def outlined(img, width):
    """grows the alpha into a black rim under the shape."""
    radius = max(1, int(width))
    grown = img.getchannel("A").filter(ImageFilter.MaxFilter(radius * 2 + 1))
    rim = Image.new("RGBA", img.size, BLACK)
    rim.putalpha(grown)
    rim.alpha_composite(img)
    return rim


# ---------------------------------------------------------------- glyph deck

# every glyph draws into a unit square (0..1, y down) so the same code renders
# them at card size and at hud size.
def glyph_canvas(size):
    return Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))


def U(v, size):
    return v * size * SS


def g_fog(d, s):
    for i, (y, h) in enumerate([(0.30, 0.085), (0.50, 0.10), (0.70, 0.085)]):
        inset = 0.10 + 0.04 * (i % 2)
        d.rounded_rectangle(
            [U(inset, s), U(y - h / 2, s), U(1 - inset, s), U(y + h / 2, s)],
            radius=U(h / 2, s), fill=GLOSS)


def g_mirror(d, s):
    d.polygon([(U(0.5, s), U(0.12, s)), (U(0.5, s), U(0.88, s))], fill=GLOSS)
    d.rectangle([U(0.485, s), U(0.10, s), U(0.515, s), U(0.90, s)], fill=GLOSS)
    d.polygon([(U(0.44, s), U(0.30, s)), (U(0.44, s), U(0.70, s)),
               (U(0.14, s), U(0.50, s))], fill=GLOSS)
    d.polygon([(U(0.56, s), U(0.30, s)), (U(0.56, s), U(0.70, s)),
               (U(0.86, s), U(0.50, s))], fill=GLOSS)


def g_quake(d, s):
    pts = [(0.10, 0.50), (0.28, 0.22), (0.40, 0.62), (0.55, 0.18),
           (0.68, 0.66), (0.80, 0.34), (0.90, 0.50)]
    d.line([(U(x, s), U(y, s)) for x, y in pts], fill=GLOSS,
           width=int(U(0.085, s)), joint="curve")


def g_zoom_in(d, s):
    r = U(0.26, s)
    c = (U(0.44, s), U(0.44, s))
    d.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], outline=GLOSS,
              width=int(U(0.075, s)))
    d.line([U(0.62, s), U(0.62, s), U(0.86, s), U(0.86, s)], fill=GLOSS,
           width=int(U(0.10, s)))
    d.rectangle([U(0.30, s), U(0.415, s), U(0.58, s), U(0.465, s)], fill=GLOSS)
    d.rectangle([U(0.415, s), U(0.30, s), U(0.465, s), U(0.58, s)], fill=GLOSS)


def g_zoom_out(d, s):
    r = U(0.26, s)
    c = (U(0.44, s), U(0.44, s))
    d.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], outline=GLOSS,
              width=int(U(0.075, s)))
    d.line([U(0.62, s), U(0.62, s), U(0.86, s), U(0.86, s)], fill=GLOSS,
           width=int(U(0.10, s)))
    d.rectangle([U(0.30, s), U(0.415, s), U(0.58, s), U(0.465, s)], fill=GLOSS)


def g_shield(d, s):
    d.polygon([(U(0.50, s), U(0.10, s)), (U(0.86, s), U(0.26, s)),
               (U(0.86, s), U(0.54, s)), (U(0.50, s), U(0.92, s)),
               (U(0.14, s), U(0.54, s)), (U(0.14, s), U(0.26, s))], fill=GLOSS)


def g_checkpoint(d, s):
    d.rectangle([U(0.22, s), U(0.10, s), U(0.30, s), U(0.90, s)], fill=GLOSS)
    d.polygon([(U(0.30, s), U(0.14, s)), (U(0.84, s), U(0.30, s)),
               (U(0.30, s), U(0.50, s))], fill=GLOSS)


def g_dispel(d, s):
    d.ellipse([U(0.14, s), U(0.14, s), U(0.86, s), U(0.86, s)],
              outline=GLOSS, width=int(U(0.10, s)))
    d.line([U(0.28, s), U(0.28, s), U(0.72, s), U(0.72, s)], fill=GLOSS,
           width=int(U(0.10, s)))


def g_reflect(d, s):
    d.arc([U(0.14, s), U(0.18, s), U(0.86, s), U(0.90, s)], 195, 345,
          fill=GLOSS, width=int(U(0.10, s)))
    d.polygon([(U(0.78, s), U(0.16, s)), (U(0.92, s), U(0.40, s)),
               (U(0.64, s), U(0.40, s))], fill=GLOSS)


def g_swap(d, s):
    d.rectangle([U(0.22, s), U(0.32, s), U(0.78, s), U(0.40, s)], fill=GLOSS)
    d.polygon([(U(0.86, s), U(0.36, s)), (U(0.62, s), U(0.20, s)),
               (U(0.62, s), U(0.52, s))], fill=GLOSS)
    d.rectangle([U(0.22, s), U(0.60, s), U(0.78, s), U(0.68, s)], fill=GLOSS)
    d.polygon([(U(0.14, s), U(0.64, s)), (U(0.38, s), U(0.48, s)),
               (U(0.38, s), U(0.80, s))], fill=GLOSS)


def g_eye(d, s):
    d.polygon([(U(0.08, s), U(0.50, s))] +
              [(U(0.08 + 0.84 * t, s), U(0.50 - 0.30 * math.sin(math.pi * t), s))
               for t in [i / 24 for i in range(25)]] +
              [(U(0.08 + 0.84 * (1 - t), s),
                U(0.50 + 0.30 * math.sin(math.pi * t), s))
               for t in [i / 24 for i in range(25)]], fill=GLOSS)
    d.ellipse([U(0.40, s), U(0.40, s), U(0.60, s), U(0.60, s)], fill=DARK)


def g_freeze(d, s):
    for i in range(3):
        a = math.pi * i / 3
        dx, dy = math.cos(a) * U(0.36, s), math.sin(a) * U(0.36, s)
        d.line([U(0.5, s) - dx, U(0.5, s) - dy, U(0.5, s) + dx, U(0.5, s) + dy],
               fill=GLOSS, width=int(U(0.085, s)))
    d.ellipse([U(0.42, s), U(0.42, s), U(0.58, s), U(0.58, s)], fill=GLOSS)


def g_mask(d, s):
    # domino mask: a wide band with two cut eyes and a dip in the middle.
    d.polygon([(U(0.08, s), U(0.34, s)), (U(0.36, s), U(0.28, s)),
               (U(0.50, s), U(0.36, s)), (U(0.64, s), U(0.28, s)),
               (U(0.92, s), U(0.34, s)), (U(0.86, s), U(0.62, s)),
               (U(0.62, s), U(0.74, s)), (U(0.50, s), U(0.62, s)),
               (U(0.38, s), U(0.74, s)), (U(0.14, s), U(0.62, s))], fill=GLOSS)
    d.ellipse([U(0.20, s), U(0.38, s), U(0.40, s), U(0.56, s)], fill=DARK)
    d.ellipse([U(0.60, s), U(0.38, s), U(0.80, s), U(0.56, s)], fill=DARK)


def g_weight(d, s):
    d.polygon([(U(0.24, s), U(0.34, s)), (U(0.76, s), U(0.34, s)),
               (U(0.88, s), U(0.88, s)), (U(0.12, s), U(0.88, s))], fill=GLOSS)
    d.arc([U(0.34, s), U(0.10, s), U(0.66, s), U(0.46, s)], 180, 360,
          fill=GLOSS, width=int(U(0.075, s)))


def g_noise(d, s):
    d.polygon([(U(0.14, s), U(0.40, s)), (U(0.30, s), U(0.40, s)),
               (U(0.48, s), U(0.20, s)), (U(0.48, s), U(0.80, s)),
               (U(0.30, s), U(0.60, s)), (U(0.14, s), U(0.60, s))], fill=GLOSS)
    for i, r in enumerate([0.12, 0.22]):
        d.arc([U(0.52 - r, s) + U(0.10, s), U(0.50 - r, s),
               U(0.52 + r, s) + U(0.10, s), U(0.50 + r, s)], 300, 60,
              fill=GLOSS, width=int(U(0.06, s)))


def g_bolt(d, s):
    d.polygon([(U(0.60, s), U(0.08, s)), (U(0.24, s), U(0.54, s)),
               (U(0.46, s), U(0.54, s)), (U(0.38, s), U(0.92, s)),
               (U(0.76, s), U(0.44, s)), (U(0.53, s), U(0.44, s))], fill=GLOSS)


def g_hourglass(d, s):
    d.polygon([(U(0.20, s), U(0.10, s)), (U(0.80, s), U(0.10, s)),
               (U(0.54, s), U(0.50, s)), (U(0.80, s), U(0.90, s)),
               (U(0.20, s), U(0.90, s)), (U(0.46, s), U(0.50, s))], fill=GLOSS)


def g_dice(d, s):
    d.rounded_rectangle([U(0.14, s), U(0.14, s), U(0.86, s), U(0.86, s)],
                        radius=U(0.16, s), fill=GLOSS)
    for x, y in [(0.32, 0.32), (0.68, 0.32), (0.50, 0.50),
                 (0.32, 0.68), (0.68, 0.68)]:
        r = U(0.07, s)
        d.ellipse([U(x, s) - r, U(y, s) - r, U(x, s) + r, U(y, s) + r], fill=DARK)


def g_skull(d, s):
    d.ellipse([U(0.16, s), U(0.12, s), U(0.84, s), U(0.72, s)], fill=GLOSS)
    d.rounded_rectangle([U(0.34, s), U(0.62, s), U(0.66, s), U(0.90, s)],
                        radius=U(0.06, s), fill=GLOSS)
    d.ellipse([U(0.28, s), U(0.32, s), U(0.46, s), U(0.54, s)], fill=DARK)
    d.ellipse([U(0.54, s), U(0.32, s), U(0.72, s), U(0.54, s)], fill=DARK)


def g_heart(d, s):
    d.ellipse([U(0.14, s), U(0.18, s), U(0.52, s), U(0.56, s)], fill=GLOSS)
    d.ellipse([U(0.48, s), U(0.18, s), U(0.86, s), U(0.56, s)], fill=GLOSS)
    d.polygon([(U(0.16, s), U(0.42, s)), (U(0.84, s), U(0.42, s)),
               (U(0.50, s), U(0.90, s))], fill=GLOSS)


def g_lock(d, s):
    d.arc([U(0.28, s), U(0.12, s), U(0.72, s), U(0.60, s)], 180, 360,
          fill=GLOSS, width=int(U(0.10, s)))
    d.rounded_rectangle([U(0.18, s), U(0.44, s), U(0.82, s), U(0.90, s)],
                        radius=U(0.10, s), fill=GLOSS)


def g_ghost(d, s):
    d.pieslice([U(0.16, s), U(0.10, s), U(0.84, s), U(0.78, s)], 180, 360,
               fill=GLOSS)
    d.rectangle([U(0.16, s), U(0.44, s), U(0.84, s), U(0.76, s)], fill=GLOSS)
    d.polygon([(U(0.16, s), U(0.74, s)), (U(0.33, s), U(0.92, s)),
               (U(0.50, s), U(0.74, s)), (U(0.67, s), U(0.92, s)),
               (U(0.84, s), U(0.74, s))], fill=GLOSS)
    d.ellipse([U(0.30, s), U(0.34, s), U(0.44, s), U(0.52, s)], fill=DARK)
    d.ellipse([U(0.56, s), U(0.34, s), U(0.70, s), U(0.52, s)], fill=DARK)


def g_magnet(d, s):
    d.arc([U(0.14, s), U(0.16, s), U(0.86, s), U(0.92, s)], 180, 360,
          fill=GLOSS, width=int(U(0.20, s)))
    d.rectangle([U(0.14, s), U(0.60, s), U(0.34, s), U(0.86, s)], fill=GLOSS)
    d.rectangle([U(0.66, s), U(0.60, s), U(0.86, s), U(0.86, s)], fill=GLOSS)


def g_bomb(d, s):
    d.ellipse([U(0.14, s), U(0.28, s), U(0.80, s), U(0.92, s)], fill=GLOSS)
    d.rectangle([U(0.54, s), U(0.20, s), U(0.68, s), U(0.36, s)], fill=GLOSS)
    d.line([U(0.62, s), U(0.22, s), U(0.86, s), U(0.08, s)], fill=GLOSS,
           width=int(U(0.06, s)))
    d.polygon(star_points(U(0.88, s), U(0.08, s), U(0.12, s), U(0.05, s), 5),
              fill=GLOSS)


def g_flag(d, s):
    d.rectangle([U(0.20, s), U(0.08, s), U(0.30, s), U(0.92, s)], fill=GLOSS)
    for row in range(3):
        for col in range(3):
            if (row + col) % 2:
                continue
            x0 = 0.30 + col * 0.18
            y0 = 0.14 + row * 0.14
            d.rectangle([U(x0, s), U(y0, s), U(x0 + 0.18, s), U(y0 + 0.14, s)],
                        fill=GLOSS)


def g_stopwatch(d, s):
    d.ellipse([U(0.14, s), U(0.22, s), U(0.86, s), U(0.94, s)], outline=GLOSS,
              width=int(U(0.09, s)))
    d.rectangle([U(0.42, s), U(0.06, s), U(0.58, s), U(0.20, s)], fill=GLOSS)
    d.line([U(0.50, s), U(0.58, s), U(0.50, s), U(0.34, s)], fill=GLOSS,
           width=int(U(0.075, s)))
    d.line([U(0.50, s), U(0.58, s), U(0.70, s), U(0.58, s)], fill=GLOSS,
           width=int(U(0.075, s)))


def g_crown(d, s):
    d.polygon([(U(0.12, s), U(0.78, s)), (U(0.88, s), U(0.78, s)),
               (U(0.92, s), U(0.24, s)), (U(0.70, s), U(0.46, s)),
               (U(0.50, s), U(0.14, s)), (U(0.30, s), U(0.46, s)),
               (U(0.08, s), U(0.24, s))], fill=GLOSS)
    d.rectangle([U(0.12, s), U(0.80, s), U(0.88, s), U(0.92, s)], fill=GLOSS)


def g_link(d, s):
    for dx in (-0.14, 0.14):
        d.rounded_rectangle([U(0.34 + dx, s), U(0.22, s),
                             U(0.66 + dx, s), U(0.78, s)],
                            radius=U(0.16, s), outline=GLOSS,
                            width=int(U(0.085, s)))


def g_arrows_up(d, s):
    for i, y in enumerate([0.16, 0.46, 0.76]):
        w = 0.34 - i * 0.06
        d.polygon([(U(0.50, s), U(y, s)), (U(0.50 + w, s), U(y + 0.20, s)),
                   (U(0.50 - w, s), U(y + 0.20, s))], fill=GLOSS)


def g_pause(d, s):
    d.rounded_rectangle([U(0.20, s), U(0.14, s), U(0.42, s), U(0.86, s)],
                        radius=U(0.06, s), fill=GLOSS)
    d.rounded_rectangle([U(0.58, s), U(0.14, s), U(0.80, s), U(0.86, s)],
                        radius=U(0.06, s), fill=GLOSS)


CARD_GLYPHS = [
    ("fog", g_fog),
    ("mirror", g_mirror),
    ("quake", g_quake),
    ("zoomin", g_zoom_in),
    ("zoomout", g_zoom_out),
    ("shield", g_shield),
    ("checkpoint", g_checkpoint),
    ("dispel", g_dispel),
    ("reflect", g_reflect),
    ("swap", g_swap),
    ("eye", g_eye),
    ("freeze", g_freeze),
    ("mask", g_mask),
    ("weight", g_weight),
    ("noise", g_noise),
    ("bolt", g_bolt),
    ("hourglass", g_hourglass),
    ("dice", g_dice),
    ("skull", g_skull),
    ("heart", g_heart),
    ("lock", g_lock),
    ("ghost", g_ghost),
    ("magnet", g_magnet),
    ("bomb", g_bomb),
]

MODE_GLYPHS = [
    ("race", g_flag),
    ("sudden", g_skull),
    ("ladder", g_arrows_up),
    ("roulette", g_dice),
    ("timeattack", g_stopwatch),
    ("tug", g_magnet),
    ("attempts", g_pause),
    ("relay", g_link),
    ("king", g_crown),
]


def make_glyphs():
    size = 96
    for name, fn in CARD_GLYPHS:
        img = glyph_canvas(size)
        fn(ImageDraw.Draw(img), size)
        save(outlined(img, size * SS * 0.035), f"paim_vsCard_{name}.png", size)

    for name, fn in MODE_GLYPHS:
        img = glyph_canvas(size)
        fn(ImageDraw.Draw(img), size)
        save(outlined(img, size * SS * 0.035), f"paim_vsMode_{name}.png", size)


# ------------------------------------------------------------------ lettering

def vs_letters(size, height=0.46):
    """the vs wordmark, drawn as two slanted polygons so it reads at 24px."""
    img = canvas(size)
    d = ImageDraw.Draw(img)
    u = size * SS
    cy = u * 0.5
    h = u * height
    slant = h * 0.20
    stroke = h * 0.30

    # v: two legs meeting at the bottom.
    vx = u * 0.30
    d.polygon([(vx - h * 0.46 + slant, cy - h / 2),
               (vx - h * 0.46 + slant + stroke, cy - h / 2),
               (vx + stroke * 0.4, cy + h / 2),
               (vx - stroke * 0.4, cy + h / 2)], fill=GLOSS)
    d.polygon([(vx + h * 0.46 + slant, cy - h / 2),
               (vx + h * 0.46 + slant - stroke, cy - h / 2),
               (vx - stroke * 0.4, cy + h / 2),
               (vx + stroke * 0.4, cy + h / 2)], fill=GLOSS)

    # s: three bars plus two connectors, the blocky arcade shape.
    sx = u * 0.68
    w = h * 0.44
    for y in (cy - h / 2, cy - stroke / 2, cy + h / 2 - stroke):
        off = slant * (1.0 - (y - (cy - h / 2)) / h) * 2 - slant
        d.polygon([(sx - w + off, y), (sx + w + off, y),
                   (sx + w + off - slant * 0.5, y + stroke),
                   (sx - w + off - slant * 0.5, y + stroke)], fill=GLOSS)
    d.polygon([(sx - w + slant, cy - h / 2),
               (sx - w + slant + stroke, cy - h / 2),
               (sx - w, cy + stroke / 2),
               (sx - w - stroke, cy + stroke / 2)], fill=GLOSS)
    d.polygon([(sx + w, cy - stroke / 2),
               (sx + w + stroke, cy - stroke / 2),
               (sx + w - slant, cy + h / 2),
               (sx + w - slant - stroke, cy + h / 2)], fill=GLOSS)

    return img


def make_logo():
    size = 192
    save(outlined(vs_letters(size, 0.62), size * SS * 0.030),
         "paim_vsLogo.png", size)


# ------------------------------------------------------------------- swords

# sword profile in blade lengths, with the origin on the guard: that is the
# point the two blades cross at, so rotating each copy about it gives the
# emblem its long blades up and short grips down.
SWORD = {
    "blade": [(-0.075, 0.10), (-0.075, -0.80), (0.0, -1.00), (0.075, -0.80),
              (0.075, 0.10)],
    "guard": [(-0.30, 0.10), (0.30, 0.10), (0.34, 0.19), (0.24, 0.24),
              (-0.24, 0.24), (-0.34, 0.19)],
    "grip": [(-0.055, 0.24), (0.055, 0.24), (0.055, 0.54), (-0.055, 0.54)],
}
SWORD_TOP, SWORD_BOTTOM = -1.00, 0.64


def draw_swords(d, center, span, angle=math.radians(30)):
    """crossed swords centred on `center`, `span` tall before rotation."""
    scale = span / (SWORD_BOTTOM - SWORD_TOP)
    # rotating about the guard leaves the emblem top-heavy; nudge it back down.
    cx = center[0]
    cy = center[1] - (SWORD_TOP + SWORD_BOTTOM) * 0.5 * scale

    for sign in (-1, 1):
        parts = [[(x * scale, y * scale) for x, y in part]
                 for part in SWORD.values()]
        parts.append([(x * scale, y * scale)
                      for x, y in circle_points(0.0, 0.58, 0.10, 24)])
        for part in parts:
            d.polygon(rotated(moved(part, cx, cy), (cx, cy), sign * angle),
                      fill=GLOSS)


def make_swords():
    size = 192
    img = canvas(size)
    draw_swords(ImageDraw.Draw(img), (size * SS / 2, size * SS / 2),
                size * SS * 0.80)
    save(outlined(img, size * SS * 0.022), "paim_vsSwords.png", size)


def make_badge_frame():
    """crossed swords + laurel arc that sits behind a progression tier medal."""
    size = 288
    img = canvas(size)
    d = ImageDraw.Draw(img)
    u = size * SS
    c = (u / 2, u / 2)

    draw_swords(d, c, u * 0.94, math.radians(32))

    # laurel: a stem arc down each side with leaves alternating off it. the
    # medal covers the middle, so the wreath only has to read at the edges.
    stem_r = u * 0.415
    for sign in (-1, 1):
        arc = [(c[0] + math.cos(math.pi * (0.5 + sign * t)) * stem_r,
                c[1] + math.sin(math.pi * (0.5 + sign * t)) * stem_r)
               for t in [0.06 + i * 0.019 for i in range(31)]]
        d.line(arc, fill=GLOSS, width=int(u * 0.022), joint="curve")

        for i in range(8):
            t = 0.10 + i * 0.068
            a = math.pi * (0.5 + sign * t)
            lx = c[0] + math.cos(a) * stem_r
            ly = c[1] + math.sin(a) * stem_r
            leaf = [(x * u * 0.075, y * u * 0.030)
                    for x, y in circle_points(1.0, 0.0, 1.0, 24)]
            d.polygon(rotated(moved(leaf, lx, ly), (lx, ly),
                              a + sign * math.radians(52)), fill=GLOSS)

    save(outlined(img, u * 0.016), "paim_vsFrame.png", size)


# -------------------------------------------------------------- creator button

BTN_HALF = 0.455
BTN_RADIUS = 0.15


def button_plate(size, with_face=True):
    img = canvas(size)
    d = ImageDraw.Draw(img)
    u = size * SS
    c = (u / 2, u / 2)

    outer = rounded_square_points(c[0], c[1], u * BTN_HALF, u * BTN_RADIUS)
    d.polygon(outer, fill=BLACK)
    d.polygon(scaled(outer, c, 0.955), fill=SHADE)
    d.polygon(scaled(outer, c, 0.925), fill=BODY)
    d.polygon(scaled(outer, c, 0.900), fill=FACE)

    if with_face:
        inner = scaled(outer, c, 0.80)
        d.polygon(scaled(inner, c, 1.06), fill=BLACK)
        d.polygon(inner, fill=DARK)

    gloss = canvas(size)
    gd = ImageDraw.Draw(gloss)
    gd.polygon(scaled(outer, c, 0.90), fill=GLOSS)
    gd.rectangle([0, c[1], u, u], fill=(0, 0, 0, 0))
    gloss = gloss.filter(ImageFilter.GaussianBlur(u * 0.05))
    gloss.putalpha(gloss.getchannel("A").point(lambda a: int(a * 0.40)))
    img.alpha_composite(gloss)
    return img


def make_button():
    """the creatorlayer tile: plate, crossed swords, vs wordmark on top."""
    size = 192
    img = button_plate(size)
    u = size * SS
    c = (u / 2, u / 2)

    swords = canvas(size)
    draw_swords(ImageDraw.Draw(swords), (c[0], c[1] - u * 0.03), u * 0.56,
                math.radians(36))
    swords = outlined(swords, u * 0.016)
    swords.putalpha(swords.getchannel("A").point(lambda a: int(a * 0.85)))
    img.alpha_composite(swords)

    letters = vs_letters(size, 0.34)
    letters = outlined(letters, u * 0.024)
    img.alpha_composite(letters)

    save(img, "paim_vsBtn.png", size)

    # flat variant without the recessed face, for the hub tabs and toolbars.
    plain = button_plate(size, with_face=False)
    letters = vs_letters(size, 0.44)
    plain.alpha_composite(outlined(letters, u * 0.024))
    save(plain, "paim_vsBtnFlat.png", size)


def make_chip():
    """rank chip pinned to the button corner: small plate for a tier glyph."""
    size = 96
    img = canvas(size)
    d = ImageDraw.Draw(img)
    u = size * SS
    c = (u / 2, u / 2)
    outer = rounded_square_points(c[0], c[1], u * 0.44, u * 0.20)
    d.polygon(outer, fill=BLACK)
    d.polygon(scaled(outer, c, 0.90), fill=GLOSS)
    d.polygon(scaled(outer, c, 0.72), fill=BLACK)
    d.polygon(scaled(outer, c, 0.66), fill=DARK)
    save(img, "paim_vsChip.png", size)


def make_pip():
    """division pip: one filled diamond, tinted per division."""
    size = 48
    img = canvas(size)
    d = ImageDraw.Draw(img)
    u = size * SS
    c = u / 2
    d.polygon([(c, u * 0.06), (u * 0.94, c), (c, u * 0.94), (u * 0.06, c)],
              fill=BLACK)
    d.polygon([(c, u * 0.20), (u * 0.80, c), (c, u * 0.80), (u * 0.20, c)],
              fill=GLOSS)
    save(img, "paim_vsPip.png", size)


# ----------------------------------------------------------------- card plate

CARD_W, CARD_H, CARD_R = 0.33, 0.46, 0.09


def card_outline(u):
    return rounded_rect_points(u / 2, u / 2, u * CARD_W, u * CARD_H, u * CARD_R)


def make_card():
    size = 256
    img = canvas(size)
    d = ImageDraw.Draw(img)
    u = size * SS
    c = (u / 2, u / 2)

    outer = card_outline(u)
    d.polygon(outer, fill=BLACK)
    d.polygon(scaled(outer, c, 0.94), fill=GLOSS)

    body = canvas(size)
    ImageDraw.Draw(body).polygon(scaled(outer, c, 0.94), fill=BLACK)
    ramp = Image.linear_gradient("L").resize((u, u)).point(lambda v: int(v * 0.38))
    shade = Image.new("RGBA", (u, u), (0, 0, 0, 255))
    shade.putalpha(ImageChops.multiply(ramp, body.getchannel("A")))
    img.alpha_composite(shade)

    # recessed art window in the upper two thirds; the name banner sits below.
    win = rounded_rect_points(c[0], c[1] - u * 0.07, u * 0.25, u * 0.27,
                              u * 0.05)
    d.polygon(scaled(win, (c[0], c[1] - u * 0.06), 1.06), fill=BLACK)
    d.polygon(win, fill=DARK)

    save(img, "paim_vsCard.png", size)


def make_card_fill():
    size = 256
    img = canvas(size)
    u = size * SS
    ImageDraw.Draw(img).polygon(scaled(card_outline(u), (u / 2, u / 2), 0.94),
                                fill=GLOSS)
    save(img, "paim_vsCardFill.png", size)


def make_card_ring():
    """rarity rim drawn on the card footprint, so it lines up when scaled."""
    size = 256
    img = canvas(size)
    d = ImageDraw.Draw(img)
    u = size * SS
    c = (u / 2, u / 2)
    d.polygon(scaled(card_outline(u), c, 0.94), fill=GLOSS)
    d.polygon(scaled(card_outline(u), c, 0.855), fill=(0, 0, 0, 0))
    save(img, "paim_vsCardRing.png", size)


def make_card_back():
    size = 256
    img = canvas(size)
    d = ImageDraw.Draw(img)
    u = size * SS
    c = (u / 2, u / 2)

    outer = card_outline(u)
    d.polygon(outer, fill=BLACK)
    d.polygon(scaled(outer, c, 0.94), fill=DARK)
    d.polygon(scaled(outer, c, 0.86), fill=BLACK)
    d.polygon(scaled(outer, c, 0.83), fill=FACE)
    d.polygon(scaled(outer, c, 0.79), fill=DARK)

    swords = canvas(size)
    draw_swords(ImageDraw.Draw(swords), c, u * 0.52, math.radians(38))
    swords = outlined(swords, u * 0.014)
    img.alpha_composite(swords)
    save(img, "paim_vsCardBack.png", size)


# ------------------------------------------------------------------- duel bar

def make_bar():
    """two-sided duel bar for the in-level hud: rim, and a separate fill."""
    size = 512
    u = size * SS

    img = Image.new("RGBA", (u, int(u * 0.16)), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    h = img.height
    d.rounded_rectangle([0, 0, u - 1, h - 1], radius=h / 2, fill=BLACK)
    d.rounded_rectangle([h * 0.14, h * 0.14, u - 1 - h * 0.14, h - 1 - h * 0.14],
                        radius=h / 2, fill=GLOSS)
    d.rounded_rectangle([h * 0.30, h * 0.30, u - 1 - h * 0.30, h - 1 - h * 0.30],
                        radius=h / 2, fill=(0, 0, 0, 0))
    out = img.resize((size, int(size * 0.16)), Image.LANCZOS)
    out.save(os.path.abspath(os.path.join(OUT, "paim_vsBar.png")))
    print("wrote paim_vsBar.png", out.size)

    fill = Image.new("RGBA", (u, int(u * 0.16)), (0, 0, 0, 0))
    fd = ImageDraw.Draw(fill)
    fd.rounded_rectangle([h * 0.30, h * 0.30, u - 1 - h * 0.30, h - 1 - h * 0.30],
                         radius=h / 2, fill=GLOSS)
    ramp = Image.linear_gradient("L").rotate(90, expand=True) \
        .resize(fill.size).point(lambda v: int(v * 0.30))
    shade = Image.new("RGBA", fill.size, (0, 0, 0, 255))
    shade.putalpha(ImageChops.multiply(ramp, fill.getchannel("A")))
    fill.alpha_composite(shade)
    out = fill.resize((size, int(size * 0.16)), Image.LANCZOS)
    out.save(os.path.abspath(os.path.join(OUT, "paim_vsBarFill.png")))
    print("wrote paim_vsBarFill.png", out.size)


def make_versus_burst():
    """radial speed lines behind the match-found portraits."""
    size = 256
    img = canvas(size)
    d = ImageDraw.Draw(img)
    u = size * SS
    c = u / 2
    for i in range(28):
        a = i * 2 * math.pi / 28
        w = 0.035 if i % 2 else 0.055
        d.polygon([
            (c + math.cos(a - w) * u * 0.10, c + math.sin(a - w) * u * 0.10),
            (c + math.cos(a + w) * u * 0.10, c + math.sin(a + w) * u * 0.10),
            (c + math.cos(a + w * 0.4) * u * 0.70,
             c + math.sin(a + w * 0.4) * u * 0.70),
            (c + math.cos(a - w * 0.4) * u * 0.70,
             c + math.sin(a - w * 0.4) * u * 0.70),
        ], fill=GLOSS)
    img = img.filter(ImageFilter.GaussianBlur(u * 0.004))
    save(img, "paim_vsBurst.png", size)


if __name__ == "__main__":
    make_button()
    make_chip()
    make_pip()
    make_logo()
    make_swords()
    make_badge_frame()
    make_card()
    make_card_fill()
    make_card_ring()
    make_card_back()
    make_glyphs()
    make_bar()
    make_versus_burst()
