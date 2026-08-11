#pragma once

// Editor panel: pick a template, capture new ones and run a build.

#include <Geode/Geode.hpp>
#include <Geode/binding/SetTextPopupDelegate.hpp>
#include <Geode/ui/Popup.hpp>

#include <functional>
#include <string>
#include <vector>

#include "../AutobuildTypes.hpp"

class EditorUI;

namespace geode {
class ScrollLayer;
}

namespace paimon::autobuild {

class AutobuildPopup : public geode::Popup, public SetTextPopupDelegate {
public:
    static AutobuildPopup* create();

    void setTextPopupClosed(SetTextPopup* popup, gd::string text) override;

private:
    bool init() override;
    void rebuild();
    void scheduleRebuild();

    std::vector<cocos2d::CCNode*> templatesTab(float width, float inner);
    std::vector<cocos2d::CCNode*> buildTab(float width, float inner);
    std::vector<cocos2d::CCNode*> settingsTab(float width, float inner);

    cocos2d::CCNode* templateRow(float width, int index);
    cocos2d::CCNode* stepperRow(float width, char const* title, char const* desc,
                                int value, std::function<void(int)> onChange);
    void buildActions();
    void addAction(char const* text, char const* sprite, bool enabled,
                   std::function<void()> onPress);
    void setCompact(bool compact);

    void runCapture(bool asSample);
    void importTemplate();
    void runBuild(bool reroll);
    void undoBuild();
    void askRename(int index);
    void confirmDelete(int index);
    void showHelp();

    void setStatus(std::string text, cocos2d::ccColor3B color);
    static EditorUI* editor();

    Options m_options;
    int m_tab = 0;
    int m_renameIndex = -1;
    bool m_compact = false;
    GLubyte m_dimOpacity = 0;
    std::string m_status;
    cocos2d::ccColor3B m_statusColor = {166, 176, 198};
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCMenu* m_actions = nullptr;
};

} // namespace paimon::autobuild
