#!/usr/bin/env python3
"""editor tool art: white glyph bodies over black rims, plus the two colour toolbar buttons."""

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


def vertical_gradient(size, top, bottom):
    w, h = size
    grad = Image.new("RGBA", (1, h))
    for y in range(h):
        t = y / max(1, h - 1)
        grad.putpixel((0, y), tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(4)))
    return grad.resize((w, h))


def filled(mask, top, bottom):
    """mask (L) painted with a vertical gradient."""
    layer = vertical_gradient(mask.size, top, bottom)
    layer.putalpha(mask)
    return layer


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

    def ring(self, cx, cy, r, w, fill=WHITE):
        self.circle(cx, cy, r, fill=None, outline=fill, width=w)

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

    def arrow(self, x0, y0, x1, y1, w=0.13, head=0.30, fill=WHITE):
        angle = math.degrees(math.atan2(y1 - y0, x1 - x0))
        a = math.radians(angle)
        shaft_end = (x1 - math.cos(a) * head * 0.7, y1 - math.sin(a) * head * 0.7)
        self.line([(x0, y0), shaft_end], w, fill)
        self.head(x1, y1, angle, head, fill=fill)

    def erase(self, draw):
        mask = Image.new("L", self.img.size, 0)
        draw(ImageDraw.Draw(mask), self)
        punch(self.img, mask)

    def erase_circle(self, cx, cy, r):
        self.erase(lambda d, s: d.ellipse(
            [s.u(cx - r), s.u(cy - r), s.u(cx + r), s.u(cy + r)], fill=255))

    def rotate(self, degrees):
        self.img = self.img.rotate(degrees, resample=Image.BICUBIC)
        self.d = ImageDraw.Draw(self.img)

    def finish(self, name, rim=0.07):
        save(outlined(self.img, self.size * SS * rim), name, self.size)


# ------------------------------------------------------------------ shared glyphs

def badge_plus(g, cx=0.74, cy=0.74, arm=0.19, w=0.12):
    g.erase_circle(cx, cy, arm + 0.1)
    g.rect(cx - w / 2, cy - arm, cx + w / 2, cy + arm, 0.03)
    g.rect(cx - arm, cy - w / 2, cx + arm, cy + w / 2, 0.03)


def magnifier(g):
    g.ring(0.42, 0.42, 0.28, 0.11)
    g.circle(0.42, 0.42, 0.20, fill=FAINT)
    g.line([(0.63, 0.63), (0.86, 0.86)], 0.15)


def g_zoom_in(g):
    magnifier(g)
    g.rect(0.38, 0.29, 0.46, 0.55, 0.02)
    g.rect(0.29, 0.38, 0.55, 0.46, 0.02)


def g_zoom_out(g):
    magnifier(g)
    g.rect(0.29, 0.38, 0.55, 0.46, 0.02)


def corners(g, x0, y0, x1, y1, arm, w):
    for (x, y, sx, sy) in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
        g.line([(x, y + sy * arm), (x, y), (x + sx * arm, y)], w)


def g_fit(g):
    corners(g, 0.12, 0.12, 0.88, 0.88, 0.22, 0.10)
    g.circle(0.5, 0.5, 0.13)
    g.ring(0.5, 0.5, 0.25, 0.06, fill=SOFT)


def g_play(g):
    g.poly([(0.24, 0.10), (0.24, 0.90), (0.90, 0.50)])


def g_trash(g):
    g.rect(0.14, 0.18, 0.86, 0.28, 0.04)
    g.rect(0.38, 0.08, 0.62, 0.20, 0.04)
    g.poly([(0.20, 0.32), (0.80, 0.32), (0.72, 0.92), (0.28, 0.92)])

    def slots(d, s):
        for x in (0.36, 0.5, 0.64):
            d.rounded_rectangle([s.u(x - 0.035), s.u(0.42), s.u(x + 0.035), s.u(0.82)],
                                radius=s.u(0.02), fill=255)
    g.erase(slots)


def g_undo(g):
    g.arc(0.54, 0.56, 0.30, 200, 430, 0.13)
    g.head(0.08, 0.40, 160, 0.32, spread=0.7)


