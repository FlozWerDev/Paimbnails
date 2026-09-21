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

} // namespace paimon::info::compat
