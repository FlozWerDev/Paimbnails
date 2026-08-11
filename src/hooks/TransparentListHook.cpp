#include <Geode/Geode.hpp>
#include <Geode/modify/GJListLayer.hpp>
#include "../framework/HookConventions.hpp"
#include <Geode/modify/LevelListCell.hpp>
#include <Geode/modify/GJScoreCell.hpp>
#include "TransparentListHelpers.hpp"

using namespace geode::prelude;

// Hide the list fill while keeping its frame.
class $modify(PaimonGJListLayer, GJListLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "GJListLayer::init");
    }

    bool init(BoomListView* listView, char const* title, cocos2d::ccColor4B color, float width, float height, int type) {
        if (!GJListLayer::init(listView, title, color, width, height, type)) {
            return false;
        }

        if (paimon::transparentlist::isTransparentMode()) {
            this->setOpacity(0);
        }

        return true;
    }
};


class $modify(PaimonTransparentLevelListCell, LevelListCell) {
    void loadFromList(GJLevelList* list) {
        LevelListCell::loadFromList(list);
        paimon::transparentlist::applyTransparentCellBg(this);
    }
};

class $modify(PaimonTransparentGJScoreCell, GJScoreCell) {
    void loadFromScore(GJUserScore* score) {
        GJScoreCell::loadFromScore(score);
        paimon::transparentlist::applyTransparentCellBg(this);
    }
};