def g_clock(g):
    g.rect(0.40, 0.04, 0.60, 0.14, 0.03)
    g.rect(0.46, 0.10, 0.54, 0.22)
    g.circle(0.5, 0.57, 0.37)

    def face(d, s):
        d.ellipse([s.u(0.5 - 0.26), s.u(0.57 - 0.26), s.u(0.5 + 0.26), s.u(0.57 + 0.26)], fill=255)
    g.erase(face)
    g.line([(0.5, 0.57), (0.5, 0.38)], 0.08)
    g.line([(0.5, 0.57), (0.65, 0.62)], 0.08)
    g.circle(0.5, 0.57, 0.06)


def g_folder(g):
    g.poly([(0.08, 0.20), (0.38, 0.20), (0.46, 0.30), (0.92, 0.30), (0.92, 0.82), (0.08, 0.82)], SOFT)
    g.poly([(0.08, 0.40), (0.92, 0.40), (0.86, 0.84), (0.14, 0.84)])


def g_import(g):
    g.line([(0.10, 0.62), (0.10, 0.88), (0.90, 0.88), (0.90, 0.62)], 0.11)
    g.arrow(0.5, 0.06, 0.5, 0.72, w=0.15, head=0.34)


def g_hammer(g):
    g.rect(0.44, 0.34, 0.58, 0.96, 0.04)
    g.rect(0.14, 0.10, 0.80, 0.38, 0.06)
    g.rect(0.74, 0.16, 0.90, 0.32, 0.03, fill=SOFT)
    g.rotate(-35)


def g_badge(g):
    g.circle(0.5, 0.5, 0.42)
    g.arc(0.5, 0.5, 0.32, 200, 300, 0.07, fill=(255, 255, 255, 120))


# ------------------------------------------------------------------ physics glyphs

def g_gravity(g):
    g.arrow(0.5, 0.06, 0.5, 0.70, w=0.17, head=0.36)
    g.rect(0.12, 0.80, 0.88, 0.92, 0.04)


def g_bounce(g):
    g.rect(0.06, 0.84, 0.94, 0.94, 0.04)
    for t in (0.0, 0.25, 0.5, 0.75):
        g.circle(0.08 + t * 0.30, 0.16 + t * t * 0.60, 0.05, fill=SOFT)
    for t in (0.2, 0.45, 0.7):
        g.circle(0.42 + t * 0.26, 0.76 - math.sin(t * math.pi * 0.5) * 0.36, 0.05, fill=SOFT)
    g.circle(0.74, 0.30, 0.18)
    g.rect(0.30, 0.79, 0.50, 0.83, 0.02, fill=GREY)


def g_friction(g):
    g.rect(0.30, 0.22, 0.80, 0.62, 0.05)
    g.rect(0.06, 0.66, 0.94, 0.76, 0.03)
    for i in range(5):
        x = 0.12 + i * 0.18
        g.line([(x + 0.08, 0.80), (x, 0.92)], 0.06, fill=SOFT)
    for y in (0.30, 0.42, 0.54):
        g.line([(0.06, y), (0.20, y)], 0.06, fill=SOFT)


def g_drag(g):
    for y, x1, r in ((0.26, 0.64, 0.12), (0.52, 0.80, 0.12), (0.78, 0.56, 0.10)):
        g.line([(0.08, y), (x1, y)], 0.10)
        g.arc(x1, y - r, r, 270, 450, 0.10)
        g.circle(x1 - 0.02, y - 2 * r + 0.01, 0.05)


def g_vel_x(g):
    g.arrow(0.30, 0.5, 0.94, 0.5, w=0.16, head=0.36)
    for y, x0 in ((0.24, 0.10), (0.5, 0.04), (0.76, 0.10)):
        g.line([(x0, y), (x0 + 0.14, y)], 0.08, fill=SOFT)


def g_spin(g):
    cx, cy, r = 0.5, 0.52, 0.34
    start, end = 285, 525
    g.arc(cx, cy, r, start, end, 0.11)
    a = math.radians(end)
    tangent = math.atan2(math.cos(a), -math.sin(a))
    tip_x = cx + math.cos(a) * r + math.cos(tangent) * 0.15
    tip_y = cy + math.sin(a) * r + math.sin(tangent) * 0.15
    g.head(tip_x, tip_y, math.degrees(tangent), 0.30, spread=0.55)
    s = 0.17
    pts = [(cx + math.cos(math.radians(20 + 90 * i)) * s,
            cy + math.sin(math.radians(20 + 90 * i)) * s) for i in range(4)]
    g.poly(pts)


