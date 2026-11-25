# Paimbnails

A Geode mod for Geometry Dash with thumbnails, visual effects, and a Paimon companion.

This tree is the **Geometry Dash 2.2091** build (Windows and Android). The native
Windows **2.209 / 2.2091** port, including its private Geode SDK, patched
bindings, layouts and standalone pilot, is maintained in the separate project
`Paimbnails-2.2091`.

## Features

- Level thumbnails in the browser, level info, leaderboards, and more
- In-game thumbnail capture and upload
- Animated and video thumbnails
- Custom backgrounds with shaders and effects
- Beat-synced visual effects
- Dynamic songs and profile music
- Emote picker in comments
- Profile pictures, reviews, and badges
- Custom progress bar
- Custom cursor
- [Paimon Shop](docs/PAIMON_SHOP.md): local layout, RTX and shader presets with PNG previews and JSON sharing
- Community leaderboard
- [Physics Lab](docs/EDITOR_PHYSICS_NATIVE.md): compile fixed/dynamic editor bodies to native GD trigger graphs or keyframes
- Pet companion
- Discord Rich Presence
- And much more

## Install

Download from the [Geode mod index](https://geode-sdk.org) — search for **Paimbnails**.

Requires [Geode 6.0.0 or newer](https://github.com/geode-sdk/geode/releases/tag/v6.0.0) (Geode CLI 4.0.1 or newer) and Geometry Dash 2.2091. Geode 6 mods cannot be loaded by a Geode 5.x loader.

See [Geode 6 integration](docs/GEODE_6_0.md) for SDK requirements and the changes used by the mod.
The previous [Geode 5.11 integration](docs/GEODE_5_11.md) is kept as a historical snapshot.
See [Separación de la variante 2.2091](docs/SEPARACION_2_2091.md) for what lives
in the other project and why it is not part of this build.

## Local builds

On this Windows installation, completed `.geode` packages are copied to
`C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\geode\mods` after packaging.
This applies to the default CMake build, `Paimbnails_PACKAGE`, building the
`Paimbnails` library target directly (including `build-clang`), and
`compilar-desde-cero.cmd`. The copy is enabled by default on Windows and creates
the destination directory if needed; the clean-build script always enables it.
Override the destination with
`-DPAIMBNAILS_MODS_DIRECTORY=<path>` or disable the copy with
`-DPAIMBNAILS_DEPLOY_LOCAL_MOD=OFF`.
Existing build directories pointing to the old `F:\SteamLibrary` installation
are migrated to this destination when CMake reconfigures; other custom paths
are preserved.

For the configured Clang build, run `cmake --build build-clang --parallel 8`.
Using `--target Paimbnails` also packages and installs the freshly linked mod.

Quick Hub's configuration offers a keyboard or mouse hold button and a duration
from 0.3 to 5 seconds (default: 1.5 seconds). Open it from the hub or the gear
beside **Hold** in the capture menu. Releasing early cancels the pending hold.

## License

Paimbnails is licensed under the [MIT License](LICENSE). See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for third-party licenses and credits.

## Links

- Discord: https://discord.gg/5N5vpSfZwY
- Source: https://github.com/FlozWerDev/Paimbnails
