"""Cocos2d affine geometry shared by drawing, bounds and viewport culling."""

import math


def object_matrix(obj):
    """Return (a, b, c, d) for a centered object in world coordinates.

    Cocos rotates its two axes independently: X rotation controls the Y
    column, Y rotation the X column. Equal angles give a clockwise rotation.
    """
    rotation_x = obj.rotation if obj.rotation_x is None else obj.rotation_x
    rotation_y = obj.rotation if obj.rotation_y is None else obj.rotation_y
    rx, ry = math.radians(rotation_x), math.radians(rotation_y)
    sx = obj.scale * obj.scale_x * (-1 if obj.flip_x else 1)
    sy = obj.scale * obj.scale_y * (-1 if obj.flip_y else 1)
    return math.cos(ry) * sx, -math.sin(ry) * sx, math.sin(rx) * sy, math.cos(rx) * sy


def sprite_corners(obj, sprite):
    """World corners in image order: top left, top right, bottom right, bottom left.

    The database stores the transformed bottom-left position of each sprite.
    Apply its local rotation/scale before the object's parent transform.
    Sprite flips change texture coordinates without moving its center.
    """
    width, height = sprite.get("contentSize", [30, 30])
    sx, sy = sprite.get("scale", [1, 1])
    angle = math.radians(sprite.get("rotation", 0))
    cos_a, sin_a = math.cos(angle), math.sin(angle)
    px, py = sprite.get("position", [-width / 2, -height / 2])
    a, b, c, d = object_matrix(obj)
    corners = []
    for u, v in ((0, 1), (1, 1), (1, 0), (0, 0)):
        if sprite.get("flipX", False):
            u = 1 - u
        if sprite.get("flipY", False):
            v = 1 - v
        lx = px + u * width * sx * cos_a + v * height * sy * sin_a
        ly = py - u * width * sx * sin_a + v * height * sy * cos_a
        corners.append((obj.x + a * lx + c * ly, obj.y + b * lx + d * ly))
    return corners


def object_bounds(obj, definition):
    corners = [point for sprite in definition.get("sprites", [])
               for point in sprite_corners(obj, sprite)]
    if not corners:
        return obj.x, obj.y, obj.x, obj.y
    xs, ys = zip(*corners)
    return min(xs), min(ys), max(xs), max(ys)
