#pragma once
//
// ManualOverrideStore.hpp - Compact binary IO for the per-sprite mask data
// that the manual editor produces.
//
// File format (one .bin per overridden sprite):
//
//   Offset  Bytes  Meaning
//   0       4      Magic: 'P','M','O','V'  (0x564F4D50 little-endian)
//   4       2      Version: u16le, currently 1
//   6       2      Flags: u16le bitfield (bit 0 = hasC1, 1 = hasC2,
//                                          2 = hasGlow, 3 = hasOutline)
//   8       4      width:  u32le
//   12      4      height: u32le
//   16+     bytes  Mask blocks in order of bit position (only those
//                  whose flag is set). Each block = width*height bytes.
//
// Why a custom binary format vs base64+JSON:
//   - 4 masks × 256² = 256 KB raw vs ~340 KB base64 vs ~120 KB BIN with
//     compression we could add later. JSON parse cost dominates the read
//     time at scale (parsing 100 overrides × 340 KB each is noticeable).
//   - Trivially zero-copy on read: we just get a span<uint8_t> straight
//     from file::readBinary.
//   - Forward-compatible: if we add float-precision masks later, bump
//     the version byte and add a new flag. Existing readers ignore.
//

#include "../engine/MaskBuilder.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string_view>

namespace paimon::texture_studio {

class ManualOverrideStore final {
public:
    // Magic number identifying valid override files.
    static constexpr std::uint32_t kMagic   = 0x564F4D50u;  // "PMOV"
    static constexpr std::uint16_t kVersion = 1;

    // Bit positions in the flags field.
    static constexpr std::uint16_t kFlagHasC1      = 1 << 0;
    static constexpr std::uint16_t kFlagHasC2      = 1 << 1;
    static constexpr std::uint16_t kFlagHasGlow    = 1 << 2;
    static constexpr std::uint16_t kFlagHasOutline = 1 << 3;

    // Save a MaskSet to disk. Empty masks are omitted (their flag bit
    // stays clear and no bytes are written).
    static geode::Result<> save(std::filesystem::path const& path,
                                MaskSet const& masks);

    // Load a MaskSet from disk. Roles whose flags are not set come back
    // as empty MaskBuffers (size 0).
    //
    // Returns Err on:
    //   - Missing magic / unsupported version
    //   - Truncated payload
    //   - Width/height bytes implying buffer larger than file
    static geode::Result<MaskSet> load(std::filesystem::path const& path);

    // Convenience: save by (slotId, spriteName) using SlotPaths.
    static geode::Result<> saveForSlot(std::string_view slotId,
                                       std::string_view spriteName,
                                       MaskSet const& masks);

    static geode::Result<MaskSet> loadForSlot(std::string_view slotId,
                                              std::string_view spriteName);

    // Delete the .bin for one override (after user clicks "reset to auto").
    // Returns Ok even if the file doesn't exist — idempotent semantics.
    static geode::Result<> deleteForSlot(std::string_view slotId,
                                         std::string_view spriteName);

private:
    ManualOverrideStore() = delete;
};

}  // namespace paimon::texture_studio
