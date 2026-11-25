# Progression art sources

`gen_gd_assets.cpp` renders every `paim_prog*.png` under `resources/` using 4× supersampled Signed Distance Fields (SDF) in native Geometry Dash style: the five tier medals, the radial groove ring, the six rarity badge frames, the recessed icon well, and the glow/spark overlays.

Regenerate them from the repository root:

```bash
g++ -O3 -std=c++20 resources/source/progression/gen_gd_assets.cpp -o /tmp/gen_gd_assets
/tmp/gen_gd_assets /tmp/paim_prog_raw
python3 -c 'import glob, os, struct, zlib ...' # pack RGBA -> PNG
```

The art uses grayscale bodies with uniform black outlines and top-left GD bevel gloss. `setColor()` is a multiply, so outlines stay black while bodies and highlights pick up the tier or rarity colour cleanly.

The source directory is not listed in `mod.json`, so only the rendered PNGs are packaged.
