"""Generates the dedicated editor toolbar icons:

    resources/paim_editor_reference.png  (Reference Image)
    resources/paim_editor_optimizer.png  (Scene / Structure Optimizer)
    resources/paim_editor_mcp.png        (Editor MCP)
    resources/paim_editor_music.png      (Editor Music)

GD style: thick black outline per part, vertical gradients, glossy highlight
and a soft drop shadow. Requires pycairo only.

Run from the repo root: python3 tools/gen_editor_tool_icons.py
"""
import math
import os

import cairo

SIZE = 200
OL = 7.0  # outline thickness outside each shape
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources")


def hexc(h, a=1.0):
    h = h.lstrip("#")
    return (int(h[0:2], 16) / 255, int(h[2:4], 16) / 255, int(h[4:6], 16) / 255, a)


# --------------------------------------------------------------------------
# path helpers (each returns a callable that appends a path to a context)
# --------------------------------------------------------------------------

def rrect(x, y, w, h, r):
    def fn(c):
        c.new_sub_path()
        c.arc(x + w - r, y + r, r, -math.pi / 2, 0)
        c.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
        c.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
        c.arc(x + r, y + r, r, math.pi, 1.5 * math.pi)
        c.close_path()
    return fn


def poly(pts):
    def fn(c):
        c.move_to(*pts[0])
        for p in pts[1:]:
            c.line_to(*p)
        c.close_path()
    return fn


def circle(cx, cy, r):
    def fn(c):
        c.new_sub_path()
        c.arc(cx, cy, r, 0, math.tau)
        c.close_path()
    return fn


def ellipse(cx, cy, rx, ry, rot=0.0):
    def fn(c):
        c.save()
        c.translate(cx, cy)
        c.rotate(rot)
        c.scale(rx, ry)
        c.new_sub_path()
        c.arc(0, 0, 1, 0, math.tau)
        c.close_path()
        c.restore()
    return fn


def rotated(fn, cx, cy, ang):
    def wrap(c):
        c.save()
        c.translate(cx, cy)
        c.rotate(math.radians(ang))
        c.translate(-cx, -cy)
        fn(c)
        c.restore()
    return wrap


def union(*fns):
    def fn(c):
        for f in fns:
            f(c)
    return fn


def star4(cx, cy, r, inner=0.28):
    pts = []
    for i in range(8):
        a = -math.pi / 2 + i * math.pi / 4
        rr = r if i % 2 == 0 else r * inner
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))

    def fn(c):
        # slightly curved sides give the classic sparkle look
        c.move_to(*pts[0])
        for i in range(0, 8, 2):
            p_in = pts[(i + 1) % 8]
            p_next = pts[(i + 2) % 8]
            c.curve_to(p_in[0], p_in[1], p_in[0], p_in[1], *p_next)
        c.close_path()
    return fn


# --------------------------------------------------------------------------
# rendering
# --------------------------------------------------------------------------

def vgrad(c, top, bottom, y0, y1):
    g = cairo.LinearGradient(0, y0, 0, y1)
    g.add_color_stop_rgba(0, *hexc(top))
    g.add_color_stop_rgba(1, *hexc(bottom))
    return g


def path_extents(c, fn):
    c.new_path()
    fn(c)
    ext = c.fill_extents()
    c.new_path()
    return ext


def outline(c, fn, width=OL):
    c.new_path()
    fn(c)
    c.set_source_rgba(0, 0, 0, 1)
    c.set_line_width(width * 2)
    c.set_line_join(cairo.LINE_JOIN_ROUND)
    c.set_line_cap(cairo.LINE_CAP_ROUND)
    c.stroke_preserve()
    c.fill()


def fill(c, fn, top, bottom=None):
    x0, y0, x1, y1 = path_extents(c, fn)
    c.new_path()
    fn(c)
    if bottom is None:
        c.set_source_rgba(*hexc(top))
    else:
        c.set_source(vgrad(c, top, bottom, y0, y1))
    c.fill()


def gloss(c, fn, alpha=0.45, frac=0.45):
    """White highlight over the upper part of a shape."""
    x0, y0, x1, y1 = path_extents(c, fn)
    c.save()
    c.new_path()
    fn(c)
    c.clip()
    h = (y1 - y0) * frac
    g = cairo.LinearGradient(0, y0, 0, y0 + h)
    g.add_color_stop_rgba(0, 1, 1, 1, alpha)
    g.add_color_stop_rgba(1, 1, 1, 1, 0)
    c.set_source(g)
    c.rectangle(x0, y0, x1 - x0, h)
    c.fill()
    c.restore()


def part(c, fn, top, bottom=None, shine=0.0, width=OL):
    outline(c, fn, width)
    fill(c, fn, top, bottom)
    if shine > 0:
        gloss(c, fn, shine)


def shadow(c, fns, dx=4, dy=6, alpha=0.32):
    c.push_group()
    c.translate(dx, dy)
    for fn in fns:
        outline(c, fn)
    c.translate(-dx, -dy)
    c.pop_group_to_source()
    c.paint_with_alpha(alpha)


