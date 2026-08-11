#include "InfoCompat.hpp"
#include "../../framework/compat/ModCompat.hpp"
#include <Geode/loader/Mod.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace paimon::info::compat {

namespace {

// Modules whose UI lands in the same place as BetterInfo's. Advanced Search,
// Search Presets and the progress modules are absent on purpose: BetterInfo has
// no equivalent, so running both is fine.
constexpr std::string_view kOverlapping[] = {
    "info-mod-extended",
    "info-mod-ids",
    "info-mod-jump-page",
    "info-mod-unreg-profiles",
    "info-mod-comment-tools",
    "info-mod-green-users",
};

} // namespace

bool cedingToBetterInfo() {
    if (!paimon::compat::ModCompat::isBetterInfoLoaded()) return false;
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting("info-compat-force")) return true;
    return !mod->getSettingValue<bool>("info-compat-force");
}

bool overlapsBetterInfo(std::string_view key) {
    return std::find(std::begin(kOverlapping), std::end(kOverlapping), key)
        != std::end(kOverlapping);
}

bool isCeded(std::string_view key) {
    return cedingToBetterInfo() && overlapsBetterInfo(key);
}

std::vector<std::string_view> const& overlappingKeys() {
    static std::vector<std::string_view> const kKeys(
        std::begin(kOverlapping), std::end(kOverlapping));
    return kKeys;
}

int cededModuleCount() {
    if (!cedingToBetterInfo()) return 0;
    auto* mod = Mod::get();
    if (!mod) return 0;

    int count = 0;
    for (auto key : kOverlapping) {
        if (!mod->hasSetting(key)) continue;
        if (mod->getSettingValue<bool>(std::string(key))) count++;
    }
    return count;
}

} // namespace paimon::info::compat
