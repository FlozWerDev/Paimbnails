# Editor tool art sources

`make_assets.py` draws the art for the Physics Simulator and the GIF to Objects
popups under `resources/`:

- `paim_physics.png` and `paim_gif_import.png`, the colour editor toolbar buttons.
- `paim_ui_*`: shared glyphs (zoom, fit, play, trash, undo, clock, folder,
  import, hammer, badge) and the red/green `stepMinus` / `stepPlus` buttons.
- `paim_phys_*`: world options, capture, dynamic/static bodies, hitboxes.
- `paim_gif_*`: import options, toggles and one glyph per import mode.

Regenerate them from the repository root:

```bash
python3 resources/source/editor-tools/make_assets.py
```

Glyphs are white over a black rim, so `setColor()` tints the body and keeps the
outline. The toolbar buttons and the stepper buttons are already coloured.

The source directory is not listed in `mod.json`, so only the rendered PNGs are
packaged.
