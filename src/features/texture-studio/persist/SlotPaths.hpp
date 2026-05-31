#pragma once
//
// SlotPaths.hpp - Central place for the on-disk layout of texture-studio
// slots. Every other persist component goes through these helpers so the
// directory structure is documented in exactly one location and a future
// refactor (e.g. moving to {gd_save_dir}/paimbnails/...) only touches this
// file.
//
// On-disk layout:
//
//   <Mod::get()->getSaveDir()>/texture-studio/
//   ├── slots.json                         (master index)
//   └── slots/
//       └── <slot_id>/
//           ├── project.json               (TextureProject, no big blobs)
//           ├── overrides/
//           │   └── <sanitized_sprite>.bin (ManualOverride binary)
//           ├── cache/
//           │   └── auto.bin               (AutoDetectionCache)
//           └── output/
//               └── pack.zip               (last exported)
//
// Constraints:
//   - Sprite names contain "/" or ":" on some packs; we sanitize to "_"
//     so they're valid Windows/POSIX filenames.
//   - Slot ids come from PackMetadataBuilder::buildPackId, so they're
//     already lowercase + alphanumeric/underscore. No sanitization needed.
//

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace paimon::texture_studio {

class SlotPaths final {
public:
    // ── Root paths ─────────────────────────────────────────────────────

    // Root directory for all texture-studio state.
    static std::filesystem::path rootDir();

    // The master index file listing all slot ids.
    static std::filesystem::path slotsIndexFile();

    // Directory containing all per-slot folders.
    static std::filesystem::path slotsDir();

    // ── Per-slot paths ─────────────────────────────────────────────────

    // The directory holding everything for one slot.
    static std::filesystem::path slotDir(std::string_view slotId);

    // The slot's project.json (small metadata).
    static std::filesystem::path projectFile(std::string_view slotId);

    // The slot's overrides/ folder (one .bin per overridden sprite).
    static std::filesystem::path overridesDir(std::string_view slotId);

    // Path to one specific sprite's manual-override file. The sprite name
    // is sanitized (slashes / colons → underscores).
    static std::filesystem::path overrideFile(std::string_view slotId,
                                              std::string_view spriteName);

    // The slot's cache/auto.bin (clustering cache).
    static std::filesystem::path autoCacheFile(std::string_view slotId);

    // The slot's output zip file.
    static std::filesystem::path outputZipFile(std::string_view slotId);

    // ── Utilities ──────────────────────────────────────────────────────

    // Sanitize a sprite name into a filename-safe form. Idempotent.
    static std::string sanitizeFilename(std::string_view name);

    // Ensure all base directories exist for the given slot. Returns Err on
    // filesystem errors; safe to call multiple times.
    static geode::Result<> ensureSlotDirs(std::string_view slotId);

private:
    SlotPaths() = delete;
};

}  // namespace paimon::texture_studio
