# Geode 5.11 integration

> Historical snapshot for GD 2.2081. The current tree was migrated to the public
> GD 2.2091 nightly on 2026-10-31. See the [current SDK review](REVISION_SDK_NIGHTLY_2026-10-31.md).

Paimbnails targets **Geode 5.11.2** and Geometry Dash **2.2081** on Windows and Android.

The stable release checked on October 16, 2026 is [5.11.2](https://github.com/geode-sdk/geode/releases/tag/v5.11.2), published on October 15 at 22:38 UTC. It fixes loader crashes and text input in forward compatibility mode on GD 2.209. The release is still primarily for 2.2081 and adds no public SDK APIs compared with 5.11.1.

The [5.11.1](https://github.com/geode-sdk/geode/releases/tag/v5.11.1) locale rollback remains in place: filesystem paths require explicit Unicode conversion. This integration does not depend on a global UTF-8 locale.

## Changes adopted

- `mod.json` and CMake require 5.11.2. The existing GitHub Actions workflow uses `sdk: given`, so it selects the SDK and loader binaries from this version in the manifest. The local SDK and Windows link libraries were updated together.
- Clean builds pin [bindings revision `1012fcb37243286184d319404b15fd929098fd47`](https://github.com/geode-sdk/bindings/tree/1012fcb37243286184d319404b15fd929098fd47), rather than a moving `main` branch. Explicit `GEODE_BINDINGS_REPO_PATH` overrides remain available. The active local checkout was advanced to this same revision and bindings were regenerated from `bindings/2.2081/Entry.bro`.
- The reviewed `.bro` changes include Windows bindings for `CCGrabber` and other Cocos classes, plus the `m_pHandlers` dispatcher member rename. The accompanying inline implementations correct the editor start-position object ID (`31`) and a force-block object ID (`3645`). Paimbnails does not access the deprecated dispatcher member names.
- `GEODE_PACKAGE_ZSTD` defaults to `ON`, adopting the packaging option introduced in [Geode 5.10](https://github.com/geode-sdk/geode/releases/tag/v5.10.0). CMake checks that the installed CLI supports `--zstd` (validated with CLI 3.9.0); `-DGEODE_PACKAGE_ZSTD=OFF` retains Deflate packaging. GitHub Actions also passes `zstd: true` when combining platform packages. Automatic installation after a local build stays enabled.
- WAV writing, cache lookup and cleanup explicitly interpret UTF-8 paths. Writing now checks stream closure, PCM frame alignment and WAV size/rate limits before accepting output. A regression reproduces the old Unicode-path failure and checks the production implementation with an accented/CJK/emoji temporary directory.
- CMake rejects older SDKs and aligns arc 1.5.11 and asp revision `ecea33b` with the upstream SDK. These pins also replace dependency versions left in an existing CMake cache.
- Hook priority helpers check failures and use `Modify::getAllHooks()` to report the hooks that actually exist. Debug Logs lists registered hooks with their modify class name, priority and enabled state using `Hook::getModifyClassName()`. The list is printed at startup when debug logging is enabled, or when the setting is enabled during a session.
- Dynamic popup removal hooks the actual `CCNode` implementation instead of an unbound inherited method on `FLAlertLayer`. The deferred close retains the popup before releasing its animation guard.
- Texture Studio checks `CCFileUtils::getTexturePackCount()`, ordered search paths and graphics content scale before reusing live color masks. Changing packs, replacing a pack with another at the same count, or changing graphics scale discards stale sources and invalidates pending mask results. Texture reload also clears the saved source snapshot.

## Improvements provided by the loader

Upgrading Geode supplies fixes for disabled or invisible `Button` and `SliderNode` controls intercepting touches, native wrapper races across threads, touch dispatcher destruction on exit, Wine crashes and Android resource download errors. Paimbnails keeps its own lifecycle and main-thread guards.

The updated SDK also improves Geode `Label` wrapping and texture reload behavior, adds JSON and color formatters, and updates the network and async dependencies. The mod currently uses its existing bitmap text and menu controls, so this integration does not replace those UI components.

## SDK setup and validation

Update the SDK source and install its prebuilt link libraries with the Geode CLI:

```sh
geode sdk update 5.11.2
geode sdk install-binaries --platform win --version 5.11.2
```

Android builds need the matching `android32` or `android64` binaries instead of `win`; GitHub Actions installs those automatically.

The compatibility audit uses SDK headers and existing generated game bindings without configuring or compiling the mod:

```sh
python3 tests/geode_sdk_compat_regression.py --sdk "$GEODE_SDK"
python3 tests/pause_layer_bindings_regression.py
python3 tests/live_slot_shader_regression.py --static-only
```

The Windows build and automatic mod installation passed. A separate clean CMake configure verified the pinned bindings download. Validation also passed 28 portable checks (including ten C++ regressions with ASan/UBSan), three binding audits and Clang static analysis of WAV handling. The pause/editor audit checked 57 native hooks; the SDK audit checked 261 headers, 93 modify bases and 145 priority targets.

The Windows package contains 636 entries. All entries were decompressed and CRC-checked; their contents match a Deflate package built from the same DLL. The packaged DLL matches the build output, and the installed package hash matches the generated file. Zstd reduced the package from 16,854,799 to 16,010,748 bytes (844,051 bytes, 5.01%). Logs and package verification are under `build-clang/geode-5.11.2-validation/`.

Android compilation and in-game visual interaction were not run locally.

## GD 2.209 / 2.2091 lives in a separate project

The native Windows 2.2091 port is no longer part of this tree. It was moved to
`Paimbnails-2.2091`, together with its private Geode SDK, patched bindings,
native layout checks, migration scripts, standalone pilot and 2.2091 build
artifacts. See `docs/SEPARACION_2_2091.md`.

This tree stays on the 2.2081 ABI. A mod compiled for 2.2081 does not become a
supported 2.209 mod by updating the loader: native addresses, signatures and
layouts differ, and the 2.2091 bindings require their own PAD entries and
resolved addresses. The manifest therefore keeps its accurate 2.2081 target; it
does not bypass the version check with `"*"` or reuse 2.2091 layouts against
2.2081 libraries.
