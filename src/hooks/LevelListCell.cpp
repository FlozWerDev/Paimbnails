#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListCell.hpp>
#include "../framework/HookConventions.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelListCell, LevelListCell) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelListCell::loadFromList");
    }

    // init hook removed (compile errors)

    $override
    void loadFromList(GJLevelList* list) {
        LevelListCell::loadFromList(list);

        if (!list) {
            log::warn("PaimonLevelListCell: list is null");
            return;
        }

        log::debug("PaimonLevelListCell: loadFromList called for list ID: {}", list->m_listID);
    }
};