def g_quality(g):
    pts = [(0.06 + 0.88 * t, 0.5 - math.sin(t * math.pi * 2) * 0.28) for t in (i / 40 for i in range(41))]
    g.line(pts, 0.08, fill=SOFT)
    for i in range(6):
        t = (i + 0.5) / 6
        g.circle(0.06 + 0.88 * t, 0.5 - math.sin(t * math.pi * 2) * 0.28, 0.08)


def g_world(g):
    g.circle(0.5, 0.5, 0.40)

    def lines(d, s):
        d.ellipse([s.u(0.36), s.u(0.10), s.u(0.64), s.u(0.90)], outline=255, width=int(s.u(0.06)))
        d.rectangle([s.u(0.10), s.u(0.47), s.u(0.90), s.u(0.53)], fill=255)
    g.erase(lines)


def g_capture(g):
    g.ring(0.5, 0.5, 0.30, 0.10)
    for (x0, y0, x1, y1) in ((0.5, 0.02, 0.5, 0.26), (0.5, 0.74, 0.5, 0.98),
                             (0.02, 0.5, 0.26, 0.5), (0.74, 0.5, 0.98, 0.5)):
        g.line([(x0, y0), (x1, y1)], 0.10)
    g.circle(0.5, 0.5, 0.10)


def g_ball(g):
    for y, x0 in ((0.34, 0.04), (0.52, 0.02), (0.70, 0.06)):
        g.line([(x0, y), (x0 + 0.16, y)], 0.08, fill=SOFT)
    g.circle(0.60, 0.52, 0.32)
    g.arc(0.60, 0.52, 0.22, 200, 280, 0.07, fill=GREY)


def g_anchor(g):
    g.ring(0.5, 0.17, 0.10, 0.08)
    g.rect(0.45, 0.26, 0.55, 0.84, 0.02)
    g.rect(0.28, 0.36, 0.72, 0.45, 0.03)
    g.arc(0.5, 0.52, 0.34, 20, 160, 0.10)
    g.head(0.86, 0.58, -60, 0.18, spread=0.8)
    g.head(0.14, 0.58, 240, 0.18, spread=0.8)


def g_add_dynamic(g):
    g.circle(0.42, 0.42, 0.32)
    g.arc(0.42, 0.42, 0.22, 200, 280, 0.07, fill=GREY)
    badge_plus(g)


def g_add_static(g):
    g.rect(0.08, 0.10, 0.64, 0.66, 0.06)
    g.rect(0.18, 0.20, 0.54, 0.56, 0.03, fill=GREY)
    badge_plus(g)


def g_hitbox(g):
    g.circle(0.5, 0.5, 0.18, fill=SOFT)
    g.ring_rect(0.12, 0.12, 0.88, 0.88, 0.04, 0.08)

    def gaps(d, s):
        for t in (0.36, 0.64):
            k = 0.05
            d.rectangle([s.u(t - k), s.u(0.05), s.u(t + k), s.u(0.20)], fill=255)
            d.rectangle([s.u(t - k), s.u(0.80), s.u(t + k), s.u(0.95)], fill=255)
            d.rectangle([s.u(0.05), s.u(t - k), s.u(0.20), s.u(t + k)], fill=255)
            d.rectangle([s.u(0.80), s.u(t - k), s.u(0.95), s.u(t + k)], fill=255)
    g.erase(gaps)
    for x, y in ((0.12, 0.12), (0.88, 0.12), (0.12, 0.88), (0.88, 0.88)):
        g.rect(x - 0.08, y - 0.08, x + 0.08, y + 0.08, 0.02)


# ------------------------------------------------------------------ gif glyphs

def g_resolution(g):
    for row in range(3):
        for col in range(3):
            x = 0.08 + col * 0.29
            y = 0.08 + row * 0.29
            fill = WHITE if (row + col) % 2 == 0 else SOFT
            g.rect(x, y, x + 0.26, y + 0.26, 0.04, fill=fill)


def g_colors(g):
    g.circle(0.5, 0.52, 0.42)

    def holes(d, s):
        for cx, cy, r in ((0.34, 0.32, 0.08), (0.56, 0.26, 0.08), (0.74, 0.42, 0.08),
                          (0.30, 0.56, 0.08), (0.62, 0.70, 0.12)):
            d.ellipse([s.u(cx - r), s.u(cy - r), s.u(cx + r), s.u(cy + r)], fill=255)
    g.erase(holes)
    g.circle(0.62, 0.70, 0.06, fill=SOFT)


