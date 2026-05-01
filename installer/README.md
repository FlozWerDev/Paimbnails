# Paimbnails Installers

## Platforms

### Windows
- **Installer**: `Paimbnails-Setup.exe` (NSIS)
- **Portable**: `flozwer.paimbnails2.geode`
- Auto-detects Geometry Dash installation

### macOS
- **DMG**: `Paimbnails-macOS.dmg`
- Contains `.geode` file and installation instructions

### Android
- **APK**: Get from Discord (requires patched game)
- Contact: [Discord](https://discord.gg/5N5vpSfZwY)

### iOS
- **IPA**: Get from Discord (requires AltStore)
- Contact: [Discord](https://discord.gg/5N5vpSfZwY)

## Building the Windows Installer

### Prerequisites
- [NSIS](https://nsis.sourceforge.io/Download) installed on Windows
- The mod `.geode` file built at `..\build\flozwer.paimbnails2.geode`

### Build Steps

1. Build the mod first:
   ```
   cd ..\build
   cmake ..
   cmake --build . --config Release
   ```

2. Run the build script:
   ```
   build_installer.bat
   ```

   Or manually with NSIS:
   ```
   "C:\Program Files (x86)\NSIS\makensis.exe" /V4 installer.nsi
   ```

3. The output will be `Paimbnails-Installer.exe` in the `installer/` folder.

## What the Installer Does

- Auto-detects your Geometry Dash installation (Steam, LocalAppData, or common paths)
- Copies `flozwer.paimbnails2.geode` into `GeometryDash/geode/mods/`
- Shows a confirmation message on success
- Opens the GitHub Releases page after install

## Download Page

The download page is hosted at the Paimbnails cloudflare server (`/download`) and includes:
- Platform detection (Windows, macOS, Linux, Android, iOS)
- Direct download links to GitHub Releases
- Platform-specific installation instructions

## GitHub Actions

The `.github/workflows/build.yml` automatically builds:
- Windows NSIS installer
- macOS DMG
- All platform `.geode` files (Windows, macOS, iOS, Android32, Android64)

Builds are attached to releases on every tag push (`v*`).
