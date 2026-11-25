# Geode 6 integration

Paimbnails targets **Geode 6.0.0** and Geometry Dash **2.2091**.

The current target is
[v6.0.0](https://github.com/geode-sdk/geode/releases/tag/v6.0.0)
(2026-11-24), and it requires Geode CLI 4.0.1 or newer.

## Setup

```sh
winget upgrade --id GeodeSDK.GeodeCLI      # CLI 4.0.1+
geode sdk update v6.0.0
geode sdk install-binaries                 # installs bin/6.0.0
```

## Changes in the project

- `mod.json` requires `6.0.0`; CMake rejects SDKs older than 6.0.0.
- `GEODE_LINK_NIGHTLY` now defaults to `OFF`, so the mod links the release
  import library in `bin/6.0.0` instead of `bin/nightly`. Reconfigure an old
  build directory with `-DGEODE_LINK_NIGHTLY=OFF`.
- CI uses `sdk: given`, which installs the SDK and binaries named by `mod.json`.
- `tests/geode_sdk_compat_regression.py` accepts prerelease versions.
- Clean builds keep the pinned bindings revision; arc and asp match the SDK.
- Geode 6 empties `OverlayManager` when textures unload. The custom cursor now
  re-attaches on `TexturesLoaded`; the volume overlay already re-attaches on
  scene changes.
- `chatFont.fnt` text inputs use Geode 6's `CommonFilter::Text` /
  `CommonFilter::TextEmojis` instead of legacy `CommonFilter::Any` so accented
  Latin, Cyrillic, Greek, and vanilla emojis work across the mod UI.
- `v6.0.0-beta.3` fixes an `Event` port use-after-free, Android C++ demangling
  in `getObjectName`, and the Windows 2.2091 `MessageBoxFix` offsets.

## Loader compatibility

A Geode 5.x loader will not load this mod, and a Geode 6 loader will not load
mods built for Geode 5.x. Update the loader in the game only after the other
installed mods have Geode 6 builds.

## Not adopted yet

The mod still uses `CCLabelBMFont` and `std::filesystem` in many places. Both
keep working, but `geode::Label` (Unicode and emoji glyphs), `asp::fs`,
`geode::Toggler`, `geode::BoxedLabel`, `geode::ContextMenu`, multiline
`geode::TextInput`, and `geode::ProcessPopup` can be adopted gradually.
