#!/usr/bin/env python3
"""white bodies over black rims: setColor() tints the body and keeps the outline."""

import math
import os

from PIL import Image, ImageDraw, ImageFilter

SS = 4
OUT = os.path.join(os.path.dirname(__file__), "..", "..")

BLACK = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)
SOFT = (255, 255, 255, 150)
FAINT = (255, 255, 255, 80)
GREY = (200, 200, 205, 255)
CLEAR = (0, 0, 0, 0)


def canvas(w, h=None):
    return Image.new("RGBA", (w * SS, (h or w) * SS), CLEAR)


def save(img, name, w, h=None):
    out = img.resize((w, h or w), Image.LANCZOS)
    path = os.path.abspath(os.path.join(OUT, name))
    out.save(path)
    print("wrote", os.path.relpath(path, OUT), out.size)


def outlined(img, width):
    radius = max(1, int(width))
    grown = img.getchannel("A").point(lambda a: 255 if a > 8 else 0)
    grown = grown.filter(ImageFilter.MaxFilter(radius * 2 + 1))
    rim = Image.new("RGBA", img.size, BLACK)
    rim.putalpha(grown)
    rim.alpha_composite(img)
    return rim


def punch(img, mask):
    alpha = img.getchannel("A")
    alpha.paste(0, mask=mask)
    img.putalpha(alpha)


class Glyph:
    """draws into a unit square, y down."""

    def __init__(self, size):
        self.size = size
        self.img = canvas(size)
        self.d = ImageDraw.Draw(self.img)

    def u(self, v):
        return v * self.size * SS

    def pts(self, points):
        return [(self.u(x), self.u(y)) for x, y in points]

    def rect(self, x0, y0, x1, y1, r=0.0, fill=WHITE, outline=None, width=0.0):
        self.d.rounded_rectangle(
            [self.u(x0), self.u(y0), self.u(x1), self.u(y1)], radius=self.u(r),
            fill=fill, outline=outline, width=int(self.u(width)) if width else 0)

    def ring_rect(self, x0, y0, x1, y1, r, w, fill=WHITE):
        self.rect(x0, y0, x1, y1, r, fill=None, outline=fill, width=w)

    def circle(self, cx, cy, r, fill=WHITE, outline=None, width=0.0):
        self.d.ellipse([self.u(cx - r), self.u(cy - r), self.u(cx + r), self.u(cy + r)],
                       fill=fill, outline=outline, width=int(self.u(width)) if width else 0)

    def poly(self, points, fill=WHITE):
        self.d.polygon(self.pts(points), fill=fill)

    def line(self, points, w, fill=WHITE):
        self.d.line(self.pts(points), fill=fill, width=int(self.u(w)), joint="curve")
        for x, y in (points[0], points[-1]):
            self.circle(x, y, w / 2, fill=fill)

    def arc(self, cx, cy, r, start, end, w, fill=WHITE):
        box = [self.u(cx - r), self.u(cy - r), self.u(cx + r), self.u(cy + r)]
        self.d.arc(box, start, end, fill=fill, width=int(self.u(w)))

    def head(self, x, y, angle, length, spread=0.62, fill=WHITE):
        a = math.radians(angle)
        back = (x - math.cos(a) * length, y - math.sin(a) * length)
        side = length * spread
        nx, ny = -math.sin(a) * side, math.cos(a) * side
        self.poly([(x, y), (back[0] + nx, back[1] + ny), (back[0] - nx, back[1] - ny)], fill)

    def erase(self, draw):
        mask = Image.new("L", self.img.size, 0)
        draw(ImageDraw.Draw(mask), self)
        punch(self.img, mask)

    def finish(self, name, rim=0.07):
        save(outlined(self.img, self.size * SS * rim), name, self.size)


# ------------------------------------------------------------------ glyphs

def frame_body(g, x0=0.16, y0=0.18, x1=0.72, y1=0.74):
    g.ring_rect(x0, y0, x1, y1, 0.09, 0.09)
    g.rect(x0 + 0.13, y0 + 0.13, x1 - 0.13, y1 - 0.13, 0.03, fill=SOFT)


def badge_plus(g, cx=0.72, cy=0.72):
    g.rect(cx - 0.06, cy - 0.2, cx + 0.06, cy + 0.2, 0.03)
    g.rect(cx - 0.2, cy - 0.06, cx + 0.2, cy + 0.06, 0.03)


