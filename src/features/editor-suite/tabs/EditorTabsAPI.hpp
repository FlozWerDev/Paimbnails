#pragma once

// =============================================================================
// Paimon Editor Tabs API (internal + used by PublicExports)
// =============================================================================

#include <Geode/Geode.hpp>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../api/Compat.hpp"

namespace paimon::editor::tabs {

enum class Mode { Build, Edit, Delete, Custom };

enum class Backend { None, Injected, Fallback };

struct TabDesc {
    std::string id;
    Mode mode = Mode::Build;
    std::string customModeId;

    // Optional panel content. Leave empty / return nullptr for action-only tabs
    // (Search, Favs) that open an overlay and must NEVER cover the canvas.
    std::function<cocos2d::CCNode*()> createContent;
    std::function<cocos2d::CCNode*()> createIcon;
    std::function<void(bool shown, cocos2d::CCNode* content)> onToggle;
    // Fired once when the tab is activated (preferred for action-only tabs).
    std::function<void()> onActivate;
    std::function<void(int rows, int cols, cocos2d::CCNode* content)> onReload;
    std::function<bool()> isEnabled;
    std::string displayName;

    // Stable visual order. Lower values are inserted first.
    int displayOrder = 100;

    // true = click runs onActivate/onToggle and leaves the create bar alone.
    // No floating panel, no gray slab, no hide of m_createButtonBar.
    bool actionOnly = false;
};

bool registerTab(TabDesc desc);
bool unregisterTab(std::string_view id);
void setTabEnabled(std::string_view id, bool enabled);
bool isTabRegistered(std::string_view id);
void flushToEditor(EditorUI* ui);
void setTabsVisible(bool visible);
void clearSession();

bool switchTab(std::string_view id);
bool switchMode(Mode mode);
void updateTabMenu();

Backend backendInUse();
std::optional<std::string> getCurrentTabId();
std::optional<Mode> getCurrentMode();
std::vector<std::string> registeredTabIds();
std::vector<cocos2d::CCNode*> getAllTabNodes();
cocos2d::CCNode* nodeForTab(std::string_view id);
CCMenuItemToggler* togglerForTab(std::string_view id);
int tabIndex(std::string_view id);
std::optional<std::string> modeForTab(std::string_view id);
std::optional<std::string> idForTabNode(cocos2d::CCNode* tab);

using ModeCallback = std::function<void(Mode)>;
using TabCallback = std::function<void(std::string_view id)>;
void addModeSwitchCallback(ModeCallback cb);
void addTabSwitchCallback(TabCallback cb);

std::string modeIdString(Mode mode, std::string_view custom = {});
Mode modeFromId(std::string_view modeId);
EditButtonBar* createEditButtonBar(std::vector<geode::Ref<cocos2d::CCNode>> const& nodes);
cocos2d::CCNode* makeButtonBar(std::vector<geode::Ref<cocos2d::CCNode>> nodes);
std::function<cocos2d::CCNode*()> iconFromFrame(char const* frameName);

void changeModeSprites(bool enabled);
bool modeSpritesEnabled();

inline bool alphaApiLoaded() {
    return geode::Loader::get()->isModLoaded("alphalaneous.editortab_api");
}

} // namespace paimon::editor::tabs
