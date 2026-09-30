#pragma once
// your own saved stylings (the copy store keeps one snapshot per visited user),
// so you can experiment and still get the old look back.

#include "IconCopyStore.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::iconcopy {

// the name lives in `set.username`, so every widget that already knows how to
// label a copied set labels a saved styling the same way.
struct IconPreset {
    uint32_t id = 0;
    int64_t savedAt = 0;
    IconSet set;

    std::string const& name() const { return set.username; }
};

constexpr size_t kMaxPresets = 100;

// whatever the player is wearing right now.
IconSet currentPlayerSet();

// saved sets, newest first.
std::vector<IconPreset> const& presets();

// the new id, or 0 when the list is already full.
uint32_t addPreset(std::string name, IconSet set);
void renamePreset(uint32_t id, std::string name);
void removePreset(uint32_t id);

// first free "style n" number.
std::string suggestPresetName();

}  // namespace paimon::iconcopy

template <>
struct matjson::Serialize<paimon::iconcopy::IconPreset> {
    static geode::Result<paimon::iconcopy::IconPreset> fromJson(matjson::Value const& value);
    static matjson::Value toJson(paimon::iconcopy::IconPreset const& preset);
};
