#pragma once

#include "../engine/MaskBuilder.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string_view>

namespace paimon::texture_studio {

class ManualOverrideStore final {
public:
    static constexpr std::uint32_t kMagic   = 0x564F4D50u;  // "PMOV"
    static constexpr std::uint16_t kVersion = 1;

    static constexpr std::uint16_t kFlagHasC1      = 1 << 0;
    static constexpr std::uint16_t kFlagHasC2      = 1 << 1;
    static constexpr std::uint16_t kFlagHasGlow    = 1 << 2;
    static constexpr std::uint16_t kFlagHasOutline = 1 << 3;

    // Empty masks are omitted (their flag bit stays clear, no bytes written).
    static geode::Result<> save(std::filesystem::path const& path,
                                MaskSet const& masks);

    // Roles whose flags are not set come back as empty MaskBuffers (size 0).
    static geode::Result<MaskSet> load(std::filesystem::path const& path);

    static geode::Result<> saveForSlot(std::string_view slotId,
                                       std::string_view spriteName,
                                       MaskSet const& masks);

    static geode::Result<MaskSet> loadForSlot(std::string_view slotId,
                                              std::string_view spriteName);

    // Returns Ok even if the file doesn't exist — idempotent semantics.
    static geode::Result<> deleteForSlot(std::string_view slotId,
                                         std::string_view spriteName);

private:
    ManualOverrideStore() = delete;
};

}  // namespace paimon::texture_studio
