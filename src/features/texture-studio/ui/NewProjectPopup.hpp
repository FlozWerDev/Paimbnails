#pragma once

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace paimon::texture_studio {

class NewProjectPopup : public geode::Popup {
public:
    using SlotCreatedCallback = std::function<void(std::string const& slotId)>;

    static NewProjectPopup* create(SlotCreatedCallback cb);

protected:
    bool init(SlotCreatedCallback cb);

    void refreshSheetsList();
    void setAllChecked(bool checked);

    void onCreateClicked(cocos2d::CCObject*);

private:
    SlotCreatedCallback m_onCreated;

    geode::TextInput* m_nameInput   = nullptr;
    geode::TextInput* m_authorInput = nullptr;
    cocos2d::CCNode*  m_sheetsListContainer = nullptr;

    struct SheetRow {
        std::string baseName;
        std::string qualitySuffix;
        std::string plistPath;
        std::string pngPath;
        bool checked = true;
    };
    std::vector<SheetRow> m_rows;
};

}  // namespace paimon::texture_studio
