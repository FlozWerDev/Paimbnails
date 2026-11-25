#!/usr/bin/env python3
"""Render a Geode changelog.md (with frame: sprites and <cX> color tags) to standalone HTML."""

from __future__ import annotations

import argparse
import base64
import glob
import html
import os
import plistlib
import re
import struct
import zlib
from pathlib import Path

COLOR_MAP = {
    "a": "#9632ff",
    "b": "#4a52e1",
    "c": "#ffff96",
    "d": "#ff96ff",
    "f": "#96ffff",
    "g": "#40e348",
    "j": "#32c8ff",
    "l": "#60abef",
    "o": "#ffa54b",
    "p": "#ff00ff",
    "r": "#ff5a5a",
    "s": "#ffdc41",
    "y": "#ffff00",
}

DEFAULT_GD_RES = [
    "/home/wumpus/.local/share/Steam/steamapps/common/Geometry Dash/Resources",
    os.path.expanduser("~/.local/share/Steam/steamapps/common/Geometry Dash/Resources"),
    "C:/Program Files (x86)/Steam/steamapps/common/Geometry Dash/Resources",
]


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode_png_rgba(path: str) -> tuple[int, int, bytearray]:
    """Decode an 8-bit PNG (RGB/RGBA/GA/G) into raw RGBA bytes using stdlib only."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Not a PNG: {path}")

    pos = 8
    width = height = bit_depth = color_type = 0
    idat = bytearray()
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        ctype = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if bit_depth != 8 or interlace != 0:
                raise ValueError(f"Unsupported PNG format in {path}: depth={bit_depth}, interlace={interlace}")
        elif ctype == b"IDAT":
            idat.extend(chunk)
        elif ctype == b"IEND":
            break

    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color_type)
    if not channels:
        raise ValueError(f"Unsupported color_type={color_type} in {path}")

    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    recon = bytearray(height * stride)
    prev = bytearray(stride)

    idx = 0
    for y in range(height):
        ftype = raw[idx]
        idx += 1
        row = raw[idx : idx + stride]
        idx += stride
        out = bytearray(stride)
        if ftype == 0:
            out[:] = row
        elif ftype == 1:
            for i in range(stride):
                left = out[i - channels] if i >= channels else 0
                out[i] = (row[i] + left) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                out[i] = (row[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = out[i - channels] if i >= channels else 0
                out[i] = (row[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                left = out[i - channels] if i >= channels else 0
                up = prev[i]
                ul = prev[i - channels] if i >= channels else 0
                out[i] = (row[i] + _paeth(left, up, ul)) & 0xFF
        else:
            raise ValueError(f"Unknown PNG filter {ftype} in {path}")
        recon[y * stride : (y + 1) * stride] = out
        prev = out

    if channels == 4:
        return width, height, recon

    rgba = bytearray(width * height * 4)
    for i in range(width * height):
        if channels == 3:
            rgba[i * 4 : i * 4 + 3] = recon[i * 3 : i * 3 + 3]
            rgba[i * 4 + 3] = 255
        elif channels == 2:
            g, a = recon[i * 2], recon[i * 2 + 1]
            rgba[i * 4 : i * 4 + 4] = bytes((g, g, g, a))
        else:
            g = recon[i]
            rgba[i * 4 : i * 4 + 4] = bytes((g, g, g, 255))
    return width, height, rgba


def encode_png_rgba(width: int, height: int, rgba: bytes) -> bytes:
    """Encode raw RGBA bytes as a valid PNG."""
    def chunk(ctype: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(ctype + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + ctype + payload + struct.pack(">I", crc)

    raw_rows = bytearray()
    stride = width * 4
    for y in range(height):
        raw_rows.append(0)
        raw_rows.extend(rgba[y * stride : (y + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw_rows), level=6))
        + chunk(b"IEND", b"")
    )


def parse_rect(rect_str: str) -> tuple[int, int, int, int]:
    nums = [int(x) for x in re.findall(r"-?\d+", rect_str)]
    return nums[0], nums[1], nums[2], nums[3]


class SpriteResolver:
    def __init__(self, gd_res: str | None, mod_res_dir: Path) -> None:
        self.gd_res = gd_res
        self.mod_res_dir = mod_res_dir
        self.frames: dict[str, tuple[str, dict]] = {}
        self.sheets: dict[str, tuple[int, int, bytearray]] = {}
        if gd_res and os.path.isdir(gd_res):
            plists = sorted(
                glob.glob(os.path.join(gd_res, "*.plist")),
                key=lambda p: (0 if "-hd." in p else (1 if "-uhd." in p else 2), p),
            )
            for ppath in plists:
                try:
                    with open(ppath, "rb") as f:
                        pl = plistlib.load(f)
                except Exception:
                    continue
                frames = pl.get("frames")
                meta = pl.get("metadata", {})
                tex_name = meta.get("realTextureFileName") or meta.get("textureFileName")
                if not isinstance(frames, dict) or not tex_name:
                    continue
                tex_path = os.path.join(gd_res, tex_name)
                if not os.path.exists(tex_path):
                    continue
                for name, info in frames.items():
                    if name not in self.frames and isinstance(info, dict):
                        self.frames[name] = (tex_path, info)

    def resolve_frame_data_uri(self, name: str) -> str | None:
        entry = self.frames.get(name)
        if not entry:
            return None
        tex_path, info = entry
        if tex_path not in self.sheets:
            self.sheets[tex_path] = decode_png_rgba(tex_path)
        sw, sh, srgba = self.sheets[tex_path]

        rect_key = "textureRect" if "textureRect" in info else "frame"
        if rect_key not in info:
            return None
        x, y, w, h = parse_rect(info[rect_key])
        rotated = bool(info.get("textureRotated") or info.get("rotated"))

        src_w, src_h = (h, w) if rotated else (w, h)
        out = bytearray(w * h * 4)
        for dy in range(h):
            for dx in range(w):
                if rotated:
                    sx = x + dy
                    sy = y + (src_h - 1 - dx)
                else:
                    sx = x + dx
                    sy = y + dy
                if 0 <= sx < sw and 0 <= sy < sh:
                    si = (sy * sw + sx) * 4
                    di = (dy * w + dx) * 4
                    out[di : di + 4] = srgba[si : si + 4]

        png_bytes = encode_png_rgba(w, h, bytes(out))
        return "data:image/png;base64," + base64.b64encode(png_bytes).decode("ascii")

    def resolve_mod_file_uri(self, src: str) -> str | None:
        rel = src.split("/", 1)[1] if "/" in src else src
        for candidate in (self.mod_res_dir / rel, self.mod_res_dir.parent / rel):
            if candidate.is_file():
                b64 = base64.b64encode(candidate.read_bytes()).decode("ascii")
                return "data:image/png;base64," + b64
        return None


def render_inline(text: str, resolver: SpriteResolver, missing: list[str]) -> str:
    tokens: list[str] = []
    pos = 0
    pattern = re.compile(r"!\[\]\(([^)]+)\)|<c([a-z])>|</c>|`([^`]+)`|\*\*([^*]+)\*\*|\*([^*]+)\*")
    for m in pattern.finditer(text):
        if m.start() > pos:
            tokens.append(html.escape(text[pos : m.start()]))
        if m.group(1) is not None:
            raw_src = m.group(1)
            src, _, query = raw_src.partition("?")
            height = 18
            hm = re.search(r"height=(\d+(?:\.\d+)?)", query)
            if hm:
                height = float(hm.group(1))
            if src.startswith("frame:"):
                fname = src[6:]
                uri = resolver.resolve_frame_data_uri(fname)
                if uri:
                    tokens.append(
                        f'<img src="{uri}" alt="{html.escape(fname)}" title="{html.escape(raw_src)}" '
                        f'style="height:{height}px;vertical-align:middle;margin:0 4px 2px 0;">'
                    )
                else:
                    missing.append(raw_src)
                    tokens.append(f'<span class="missing">[{html.escape(raw_src)}]</span>')
            else:
                uri = resolver.resolve_mod_file_uri(src)
                if uri:
                    tokens.append(
                        f'<img src="{uri}" alt="{html.escape(src)}" title="{html.escape(raw_src)}" '
                        f'style="height:{height}px;vertical-align:middle;margin:0 4px 2px 0;">'
                    )
                else:
                    missing.append(raw_src)
                    tokens.append(f'<span class="missing">[{html.escape(raw_src)}]</span>')
        elif m.group(2) is not None:
            color = COLOR_MAP.get(m.group(2), "#ffffff")
            tokens.append(f'<span style="color:{color};font-weight:600;">')
        elif m.group(0) == "</c>":
            tokens.append("</span>")
        elif m.group(3) is not None:
            tokens.append(f"<code>{html.escape(m.group(3))}</code>")
        elif m.group(4) is not None:
            tokens.append(f"<strong>{html.escape(m.group(4))}</strong>")
        elif m.group(5) is not None:
            tokens.append(f"<em>{html.escape(m.group(5))}</em>")
        pos = m.end()
    if pos < len(text):
        tokens.append(html.escape(text[pos:]))
    return "".join(tokens)


def render_markdown(md_text: str, resolver: SpriteResolver) -> tuple[str, list[str]]:
    lines = md_text.splitlines()
    out: list[str] = []
    missing: list[str] = []
    in_list = False

    for line in lines:
        s = line.strip()
        if not s:
            if in_list:
                out.append("</ul>")
                in_list = False
            continue
        if s == "---":
            if in_list:
                out.append("</ul>")
                in_list = False
            out.append("<hr>")
            continue
        if s.startswith("# "):
            if in_list:
                out.append("</ul>")
                in_list = False
            out.append(f"<h1>{render_inline(s[2:], resolver, missing)}</h1>")
        elif s.startswith("## "):
            if in_list:
                out.append("</ul>")
                in_list = False
            out.append(f"<h2>{render_inline(s[3:], resolver, missing)}</h2>")
        elif s.startswith("- "):
            if not in_list:
                out.append("<ul>")
                in_list = True
            out.append(f"<li>{render_inline(s[2:], resolver, missing)}</li>")
        else:
            if in_list:
                out.append("</ul>")
                in_list = False
            out.append(f"<p>{render_inline(s, resolver, missing)}</p>")

    if in_list:
        out.append("</ul>")
    return "\n".join(out), missing


def find_gd_resources(explicit: str | None) -> str | None:
    if explicit and os.path.isdir(explicit):
        return explicit
    for p in DEFAULT_GD_RES:
        if os.path.isdir(p):
            return p
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Preview Geode changelog.md with frame: sprites.")
    parser.add_argument("input", nargs="?", default="changelog.md", help="Markdown file to render")
    parser.add_argument("-o", "--out", default="/tmp/paimbnails_changelog_preview.html", help="Output HTML file")
    parser.add_argument("--gd-res", default=None, help="Path to Geometry Dash Resources directory")
    args = parser.parse_args()

    in_path = Path(args.input).resolve()
    mod_res_dir = in_path.parent / "resources"
    gd_res = find_gd_resources(args.gd_res)
    resolver = SpriteResolver(gd_res, mod_res_dir)
    body, missing = render_markdown(in_path.read_text(encoding="utf-8"), resolver)

    doc = f"""<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<title>{html.escape(in_path.name)} — Geode Preview</title>