def g_frame_add(g):
    frame_body(g)
    badge_plus(g)


def g_frame_dup(g):
    g.ring_rect(0.30, 0.10, 0.86, 0.62, 0.09, 0.08, fill=SOFT)
    g.rect(0.12, 0.34, 0.68, 0.88, 0.09)
    g.rect(0.25, 0.47, 0.55, 0.75, 0.03, fill=GREY)


def g_frame_del(g):
    frame_body(g)
    for a in (45, -45):
        r = math.radians(a)
        dx, dy = math.cos(r) * 0.17, math.sin(r) * 0.17
        g.line([(0.72 - dx, 0.72 - dy), (0.72 + dx, 0.72 + dy)], 0.11)


def corners(g, x0, y0, x1, y1, arm, w):
    for (x, y, sx, sy) in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
        g.line([(x, y + sy * arm), (x, y), (x + sx * arm, y)], w)


def g_select(g):
    corners(g, 0.14, 0.14, 0.86, 0.86, 0.22, 0.09)
    g.rect(0.36, 0.36, 0.64, 0.64, 0.05)


def g_assign(g):
    corners(g, 0.12, 0.10, 0.88, 0.58, 0.16, 0.08)
    g.line([(0.5, 0.20), (0.5, 0.62)], 0.11)
    g.head(0.5, 0.82, 90, 0.24)
    g.rect(0.18, 0.84, 0.82, 0.93, 0.04)


def g_onion(g):
    g.circle(0.30, 0.52, 0.22, fill=FAINT)
    g.circle(0.50, 0.50, 0.25, fill=SOFT)
    g.circle(0.68, 0.48, 0.28)


def g_ghost(g):
    g.d.pieslice([g.u(0.18), g.u(0.10), g.u(0.82), g.u(0.74)], 180, 360, fill=WHITE)
    g.rect(0.18, 0.42, 0.82, 0.78)
    for i in range(3):
        x = 0.18 + i * 0.2133
        g.poly([(x, 0.77), (x + 0.2133, 0.77), (x + 0.1066, 0.92)])

    def eyes(d, s):
        for cx in (0.38, 0.62):
            d.ellipse([s.u(cx - 0.07), s.u(0.36), s.u(cx + 0.07), s.u(0.52)], fill=255)
    g.erase(eyes)


def g_layers(g):
    for i, fill in enumerate((FAINT, SOFT, WHITE)):
        y = 0.62 - i * 0.2
        g.poly([(0.5, y - 0.16), (0.9, y), (0.5, y + 0.16), (0.1, y)], fill)


def g_eye(g, closed=False):
    upper = [(0.06 + 0.88 * t, 0.5 - math.sin(math.pi * t) * 0.30) for t in (i / 24 for i in range(25))]
    lower = [(0.94 - 0.88 * t, 0.5 + math.sin(math.pi * t) * 0.30) for t in (i / 24 for i in range(25))]
    g.poly(upper + lower)

    def pupil(d, s):
        d.ellipse([s.u(0.34), s.u(0.34), s.u(0.66), s.u(0.66)], fill=255)
    g.erase(pupil)
    g.circle(0.5, 0.5, 0.09)
    if closed:
        g.line([(0.14, 0.86), (0.86, 0.14)], 0.1)


def g_lock(g, open_=False):
    g.rect(0.18, 0.44, 0.82, 0.90, 0.08)
    if open_:
        g.arc(0.66, 0.30, 0.19, 180, 360, 0.09)
        g.rect(0.425, 0.28, 0.515, 0.46)
    else:
        g.arc(0.5, 0.40, 0.21, 180, 360, 0.09)
        g.rect(0.245, 0.38, 0.335, 0.48)
        g.rect(0.665, 0.38, 0.755, 0.48)

    def hole(d, s):
        d.ellipse([s.u(0.44), s.u(0.56), s.u(0.56), s.u(0.68)], fill=255)
        d.rectangle([s.u(0.475), s.u(0.62), s.u(0.525), s.u(0.78)], fill=255)
    g.erase(hole)


def g_bake(g):
    g.poly([(0.58, 0.04), (0.20, 0.56), (0.46, 0.56), (0.36, 0.96), (0.80, 0.40),
            (0.54, 0.40), (0.66, 0.04)])