def g_budget(g):
    g.rect(0.06, 0.58, 0.46, 0.92, 0.05)
    g.rect(0.54, 0.58, 0.94, 0.92, 0.05, fill=SOFT)
    g.rect(0.30, 0.18, 0.70, 0.52, 0.05)
    g.rect(0.40, 0.04, 0.60, 0.12, 0.03, fill=SOFT)


def g_pixel(g):
    g.rect(0.06, 0.32, 0.66, 0.92, 0.05)
    g.rect(0.16, 0.42, 0.40, 0.66, 0.02, fill=GREY)
    g.line([(0.10, 0.12), (0.62, 0.12)], 0.06)
    g.head(0.06, 0.12, 180, 0.12, spread=0.8)
    g.head(0.66, 0.12, 0, 0.12, spread=0.8)
    g.line([(0.86, 0.36), (0.86, 0.88)], 0.06)
    g.head(0.86, 0.32, -90, 0.12, spread=0.8)
    g.head(0.86, 0.92, 90, 0.12, spread=0.8)


def g_background(g):
    cells = 4
    step = 0.80 / cells
    for row in range(cells):
        for col in range(cells):
            fill = WHITE if (row + col) % 2 == 0 else FAINT
            x = 0.10 + col * step
            y = 0.10 + row * step
            g.rect(x, y, x + step, y + step, fill=fill)


def g_tolerance(g):
    g.line([(0.14, 0.86), (0.60, 0.40)], 0.12)
    cx, cy = 0.70, 0.30
    pts = []
    for i in range(8):
        r = 0.24 if i % 2 == 0 else 0.10
        a = math.radians(-90 + i * 45)
        pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    g.poly(pts)
    g.circle(0.28, 0.20, 0.05, fill=SOFT)
    g.circle(0.88, 0.66, 0.05, fill=SOFT)


def g_sampling(g):
    for i, top in enumerate((0.66, 0.46, 0.30, 0.18)):
        x = 0.08 + i * 0.21
        g.rect(x, top, x + 0.19, 0.92, 0.03, fill=FAINT)
    curve = [(0.08 + 0.84 * t, 0.80 - (1 - (1 - t) ** 2) * 0.66) for t in (i / 30 for i in range(31))]
    g.line(curve, 0.12)


def g_dither(g):
    cells = 5
    step = 0.84 / cells
    for row in range(cells):
        for col in range(cells):
            density = 1 - col / (cells - 1)
            keep = (row + col) % 2 == 0 or density > 0.7
            if col == cells - 1 and row % 2:
                keep = False
            if not keep:
                continue
            x = 0.08 + col * step
            y = 0.08 + row * step
            g.rect(x + 0.01, y + 0.01, x + step - 0.01, y + step - 0.01, 0.02)


def g_glow(g):
    g.circle(0.5, 0.5, 0.36, fill=FAINT)
    g.circle(0.5, 0.5, 0.18)
    for i in range(8):
        a = math.radians(i * 45)
        r0, r1 = (0.28, 0.46) if i % 2 == 0 else (0.28, 0.38)
        g.line([(0.5 + math.cos(a) * r0, 0.5 + math.sin(a) * r0),
                (0.5 + math.cos(a) * r1, 0.5 + math.sin(a) * r1)], 0.08)


def g_mode_blocks(g):
    for row in range(2):
        for col in range(2):
            x = 0.10 + col * 0.42
            y = 0.10 + row * 0.42
            fill = WHITE if (row + col) % 2 == 0 else GREY
            g.rect(x, y, x + 0.38, y + 0.38, 0.05, fill=fill)


def g_mode_art(g):
    g.poly([(0.5, 0.94), (0.20, 0.50), (0.32, 0.18), (0.68, 0.18), (0.80, 0.50)])
    g.rect(0.30, 0.06, 0.70, 0.16, 0.03)

    def slit(d, s):
        d.ellipse([s.u(0.43), s.u(0.43), s.u(0.57), s.u(0.57)], fill=255)
        d.rectangle([s.u(0.48), s.u(0.55), s.u(0.52), s.u(0.92)], fill=255)
    g.erase(slit)


def g_mode_paint(g):
    g.line([(0.88, 0.10), (0.48, 0.50)], 0.13)
    g.rect(0.40, 0.44, 0.58, 0.62, 0.03, fill=GREY)
    g.poly([(0.36, 0.52), (0.50, 0.66), (0.36, 0.84), (0.10, 0.92), (0.16, 0.66)])


