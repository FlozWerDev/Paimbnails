# Pack Gen: live slots

The manager now opens `LiveSlotEditor`. A slot stores its name, four colors,
intensity, brightness, saturation, contrast, glow strength/blend mode, outline
protection, region analysis parameters, and UI scope. Save & Apply persists
JSON and selects the slot. It does not export PNGs, ZIPs, or install a Texture
Loader pack. Import/export shares this same configuration.

The editor previews unsaved changes globally. Back restores the saved active
slot. Compare original temporarily bypasses the effect without changing the
saved selection. Disable clears the active selection. Saving an old project
through the new editor converts it to palette settings; custom images and
fusions are not part of the live slot format.

`LiveSlotRuntime` resolves the current local UI sheets and standalone controls
using cocos resource lookup. It analyzes only selected UI frames on a tracked
worker, repacks their four role weights into in-memory mask textures, and
cancels stale work using a generation counter. The existing clustering,
classification, edge cleanup, and luminance tint rules remain in use.

Color and grade changes update shader uniforms. Only region parameters and
scope trigger another CPU analysis. The masks are reused when switching
between slots with identical analysis parameters. The shader runs at sprite
and sprite-batch draw time, preserves source alpha, accounts for premultiplied
textures and padding, and leaves nodes with custom shaders alone. GL reload
invalidates pending uploads, textures, and the program; the saved slot is
restored after the menu loads again.

Supported targets are the catalogued vanilla UI frames and standalone button,
slider, and loading controls. Gameplay art is excluded by the existing catalog.
Arbitrary mod atlases, image replacement, baked backgrounds, fonts, fusion and
export options are not exposed by this editor. Atlas masks larger than 4096
pixels per side are skipped and reported as unavailable.

## Validation

Without compilation:

```sh
python3 tests/live_slot_shader_regression.py --static-only
```

With explicit authorization to compile shaders, the headless regression uses
Mesa EGL/GLES2 and checks the real shader against reference pixels:

```sh
python3 tests/live_slot_shader_regression.py
python3 tests/tint_preview_shader_regression.py
```

The C++ integration still requires a build and in-game validation: create two
slots, drag the color picker and each slider, compare, cancel, save, switch,
disable, delete the active slot, restart, and change texture quality. Check
batched panels, translucent edges, and coexistence with custom shaders.
