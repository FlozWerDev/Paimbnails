#pragma once
#include "../../../utils/FilesystemPath.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Localization.hpp"
#include "../services/ShopLibrary.hpp"

namespace paimon::shop {
inline char const* text(char const* spanish, char const* english) {
    return Localization::get().getLanguage() == Localization::Language::SPANISH ? spanish : english;
}
inline ShopLibrary localLibrary() { return ShopLibrary(paimon::paths::toStdPath(geode::Mod::get()->getSaveDir()) / "paimon-shop" / "presets"); }
void showShopError(std::string const& error);
cocos2d::CCNode* makePreview(std::string const& encoded, cocos2d::CCSize size);
} // namespace paimon::shop
