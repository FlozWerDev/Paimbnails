# Geode 5.11 integration

Paimbnails targets **Geode 5.11.1** and Geometry Dash **2.2081** on Windows and Android.

The latest stable release checked on October 12, 2026 is [5.11.1](https://github.com/geode-sdk/geode/releases/tag/v5.11.1), published on October 10. It keeps the improvements from [5.11.0](https://github.com/geode-sdk/geode/releases/tag/v5.11.0) and reverts that release's process-wide character conversion change. File paths must still use explicit Unicode conversion where required; this integration does not depend on a global UTF-8 locale.

## Changes adopted

- `mod.json` requires 5.11.1. The existing GitHub Actions workflow uses `sdk: given`, so it selects the SDK and loader binaries from this version in the manifest.
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
geode sdk update 5.11.1
geode sdk install-binaries --platform win --version 5.11.1
```

Android builds need the matching `android32` or `android64` binaries instead of `win`; GitHub Actions installs those automatically.

The compatibility audit uses SDK headers and existing generated game bindings without configuring or compiling the mod:

```sh
python3 tests/geode_sdk_compat_regression.py --sdk "$GEODE_SDK"
python3 tests/pause_layer_bindings_regression.py
python3 tests/live_slot_shader_regression.py --static-only
```

These checks do not replace compilation or testing in Geometry Dash. No build or game execution was performed for this integration.
