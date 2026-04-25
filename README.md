# Paimbnails

Paimbnails is a large Geode mod for Geometry Dash focused on thumbnails, capture tools, visual customization, audio features, community systems, profile enhancements, emotes, pets, and more.

## Features

- Level thumbnails across multiple GD screens
- In-game thumbnail capture and preview workflows
- Animated and video thumbnail support
- Custom backgrounds and visual effects
- Dynamic songs and profile music
- Emote picker and inline emote rendering
- Community / leaderboard features
- Profile media and moderation tools
- Cross-platform Geode build targets

## Requirements

- Geometry Dash 2.2081
- Geode SDK
- CMake 3.21+
- A C++23-capable compiler
- `GEODE_SDK` environment variable pointing to your Geode SDK folder

## Dependencies

Declared in `mod.json`:

- `geode.node-ids`
- `prevter.imageplus`
- `eclipse.ffmpeg-api`

Make sure your Geode environment installs and resolves these dependencies correctly.

## Building

### 1. Set the Geode SDK path

On Windows:

```bat
set GEODE_SDK=C:\path\to\geode-sdk
```

On macOS / Linux:

```sh
export GEODE_SDK=/path/to/geode-sdk
```

### 2. Configure and build

Example CMake flow:

```sh
cmake -B build
cmake --build build --config Release
```

The project uses C++23 and expects Geode to be available through the `GEODE_SDK` environment variable.

## CI

GitHub Actions builds the mod for:

- Windows
- macOS
- iOS
- Android32
- Android64

## Project Structure

- `src/` — mod source code
- `resources/` — packaged sprites/resources
- `docs/` — internal documentation and audit notes
- `servers/` — related backend / service code
- `mod.json` — Geode metadata, settings, and dependencies
- `CMakeLists.txt` — build configuration

## Public Metadata

- Mod ID: `flozwer.paimbnails2`
- Name: `Paimbnails`
- Framework: Geode
- Target GD version: `2.2081`

## Support / Links

- Source: https://github.com/Fl0zWer/Paimbnails
- Community: https://discord.gg/5N5vpSfZwY

For user-facing information inside Geode, see:

- `about.md`
- `support.md`
- `changelog.md`