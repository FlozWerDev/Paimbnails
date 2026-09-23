#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <functional>
#include <string>

namespace paimon::icon_maker {

class TemplatePickerPopup : public geode::Popup {
public:
    using PickedCallback = std::function<void(int iconId)>;
    using ProjectCallback = std::function<void(std::string const& projectId)>;

    // Con `onProject` puesto aparece la pestana "Mis iconos", que solo lista
    // proyectos del mismo gamemode para que las zonas cuadren.
    static TemplatePickerPopup* create(IconType type, PickedCallback onPicked,
                                       ProjectCallback onProject = nullptr);

protected:
    bool init(IconType type, PickedCallback onPicked, ProjectCallback onProject);

    void rebuildPage();
    void rebuildVanillaPage(cocos2d::CCMenu* menu);
    void rebuildProjectPage(cocos2d::CCMenu* menu);
    void jumpToId(int iconId);
    int iconCount() const;
    int pageCount() const;

    IconType m_type = IconType::Cube;
    PickedCallback m_onPicked;
    ProjectCallback m_onProject;

    cocos2d::CCNode* m_gridArea = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    geode::TextInput* m_idInput = nullptr;
    std::vector<std::string> m_projectIds;
    int m_page = 0;
    bool m_mine = false;
};

}  // namespace paimon::icon_maker