def circular_arrow(g, r=0.30, cx=0.52, cy=0.46):
    start, end = 285, 525
    g.arc(cx, cy, r, start, end, 0.12)
    a = math.radians(end)
    tangent = math.atan2(math.cos(a), -math.sin(a))
    tip_x = cx + math.cos(a) * r + math.cos(tangent) * 0.16
    tip_y = cy + math.sin(a) * r + math.sin(tangent) * 0.16
    g.head(tip_x, tip_y, math.degrees(tangent), 0.30, spread=0.55)


def g_loop(g):
    circular_arrow(g)


def g_once(g):
    g.line([(0.10, 0.5), (0.58, 0.5)], 0.12)
    g.head(0.80, 0.5, 0, 0.30)
    g.rect(0.80, 0.18, 0.90, 0.82, 0.03)


def g_pingpong(g):
    g.line([(0.28, 0.5), (0.72, 0.5)], 0.12)
    g.head(0.94, 0.5, 0, 0.28)
    g.head(0.06, 0.5, 180, 0.28)


def g_repeat(g):
    circular_arrow(g)
    g.circle(0.52, 0.46, 0.1)


def g_reverse(g):
    g.line([(0.42, 0.5), (0.90, 0.5)], 0.12)
    g.head(0.08, 0.5, 180, 0.30)


def g_skip(g):
    frame_body(g, 0.14, 0.14, 0.86, 0.86)
    g.line([(0.20, 0.80), (0.80, 0.20)], 0.11)


def g_tag(g):
    g.poly([(0.10, 0.30), (0.56, 0.30), (0.90, 0.5), (0.56, 0.70), (0.10, 0.70)])

    def hole(d, s):
        d.ellipse([s.u(0.58), s.u(0.43), s.u(0.72), s.u(0.57)], fill=255)
    g.erase(hole)


def g_key(g):
    g.poly([(0.5, 0.08), (0.92, 0.5), (0.5, 0.92), (0.08, 0.5)])


def g_empty(g):
    g.circle(0.5, 0.5, 0.36, fill=None, outline=WHITE, width=0.12)


def g_gear(g):
    teeth = 8
    pts = []
    for i in range(teeth * 4):
        a = i * math.pi * 2 / (teeth * 4)
        r = 0.44 if (i % 4) in (1, 2) else 0.33
        pts.append((0.5 + math.cos(a) * r, 0.5 + math.sin(a) * r))
    g.poly(pts)

    def hole(d, s):
        d.ellipse([s.u(0.36), s.u(0.36), s.u(0.64), s.u(0.64)], fill=255)
    g.erase(hole)


def g_film(g):
    g.rect(0.06, 0.18, 0.94, 0.82, 0.07)

    def holes(d, s):
        for i in range(5):
            x = 0.12 + i * 0.165
            for y in (0.22, 0.70):
                d.rounded_rectangle([s.u(x), s.u(y), s.u(x + 0.09), s.u(y + 0.08)],
                                    radius=s.u(0.02), fill=255)
        d.rounded_rectangle([s.u(0.16), s.u(0.36), s.u(0.84), s.u(0.64)], radius=s.u(0.03), fill=255)
    g.erase(holes)


def g_playhead(g):
    g.poly([(0.16, 0.04), (0.84, 0.04), (0.84, 0.34), (0.5, 0.58), (0.16, 0.34)])
    g.rect(0.44, 0.50, 0.56, 0.98, 0.03)


GLYPHS = {
    "frameAdd": g_frame_add,
    "frameDup": g_frame_dup,
    "frameDel": g_frame_del,
    "select": g_select,
    "assign": g_assign,
    "onion": g_onion,
    "ghost": g_ghost,
    "layers": g_layers,
    "eye": g_eye,
    "eyeOff": lambda g: g_eye(g, closed=True),
    "lock": g_lock,
    "unlock": lambda g: g_lock(g, open_=True),
    "bake": g_bake,
    "loop": g_loop,
    "once": g_once,
    "pingpong": g_pingpong,
    "repeat": g_repeat,
    "reverse": g_reverse,
    "skip": g_skip,
    "tag": g_tag,
    "key": g_key,
    "empty": g_empty,
    "gear": g_gear,
    "film": g_film,
}


