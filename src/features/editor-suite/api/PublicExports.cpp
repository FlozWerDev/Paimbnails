// Non-inline exports for include/paimon/editor/* (api.headers for other mods).

#include <paimon/editor/Tabs.hpp>
#include <paimon/editor/PaimonEditor.hpp>

#include "../tabs/EditorTabsAPI.hpp"
#include "FakePause.hpp"
#include "GroupViewAPI.hpp"
#include "UIScaleAPI.hpp"

#include <Geode/binding/LevelEditorLayer.hpp>

namespace paimon::editor_tabs {

void addTab(
    std::string_view tabID,
    std::string_view modeID,
    CreateTab createTab,
    CreateTabIcon createIcon,
    ToggleTab toggleTab,
    ReloadTab reloadTab
) {
    paimon::editor::tabs::TabDesc d;
    d.id = std::string(tabID);
    d.mode = paimon::editor::tabs::modeFromId(modeID);
    if (d.mode == paimon::editor::tabs::Mode::Custom) d.customModeId = std::string(modeID);
    d.createContent = std::move(createTab);
    d.createIcon = std::move(createIcon);
    d.onToggle = std::move(toggleTab);
    d.onReload = std::move(reloadTab);
    d.displayName = d.id;
    auto slash = d.displayName.rfind('/');
    if (slash != std::string::npos) d.displayName = d.displayName.substr(slash + 1);
    paimon::editor::tabs::registerTab(std::move(d));
}

void removeTab(std::string_view tabID) {
    paimon::editor::tabs::unregisterTab(tabID);
}

void switchTab(std::string_view tabID) {
    paimon::editor::tabs::switchTab(tabID);
}

void switchMode(std::string_view modeID) {
    paimon::editor::tabs::switchMode(paimon::editor::tabs::modeFromId(modeID));
}

void changeModeSprites(bool enabled) {
    paimon::editor::tabs::changeModeSprites(enabled);
}

void updateTabMenu() {
    paimon::editor::tabs::updateTabMenu();
}

void addModeSwitchCallback(std::function<void(std::string_view)> cb) {
    paimon::editor::tabs::addModeSwitchCallback(
        [cb = std::move(cb)](paimon::editor::tabs::Mode m) {
            if (cb) cb(paimon::editor::tabs::modeIdString(m));
        }
    );
}

void addTabSwitchCallback(std::function<void(std::string_view)> cb) {
    paimon::editor::tabs::addTabSwitchCallback(std::move(cb));
}

EditButtonBar* createEditButtonBar(std::vector<geode::Ref<cocos2d::CCNode>> const& nodes) {
    return paimon::editor::tabs::createEditButtonBar(nodes);
}

} // namespace paimon::editor_tabs

namespace paimon::editor {

void updateGroupView() {
    group_view::updateGroupView();
}

float uiScale() {
    return ui_scale::currentScale();
}

bool runBuildHelper() {
    return ::paimon::editor::runBuildHelper(LevelEditorLayer::get());
}

} // namespace paimon::editor
