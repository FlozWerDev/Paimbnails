#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "../services/ModlyTypes.hpp"
#include <Geode/ui/Popup.hpp>
#include <vector>

namespace paimon::compat_mods {

class ModlyProfilePopup : public PaimonPopup {
public:
    static ModlyProfilePopup* create(ModlyUser const& user);

protected:
    ModlyUser m_user;
    std::vector<ModlyMod> m_projects;

    bool init(ModlyUser const& user);
    void buildHeader();
    void buildProjects();

    void onProject(cocos2d::CCObject* sender);
};

} // namespace paimon::compat_mods
