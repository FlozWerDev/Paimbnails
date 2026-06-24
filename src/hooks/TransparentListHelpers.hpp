#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/TableViewCell.hpp>

using namespace geode::prelude;

namespace paimon::transparentlist {

inline bool isTransparentMode() {
    return Mod::get()->getSavedValue<bool>("transparent-list-mode", false);
}

inline void applyTransparentCellBg(CCNode* self) {
    if (!isTransparentMode()) return;
    auto* cell = typeinfo_cast<TableViewCell*>(self);
    if (!cell) return;

    if (auto* bg = cell->m_backgroundLayer) {
        if (auto* bgColor = typeinfo_cast<cocos2d::CCLayerColor*>(bg)) {
            auto size = bgColor->getContentSize();
            bgColor->changeWidthAndHeight(0.f, 0.f);
            bgColor->setContentSize(size);
        } else {
            bg->setVisible(false);
        }
    }
}

} // namespace paimon::transparentlist