def new_canvas():
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, SIZE, SIZE)
    c = cairo.Context(surf)
    c.set_antialias(cairo.ANTIALIAS_BEST)
    return surf, c


def save(surf, name):
    path = os.path.normpath(os.path.join(OUT, name))
    surf.write_to_png(path)
    print("wrote", path)


# --------------------------------------------------------------------------
# icons
# --------------------------------------------------------------------------

def icon_reference():
    """Pinned photo over a transparent (checkerboard) tracing card."""
    surf, c = new_canvas()

    back = rotated(rrect(22, 52, 120, 100, 10), 82, 102, -10)
    front = rotated(rrect(60, 46, 122, 100, 10), 121, 96, 7)
    pin_head = circle(118, 44, 15)
    shadow(c, [back, front, pin_head])

    # back card: checkerboard = reference opacity
    outline(c, back)
    c.save()
    c.new_path()
    back(c)
    c.clip()
    c.set_source_rgba(*hexc("#f2f4f8"))
    c.paint()
    c.translate(82, 102)
    c.rotate(math.radians(-10))
    c.set_source_rgba(*hexc("#b7bfcc"))
    cell = 14
    for iy in range(-6, 7):
        for ix in range(-6, 7):
            if (ix + iy) % 2 == 0:
                c.rectangle(ix * cell, iy * cell, cell, cell)
    c.fill()
    c.restore()
    # dashed tracing guide on the back card
    c.save()
    c.new_path()
    rotated(rrect(32, 62, 100, 80, 6), 82, 102, -10)(c)
    c.set_source_rgba(*hexc("#5a6aa0", 0.85))
    c.set_line_width(3.2)
    c.set_dash([9, 6])
    c.stroke()
    c.restore()

    # front photo
    part(c, front, "#ffffff", "#dfe4ee")
    c.save()
    c.translate(121, 96)
    c.rotate(math.radians(7))
    c.translate(-121, -96)
    inner = rrect(71, 57, 100, 72, 5)
    c.new_path()
    inner(c)
    c.clip()
    c.set_source(vgrad(c, "#36c8ff", "#9be9ff", 57, 129))
    c.paint()
    sun = circle(148, 78, 11)
    hill_back = poly([(100, 132), (140, 88), (182, 132)])
    hill_front = poly([(62, 132), (96, 92), (136, 132)])
    part(c, sun, "#ffe45a", "#ffb21e", width=2.5)
    part(c, hill_back, "#5fd23c", "#2f9a22", width=2.5)
    part(c, hill_front, "#8ef04e", "#44b52a", width=2.5)
    c.restore()
    c.save()
    c.new_path()
    rotated(rrect(71, 57, 100, 72, 5), 121, 96, 7)(c)
    c.set_source_rgba(0, 0, 0, 1)
    c.set_line_width(3.5)
    c.stroke()
    c.restore()

    # push pin
    needle = poly([(115, 52), (121, 52), (119, 70), (117, 70)])
    part(c, needle, "#d9dde6", "#8b93a5", width=3)
    part(c, pin_head, "#ff6b6b", "#c4142b")
    c.new_path()
    ellipse(112, 38, 6, 4, -0.6)(c)
    c.set_source_rgba(1, 1, 1, 0.85)
    c.fill()

    save(surf, "paim_editor_reference.png")


def iso_slab(cx, cy, w, h, t):
    """Isometric slab: returns (silhouette, top, left, right) paths."""
    top = [(cx, cy - h / 2), (cx + w / 2, cy), (cx, cy + h / 2), (cx - w / 2, cy)]
    left = [(cx - w / 2, cy), (cx, cy + h / 2), (cx, cy + h / 2 + t), (cx - w / 2, cy + t)]
    right = [(cx, cy + h / 2), (cx + w / 2, cy), (cx + w / 2, cy + t), (cx, cy + h / 2 + t)]
    sil = [top[0], top[1], right[2], right[3], left[3], top[3]]
    return poly(sil), poly(top), poly(left), poly(right)