<style>
  body {{ background:#161923; color:#eef2fb; font-family:system-ui,-apple-system,sans-serif; max-width:900px; margin:28px auto; padding:0 20px; line-height:1.5; }}
  h1 {{ font-size:1.7rem; margin:1.2rem 0 .6rem; }}
  h2 {{ font-size:1.25rem; margin:1rem 0 .45rem; }}
  hr {{ border:none; border-top:1px solid #2e3448; margin:1rem 0; }}
  ul {{ padding-left:1.3rem; margin:.35rem 0 .8rem; }}
  li {{ margin:.3rem 0; }}
  code {{ background:#0d1018; padding:1px 5px; border-radius:4px; font-size:.9em; color:#bfe3ff; }}
  .missing {{ color:#ff6b6b; font-size:.85em; font-family:monospace; }}
  .banner {{ font-size:.82rem; color:#9aa4bd; margin-bottom:1rem; padding:8px 12px; background:#1e2331; border-radius:8px; }}
</style>
</head>
<body>
<div class="banner">Source: <code>{html.escape(str(in_path))}</code> | GD Resources: <code>{html.escape(gd_res or "not found")}</code> | Missing images: {len(missing)}</div>
{body}
</body>
</html>
"""
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(doc, encoding="utf-8")
    print(f"Wrote {out_path} (missing={len(missing)})")
    if missing:
        print("Missing:", sorted(set(missing)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