def sparkle(g, cx, cy, r, fill=WHITE):
    k = r * 0.28
    g.poly([(cx, cy - r), (cx + k, cy - k), (cx + r, cy), (cx + k, cy + k),
            (cx, cy + r), (cx - k, cy + k), (cx - r, cy), (cx - k, cy - k)], fill)


def g_mode_render(g):
    sparkle(g, 0.42, 0.56, 0.38)
    sparkle(g, 0.78, 0.22, 0.18, fill=SOFT)


def g_mode_free(g):
    g.circle(0.32, 0.58, 0.20)
    g.circle(0.54, 0.42, 0.26)
    g.circle(0.74, 0.60, 0.18)
    g.rect(0.14, 0.58, 0.88, 0.78, 0.10)


def g_mode_circles(g):
    g.circle(0.66, 0.36, 0.26, fill=SOFT)
    g.circle(0.34, 0.62, 0.28)
    g.circle(0.72, 0.76, 0.14, fill=GREY)


def g_mode_blur(g):
    g.circle(0.5, 0.5, 0.44, fill=FAINT)
    g.circle(0.5, 0.5, 0.33, fill=SOFT)
    g.circle(0.5, 0.5, 0.20)


def gradient_bar(g, x0, y0, x1, y1, top, bottom):
    mask = Image.new("L", g.img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [g.u(x0), g.u(y0), g.u(x1), g.u(y1)], radius=g.u(0.05), fill=255)
    g.img.alpha_composite(filled(mask, top, bottom))


def g_mode_vert(g):
    for i, x in enumerate((0.08, 0.38, 0.68)):
        gradient_bar(g, x, 0.08 + i * 0.06, x + 0.24, 0.92, WHITE, (255, 255, 255, 60))


def g_mode_vertx(g):
    gradient_bar(g, 0.08, 0.08, 0.92, 0.92, WHITE, (255, 255, 255, 50))
    sparkle(g, 0.70, 0.30, 0.18, fill=BLACK)
    sparkle(g, 0.70, 0.30, 0.12)


GLYPHS = {
    "paim_ui_zoomIn": g_zoom_in,
    "paim_ui_zoomOut": g_zoom_out,
    "paim_ui_fit": g_fit,
    "paim_ui_play": g_play,
    "paim_ui_trash": g_trash,
    "paim_ui_undo": g_undo,
    "paim_ui_clock": g_clock,
    "paim_ui_folder": g_folder,
    "paim_ui_import": g_import,
    "paim_ui_hammer": g_hammer,
    "paim_ui_badge": g_badge,
    "paim_phys_gravity": g_gravity,
    "paim_phys_bounce": g_bounce,
    "paim_phys_friction": g_friction,
    "paim_phys_drag": g_drag,
    "paim_phys_velX": g_vel_x,
    "paim_phys_velY": lambda g: (g_vel_x(g), g.rotate(90)),
    "paim_phys_spin": g_spin,
    "paim_phys_quality": g_quality,
    "paim_phys_world": g_world,
    "paim_phys_capture": g_capture,
    "paim_phys_ball": g_ball,
    "paim_phys_anchor": g_anchor,
    "paim_phys_addDyn": g_add_dynamic,
    "paim_phys_addStatic": g_add_static,
    "paim_phys_hitbox": g_hitbox,
    "paim_gif_resolution": g_resolution,
    "paim_gif_colors": g_colors,
    "paim_gif_budget": g_budget,
    "paim_gif_pixel": g_pixel,
    "paim_gif_background": g_background,
    "paim_gif_tolerance": g_tolerance,
    "paim_gif_sampling": g_sampling,
    "paim_gif_dither": g_dither,
    "paim_gif_glow": g_glow,
    "paim_gif_modeBlocks": g_mode_blocks,
    "paim_gif_modeArt": g_mode_art,
    "paim_gif_modePaint": g_mode_paint,
    "paim_gif_modeRender": g_mode_render,
    "paim_gif_modeFree": g_mode_free,
    "paim_gif_modeCircles": g_mode_circles,
    "paim_gif_modeBlur": g_mode_blur,
    "paim_gif_modeVert": g_mode_vert,
    "paim_gif_modeVertX": g_mode_vertx,
}


def make_glyphs():
    for name, draw in GLYPHS.items():
        g = Glyph(80)
        draw(g)
        g.finish(f"{name}.png")