def icon_optimizer():
    """Stacked scene layers merged by a lightning bolt."""
    surf, c = new_canvas()

    slabs = [
        (84, 136, ("#4d7cff", "#2f56d6"), "#2442a8", "#3150c4"),
        (84, 104, ("#3ec4ff", "#1f93e6"), "#1a6fb8", "#1f82d0"),
        (84, 72, ("#8af7ff", "#4fd8f2"), "#2ea8c4", "#38bdd8"),
    ]
    W, H, T = 132, 64, 16
    bolt = poly([(150, 34), (112, 108), (138, 108), (122, 172), (178, 86), (150, 86), (170, 34)])
    sils = [iso_slab(x, y, W, H, T)[0] for x, y, *_ in slabs]
    shadow(c, sils + [bolt])

    for x, y, (tt, tb), lc, rc in slabs:
        sil, top, left, right = iso_slab(x, y, W, H, T)
        outline(c, sil)
        fill(c, left, lc)
        fill(c, right, rc)
        fill(c, top, tt, tb)
        gloss(c, top, 0.35, 0.6)
        # small block grid on top face = level objects
        c.save()
        c.new_path()
        top(c)
        c.clip()
        c.set_source_rgba(1, 1, 1, 0.28)
        c.set_line_width(2.2)
        for k in (-1, 0, 1):
            off = k * W / 6
            c.move_to(x - W / 4 + off, y - H / 4 + off * H / W)
            c.line_to(x + W / 4 + off, y + H / 4 + off * H / W)
            c.move_to(x + W / 4 + off, y - H / 4 - off * H / W)
            c.line_to(x - W / 4 + off, y + H / 4 - off * H / W)
        c.stroke()
        c.restore()
        # inner edges between faces
        c.new_path()
        c.move_to(x - W / 2, y)
        c.line_to(x, y + H / 2)
        c.line_to(x + W / 2, y)
        c.move_to(x, y + H / 2)
        c.line_to(x, y + H / 2 + T)
        c.set_source_rgba(0, 0, 0, 1)
        c.set_line_width(3)
        c.set_line_join(cairo.LINE_JOIN_ROUND)
        c.stroke()

    part(c, bolt, "#fff27a", "#ff9d1c", shine=0.5)

    save(surf, "paim_editor_optimizer.png")


def icon_mcp():
    """AI chip: connector pins around a glowing sparkle core."""
    surf, c = new_canvas()

    body = rrect(50, 50, 100, 100, 16)
    pins = []
    for i in range(3):
        p = 72 + i * 28
        pins += [
            rrect(p - 7, 24, 14, 30, 5),
            rrect(p - 7, 146, 14, 30, 5),
            rrect(24, p - 7, 30, 14, 5),
            rrect(146, p - 7, 30, 14, 5),
        ]
    shadow(c, pins + [body])

    for pin in pins:
        part(c, pin, "#ffffff", "#aab3c5", width=5.5)
    part(c, body, "#5b6280", "#2b2f44", shine=0.25)

    core = rrect(66, 66, 68, 68, 12)
    part(c, core, "#c45cff", "#ff4fa0", width=4)
    gloss(c, core, 0.35, 0.5)

    # corner traces on the body
    c.set_source_rgba(1, 1, 1, 0.35)
    for (x, y) in ((58, 58), (142, 58), (58, 142), (142, 142)):
        c.new_path()
        c.arc(x, y, 3.2, 0, math.tau)
        c.fill()

    big = star4(96, 104, 26, 0.3)
    small = star4(119, 80, 12, 0.32)
    part(c, small, "#ffffff", "#ffe6fb", width=3.5)
    part(c, big, "#ffffff", "#ffe6fb", width=4)

    save(surf, "paim_editor_mcp.png")


def icon_music():
    """Vinyl record behind a golden double note."""
    surf, c = new_canvas()

    disc = circle(84, 92, 64)
    head1 = ellipse(112, 154, 20, 15, -0.38)
    head2 = ellipse(166, 140, 20, 15, -0.38)
    stem1 = rrect(122, 66, 12, 88, 4)
    stem2 = rrect(176, 52, 12, 88, 4)
    beam = poly([(122, 64), (188, 46), (188, 74), (122, 92)])
    note = union(head1, head2, stem1, stem2, beam)
    shadow(c, [disc, note])

    # vinyl
    outline(c, disc)
    g = cairo.RadialGradient(70, 74, 8, 84, 92, 64)
    g.add_color_stop_rgba(0, *hexc("#4a4c63"))
    g.add_color_stop_rgba(1, *hexc("#17171f"))
    c.new_path()
    disc(c)
    c.set_source(g)
    c.fill()
    c.set_line_width(1.6)
    for r in (56, 49, 42, 35):
        c.new_path()
        c.arc(84, 92, r, 0, math.tau)
        c.set_source_rgba(1, 1, 1, 0.13)
        c.stroke()
    # light reflection wedge
    c.save()
    c.new_path()
    disc(c)
    c.clip()
    c.move_to(84, 92)
    c.arc(84, 92, 70, math.radians(200), math.radians(240))
    c.close_path()
    c.move_to(84, 92)
    c.arc(84, 92, 70, math.radians(20), math.radians(60))
    c.close_path()
    c.set_source_rgba(1, 1, 1, 0.16)
    c.fill()
    c.restore()
    label = circle(84, 92, 22)
    part(c, label, "#ff7bc4", "#e0307f", width=3)
    c.new_path()
    c.arc(84, 92, 5, 0, math.tau)
    c.set_source_rgba(0, 0, 0, 1)
    c.fill()

    # double note
    outline(c, note)
    fill(c, note, "#fff07a", "#ff9a1c")
    gloss(c, beam, 0.5, 0.7)
    for hx, hy in ((106, 148), (160, 134)):
        c.new_path()
        ellipse(hx, hy, 7, 4, -0.38)(c)
        c.set_source_rgba(1, 1, 1, 0.75)
        c.fill()

    save(surf, "paim_editor_music.png")


if __name__ == "__main__":
    icon_reference()
    icon_optimizer()
    icon_mcp()
    icon_music()