def make_glyphs():
    for name, draw in GLYPHS.items():
        g = Glyph(80)
        draw(g)
        g.finish(f"paim_anim_{name}.png")
    g = Glyph(48)
    g_playhead(g)
    g.finish("paim_anim_playhead.png", rim=0.06)


# ------------------------------------------------------------------ timeline cells

def make_cells():
    # tinted per clip at runtime; sprocket holes read as film without a texture.
    w, h = 112, 136
    img = canvas(w, h)
    d = ImageDraw.Draw(img)
    s = SS
    d.rounded_rectangle([3 * s, 3 * s, (w - 3) * s, (h - 3) * s], radius=16 * s, fill=WHITE)
    mask = Image.new("L", img.size, 0)
    md = ImageDraw.Draw(mask)
    for i in range(4):
        x = 14 + i * 23
        for y in (9, h - 21):
            md.rounded_rectangle([x * s, y * s, (x + 12) * s, (y + 12) * s], radius=3 * s, fill=255)
    punch(img, mask)
    inner = Image.new("RGBA", img.size, CLEAR)
    ImageDraw.Draw(inner).rounded_rectangle(
        [12 * s, 28 * s, (w - 12) * s, (h - 28) * s], radius=8 * s, fill=(150, 150, 150, 255))
    img.alpha_composite(inner)
    save(outlined(img, 4 * s), "paim_anim_cell.png", w, h)

    glow = canvas(w + 16, h + 16)
    gd = ImageDraw.Draw(glow)
    gd.rounded_rectangle([4 * s, 4 * s, (w + 12) * s, (h + 12) * s], radius=20 * s,
                         outline=WHITE, width=7 * s)
    glow = glow.filter(ImageFilter.GaussianBlur(2 * s))
    sharp = canvas(w + 16, h + 16)
    ImageDraw.Draw(sharp).rounded_rectangle(
        [6 * s, 6 * s, (w + 10) * s, (h + 10) * s], radius=18 * s, outline=WHITE, width=4 * s)
    glow.alpha_composite(sharp)
    save(glow, "paim_anim_cellGlow.png", w + 16, h + 16)


# ------------------------------------------------------------------ toolbar button

def make_button():
    size = 200
    img = canvas(size)
    d = ImageDraw.Draw(img)
    s = SS * size / 100.0

    def P(x, y):
        return (x * s, y * s)

    def strip(cx, cy, angle, body, frames):
        layer = Image.new("RGBA", img.size, CLEAR)
        ld = ImageDraw.Draw(layer)
        ld.rounded_rectangle([P(cx - 40, cy - 17), P(cx + 40, cy + 17)], radius=6 * s, fill=body)
        mask = Image.new("L", img.size, 0)
        md = ImageDraw.Draw(mask)
        for i in range(7):
            x = cx - 36 + i * 11
            for y in (cy - 14, cy + 9):
                md.rounded_rectangle([P(x, y), P(x + 6, y + 5)], radius=1.2 * s, fill=255)
        punch(layer, mask)
        for i, color in enumerate(frames):
            x = cx - 34 + i * 23.3
            ld.rounded_rectangle([P(x, cy - 7), P(x + 21, cy + 7)], radius=2.5 * s, fill=color)
        layer = layer.rotate(angle, resample=Image.BICUBIC, center=P(cx, cy))
        return outlined(layer, 3.2 * s)

    img.alpha_composite(strip(50, 34, 10, (48, 52, 68, 255),
                              [(0, 170, 255, 255), (70, 205, 255, 255), (150, 230, 255, 255)]))
    img.alpha_composite(strip(50, 62, -8, (40, 44, 58, 255),
                              [(255, 160, 0, 255), (255, 200, 40, 255), (255, 235, 120, 255)]))

    play = Image.new("RGBA", img.size, CLEAR)
    pd = ImageDraw.Draw(play)
    pd.polygon([P(52, 46), P(52, 94), P(92, 70)], fill=(130, 240, 70, 255))
    pd.polygon([P(56, 53), P(56, 67), P(80, 63)], fill=(200, 255, 160, 255))
    img.alpha_composite(outlined(play, 3.6 * s))
    save(img, "paim_animate.png", size)


if __name__ == "__main__":
    make_glyphs()
    make_cells()
    make_button()
