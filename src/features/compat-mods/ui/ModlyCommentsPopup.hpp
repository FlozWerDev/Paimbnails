#pragma once

#include "../services/ModlyTypes.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <vector>

namespace paimon::compat_mods {

// Read-only view of a project's comments. Posting needs a Modly Firebase Auth
// account, which the mod does not have, so this only lists them.
class ModlyCommentsPopup : public geode::Popup {
public:
    static ModlyCommentsPopup* create(ModlyMod const& mod);

protected:
    ModlyMod m_mod;
    cocos2d::CCNode* m_listHolder = nullptr;
    cocos2d::CCNode* m_status = nullptr;

    bool init(ModlyMod const& mod);
    void load();
    void showStatus(std::string const& text);
    void buildList(std::vector<ModlyComment> const& comments);
    cocos2d::CCNode* buildCommentCard(ModlyComment const& comment, float width, int index);
};

} // namespace paimon::compat_mods