# ------------------------------------------------------------------ stepper buttons

def make_steppers():
    # same build as GJ_plus2Btn: gloss disc, dark rim, drop shadow; the sign is baked in
    size = 72
    s = SS
    for name, plus, top, bottom in (
        ("paim_ui_stepPlus.png", True, (120, 255, 140, 255), (20, 175, 60, 255)),
        ("paim_ui_stepMinus.png", False, (255, 150, 120, 255), (215, 50, 50, 255)),
    ):
        img = canvas(size)
        c, r = size * s / 2, size * s * 0.40

        shadow = Image.new("L", img.size, 0)
        ImageDraw.Draw(shadow).ellipse([c - r + 3 * s, c - r + 5 * s, c + r + 3 * s, c + r + 5 * s], fill=110)
        shade = Image.new("RGBA", img.size, BLACK)
        shade.putalpha(shadow.filter(ImageFilter.GaussianBlur(2 * s)))
        img.alpha_composite(shade)

        disc = Image.new("L", img.size, 0)
        ImageDraw.Draw(disc).ellipse([c - r, c - r, c + r, c + r], fill=255)
        rim = Image.new("RGBA", img.size, BLACK)
        rim.putalpha(disc.filter(ImageFilter.MaxFilter(int(4 * s) * 2 + 1)))
        img.alpha_composite(rim)
        img.alpha_composite(filled(disc, top, bottom))

        gloss = Image.new("L", img.size, 0)
        ImageDraw.Draw(gloss).ellipse([c - r * 0.78, c - r * 0.92, c + r * 0.78, c - r * 0.02], fill=70)
        shine = Image.new("RGBA", img.size, WHITE)
        shine.putalpha(gloss)
        img.alpha_composite(shine)

        sign = Image.new("RGBA", img.size, CLEAR)
        sd = ImageDraw.Draw(sign)
        arm, w = r * 0.55, r * 0.22
        sd.rounded_rectangle([c - arm, c - w, c + arm, c + w], radius=w * 0.4, fill=WHITE)
        if plus:
            sd.rounded_rectangle([c - w, c - arm, c + w, c + arm], radius=w * 0.4, fill=WHITE)
        img.alpha_composite(outlined(sign, 3 * s))
        save(img, name, size)


# ------------------------------------------------------------------ toolbar buttons

def P(x, y, size):
    k = SS * size / 100.0
    return (x * k, y * k)


def layer_for(size):
    return Image.new("RGBA", (size * SS, size * SS), CLEAR)


def make_physics_button():
    size = 200
    k = SS * size / 100.0
    img = canvas(size)

    def p(x, y):
        return P(x, y, size)

    ground = layer_for(size)
    gd = ImageDraw.Draw(ground)
    gd.rounded_rectangle([p(6, 76), p(94, 92)], radius=4 * k, fill=(64, 70, 96, 255))
    gd.rounded_rectangle([p(6, 76), p(94, 80)], radius=2 * k, fill=(120, 132, 170, 255))
    img.alpha_composite(outlined(ground, 3.2 * k))

    trail = layer_for(size)
    td = ImageDraw.Draw(trail)
    for i in range(6):
        t = (i + 1) / 7
        x = 22 + t * 22
        y = 26 + (t ** 2) * 48
        r = 2.4 + t * 0.8
        td.ellipse([p(x - r, y - r), p(x + r, y + r)], fill=(255, 255, 255, 235))
    for i in range(4):
        t = (i + 1) / 5
        x = 44 + t * 14
        y = 74 - math.sin(t * math.pi * 0.9) * 22
        r = 2.6
        td.ellipse([p(x - r, y - r), p(x + r, y + r)], fill=(255, 255, 255, 235))
    img.alpha_composite(outlined(trail, 2.2 * k))

    sparks = layer_for(size)
    sd = ImageDraw.Draw(sparks)
    for a in (200, 235, 305, 340):
        rad = math.radians(a)
        x0, y0 = 44 + math.cos(rad) * 6, 74 + math.sin(rad) * 6
        x1, y1 = 44 + math.cos(rad) * 12, 74 + math.sin(rad) * 12
        sd.line([p(x0, y0), p(x1, y1)], fill=(255, 230, 90, 255), width=int(3 * k))
    img.alpha_composite(outlined(sparks, 1.8 * k))

    crate = layer_for(size)
    cd = ImageDraw.Draw(crate)
    cd.rounded_rectangle([p(56, 40), p(90, 74)], radius=3 * k, fill=(255, 150, 30, 255))
    cd.rounded_rectangle([p(56, 40), p(90, 46)], radius=2 * k, fill=(255, 205, 90, 255))
    cd.rectangle([p(62, 50), p(84, 68)], outline=(150, 70, 0, 255), width=int(2.4 * k))
    cd.line([p(62, 50), p(84, 68)], fill=(150, 70, 0, 255), width=int(2.4 * k))
    crate = crate.rotate(12, resample=Image.BICUBIC, center=p(73, 70))
    img.alpha_composite(outlined(crate, 3.4 * k))

    ball = layer_for(size)
    bd = ImageDraw.Draw(ball)
    bd.ellipse([p(10, 10), p(36, 36)], fill=(0, 190, 255, 255))
    bd.ellipse([p(14, 13), p(30, 25)], fill=(140, 235, 255, 255))
    bd.ellipse([p(16, 15), p(22, 20)], fill=(255, 255, 255, 255))
    img.alpha_composite(outlined(ball, 3.4 * k))

    save(img, "paim_physics.png", size)


