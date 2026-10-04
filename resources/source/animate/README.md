# Paimon Animate art sources

`make_assets.py` draws every `paim_anim_*.png` under `resources/` plus the
`paim_animate.png` editor toolbar button: the timeline glyphs (frame tools,
onion skin, ghosts, playback modes, lock/eye, tag, gear, bake), the film cell used by the
frame strip and its selection glow, and the playhead marker.

Regenerate them from the repository root:

```bash
python3 resources/source/animate/make_assets.py
```

Glyphs are white over a black rim, so `setColor()` tints the body (clip colour,
active state) and keeps the outline. The film cell keeps a grey window in the
middle so the tint reads darker inside the frame than on the sprocket border.

The source directory is not listed in `mod.json`, so only the rendered PNGs are
packaged.
