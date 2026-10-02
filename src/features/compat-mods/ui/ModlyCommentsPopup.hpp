#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "../services/ModlyTypes.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <vector>

namespace paimon::compat_mods {

// read-only comment view; posting needs modly firebase auth the mod lacks.
class ModlyCommentsPopup : public PaimonPopup {
public:
    static ModlyCommentsPopup* create(ModlyMod const& mod);

protected:
    ModlyMod m_mod;
    cocos2d::CCNode* m_listHolder = nullptr;

    bool init(ModlyMod const& mod);
    void load();
    void showStatus(std::string const& text);
    void buildList(std::vector<ModlyComment> const& comments);
    cocos2d::CCNode* buildCommentCard(ModlyComment const& comment, float width, int index);
};

} // namespace paimon::compat_mods