def make_gif_button():
    size = 200
    k = SS * size / 100.0
    img = canvas(size)

    def p(x, y):
        return P(x, y, size)

    card = layer_for(size)
    cd = ImageDraw.Draw(card)
    cd.rounded_rectangle([p(6, 12), p(70, 72)], radius=5 * k, fill=(245, 245, 250, 255))
    sky = Image.new("L", card.size, 0)
    ImageDraw.Draw(sky).rounded_rectangle([p(12, 24), p(64, 66)], radius=2.5 * k, fill=255)
    card.alpha_composite(filled(sky, (90, 200, 255, 255), (30, 110, 230, 255)))
    cd = ImageDraw.Draw(card)
    cd.ellipse([p(46, 28), p(58, 40)], fill=(255, 220, 50, 255))
    hills = Image.new("L", card.size, 0)
    hd = ImageDraw.Draw(hills)
    hd.polygon([p(12, 66), p(12, 52), p(26, 40), p(40, 56), p(48, 50), p(64, 62), p(64, 66)], fill=255)
    hills = Image.composite(hills, Image.new("L", card.size, 0), sky)
    card.alpha_composite(filled(hills, (120, 230, 70, 255), (40, 160, 40, 255)))
    cd = ImageDraw.Draw(card)
    for i in range(5):
        x = 13 + i * 11
        cd.rounded_rectangle([p(x, 15), p(x + 6, 20)], radius=1.2 * k, fill=(60, 64, 84, 255))
    card = card.rotate(8, resample=Image.BICUBIC, center=p(38, 42))
    img.alpha_composite(outlined(card, 3.4 * k))

    blocks = [
        (66, 50, 8, (255, 150, 30, 255), -10),
        (78, 40, 10, (120, 230, 70, 255), 12),
        (74, 62, 11, (0, 190, 255, 255), -6),
        (88, 54, 9, (255, 220, 50, 255), 18),
        (86, 74, 12, (255, 90, 170, 255), -14),
    ]
    for cx, cy, s, color, angle in blocks:
        layer = layer_for(size)
        ld = ImageDraw.Draw(layer)
        ld.rounded_rectangle([p(cx - s / 2, cy - s / 2), p(cx + s / 2, cy + s / 2)], radius=1.6 * k, fill=color)
        light = tuple(min(255, c + 70) for c in color[:3]) + (255,)
        ld.rounded_rectangle([p(cx - s / 2, cy - s / 2), p(cx + s / 2, cy - s / 2 + s * 0.28)],
                             radius=1.2 * k, fill=light)
        layer = layer.rotate(angle, resample=Image.BICUBIC, center=p(cx, cy))
        img.alpha_composite(outlined(layer, 2.8 * k))

    badge = layer_for(size)
    bd = ImageDraw.Draw(badge)
    bd.ellipse([p(4, 64), p(32, 92)], fill=(235, 70, 210, 255))
    bd.ellipse([p(8, 67), p(28, 78)], fill=(255, 150, 240, 255))
    bd.polygon([p(14, 71), p(14, 85), p(26, 78)], fill=WHITE)
    img.alpha_composite(outlined(badge, 3.2 * k))

    save(img, "paim_gif_import.png", size)


if __name__ == "__main__":
    make_glyphs()
    make_steppers()
    make_physics_button()
    make_gif_button()
