#pragma once

#include "../services/ModlyTypes.hpp"
#include <Geode/ui/Popup.hpp>
#include <vector>

namespace paimon::compat_mods {

// Developer profile as the site shows it: banner, avatar, verified seal,
// description, tags and the projects the user published.
class ModlyProfilePopup : public geode::Popup {
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
