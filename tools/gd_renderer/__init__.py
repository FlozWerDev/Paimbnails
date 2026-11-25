"""
Geometry Dash Level Renderer & Interactive Editor Viewer for Python.
"""

from .asset_manager import AssetManager
from .color_engine import ColorEngine, ChannelColor, HSVShift
from .level_parser import GDLevel, LevelObject, LevelParser
from .renderer import GDRenderer


def __getattr__(name):
    # The headless renderer must not require pygame or tkinter.
    if name == "GDViewer":
        from .viewer import GDViewer
        return GDViewer
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")

__all__ = [
    "AssetManager",
    "ColorEngine",
    "ChannelColor",
    "HSVShift",
    "GDLevel",
    "LevelObject",
    "LevelParser",
    "GDRenderer",
    "GDViewer"
]
