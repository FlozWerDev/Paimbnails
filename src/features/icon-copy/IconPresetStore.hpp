#pragma once
// Icon sets you put together yourself. The copy store keeps one snapshot per
// user you visited; this one keeps as many of your own stylings as you care to
// save, so you can try something out and still get the old look back.

#include "IconCopyStore.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::iconcopy {

// The name lives in `set.username`, so every widget that already knows how to
// label a copied set labels a saved styling the same way.
struct IconPreset {
    uint32_t id = 0;
    int64_t savedAt = 0;
    IconSet set;

    std::string const& name() const { return set.username; }
};

constexpr size_t kMaxPresets = 100;

// Whatever the player is wearing right now.
IconSet currentPlayerSet();

// Saved sets, newest first.
std::vector<IconPreset> const& presets();

// The new id, or 0 when the list is already full.
uint32_t addPreset(std::string name, IconSet set);
void renamePreset(uint32_t id, std::string name);
void removePreset(uint32_t id);

// "Style 4": the first number nothing else is called.
std::string suggestPresetName();

}  // namespace paimon::iconcopy

template <>
struct matjson::Serialize<paimon::iconcopy::IconPreset> {
    static geode::Result<paimon::iconcopy::IconPreset> fromJson(matjson::Value const& value);
    static matjson::Value toJson(paimon::iconcopy::IconPreset const& preset);
};
