"""Locate the configured build and the bindings it actually uses."""

import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def build_directory(explicit=None):
    if explicit:
        candidate = Path(explicit)
        return candidate if candidate.is_absolute() else ROOT / candidate
    configured = os.environ.get("PAIMBNAILS_BUILD_DIR")
    if configured:
        return build_directory(configured)
    for name in ("build-clang", "build-win", "build"):
        candidate = ROOT / name
        if (candidate / "CMakeCache.txt").is_file():
            return candidate
    raise FileNotFoundError("Configure a build or pass --build-dir before auditing generated bindings")


def cache_values(directory=None):
    directory = build_directory(directory)
    text = (directory / "CMakeCache.txt").read_text(encoding="utf-8-sig")
    return dict(re.findall(r"^([^#/:][^:=]*):[^=]*=(.*)$", text, re.MULTILINE))


def bindings_directory(directory=None):
    directory = build_directory(directory)
    values = cache_values(directory)
    version = values.get("GEODE_GD_VERSION", "").strip()
    roots = [values.get("GEODE_BINDINGS_REPO_PATH"),
             directory / "_deps/bindings-src", ROOT / "build/_deps/bindings-src"]
    for root in roots:
        if not root:
            continue
        path = Path(root) / "bindings" / version
        if (path / "Entry.bro").is_file():
            return path
    raise FileNotFoundError(f"Missing bindings for GD {version} in build {directory}")
