#include "EditorTabsAPI.hpp"
#include "../api/Events.hpp"
#include "../EditorAssets.hpp"

#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/EditButtonBar.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <algorithm>
#include <unordered_map>

#include "../../../framework/HookConventions.hpp"
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::editor::tabs {

struct LiveTab {
    TabDesc desc;
    bool userEnabled = true;
    Ref<CCNode> content;
    Ref<CCMenuItemToggler> toggler;
    Ref<EditButtonBar> bar;
    Ref<CCMenuItemSpriteExtra> fallbackBtn;
    int buildIndex = -1;
    bool flushed = false;
};

struct State {
    std::unordered_map<std::string, LiveTab> tabs;
    Backend backend = Backend::None;
    WeakRef<EditorUI> ui;
    Ref<CCMenu> fallbackMenu;
    std::string currentTabId;
    Mode currentMode = Mode::Build;
    std::vector<ModeCallback> modeCbs;
    std::vector<TabCallback> tabCbs;
    bool flushing = false;
    bool modeSprites = true;
};

State& st() {
    static State s;
    return s;
}

bool tabActive(LiveTab const& t) {
    if (!t.userEnabled) return false;
    if (t.desc.isEnabled && !t.desc.isEnabled()) return false;
    return true;
}

std::vector<LiveTab*> orderedTabs() {
    std::vector<LiveTab*> ordered;
    ordered.reserve(st().tabs.size());
    for (auto& [_, live] : st().tabs) ordered.push_back(&live);
    std::ranges::sort(ordered, [](LiveTab const* left, LiveTab const* right) {
        if (left->desc.displayOrder != right->desc.displayOrder) {
            return left->desc.displayOrder < right->desc.displayOrder;
        }
        return left->desc.id < right->desc.id;
    });
    return ordered;
}

void fireTabCbs(std::string_view id) {
    for (auto& cb : st().tabCbs) {
        if (cb) cb(id);
    }
}

void fireModeCbs(Mode m) {
    for (auto& cb : st().modeCbs) {
        if (cb) cb(m);
    }
}

void syncTogglerAfterActivation(std::string id) {
    Loader::get()->queueInMainThread([id = std::move(id)] {
        auto it = st().tabs.find(id);
        if (it == st().tabs.end() || !it->second.toggler) return;
        auto& live = it->second;
        bool const selected = !live.desc.actionOnly
            && st().currentTabId == id
            && (!live.content || live.content->isVisible());
        if (live.toggler->isToggled() != selected) {
            live.toggler->toggle(selected);
        }
    });
}

void showInjectedBar(EditorUI* ui, LiveTab& live) {
    if (!ui || !ui->m_createButtonBars) return;
    for (auto* bar : CCArrayExt<EditButtonBar*>(ui->m_createButtonBars)) {
        if (bar) bar->setVisible(false);
    }
    if (live.bar) {
        live.bar->setVisible(true);
        live.content = live.bar;
    }
    if (ui->m_tabsArray) {
        for (auto* obj : CCArrayExt<CCMenuItemToggler*>(ui->m_tabsArray)) {
            if (!obj) continue;
            obj->toggle(live.toggler && obj == live.toggler.data());
        }
    }
    st().currentTabId = live.desc.id;
    fireTabCbs(live.desc.id);
    if (live.desc.onToggle) {
        live.desc.onToggle(true, live.bar ? static_cast<CCNode*>(live.bar.data()) : nullptr);
    }
}

// NEVER use the user's Button Rows Bypass (can be 24x48) for Paimon UI tabs —
// that creates full-screen gray EditButtonBars covering the canvas.
void clampTabGrid(int& rows, int& cols) {
    if (rows < 1) rows = 2;
    if (cols < 1) cols = 6;
    rows = std::min(rows, 3);
    cols = std::min(cols, 10);
}

CCMenuItemToggler* makeTabToggler(LiveTab& live, int tag) {
    auto makeIcon = [&]() -> CCSprite* {
        CCSprite* icon = nullptr;
        if (live.desc.createIcon) icon = typeinfo_cast<CCSprite*>(live.desc.createIcon());
        if (!icon) icon = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        return icon;
    };
    auto makeTabSprite = [&](bool on) -> CCNode* {
        auto* bg = CCSprite::createWithSpriteFrameName(
            on ? "GJ_tabOn_001.png" : "GJ_tabOff_001.png"
        );
        if (!bg) {
            return ButtonSprite::create(
                makeIcon(), 50, true, 50,
                on ? "GJ_button_02.png" : "GJ_button_05.png", .6f
            );
        }
        if (auto* icon = makeIcon()) {
            auto bgSize = bg->getContentSize();
            float maxDim = std::max(
                icon->getContentSize().width, icon->getContentSize().height
            );
            if (maxDim > 1.f) {
                icon->setScale(std::min(1.f, bgSize.height * 0.62f / maxDim));
            }
            icon->setPosition({bgSize.width / 2.f, bgSize.height / 2.f});
            if (!on) {
                icon->setColor({140, 140, 140});
                icon->setOpacity(200);
            }
            bg->addChild(icon);
        }
        return bg;
    };
    auto* onBg = makeTabSprite(true);
    auto* offBg = makeTabSprite(false);
    if (!offBg || !onBg) return nullptr;
    auto id = live.desc.id;
    auto* tog = CCMenuItemExt::createToggler(onBg, offBg, [id](CCMenuItemToggler*) {
        switchTab(id);
        syncTogglerAfterActivation(id);
    });
    if (!tog) return nullptr;
    tog->setID("paimbnails/tab-toggler/" + live.desc.id);
    tog->setTag(tag);
    tog->toggle(false);
    return tog;
}

bool tryInjectBuildTab(EditorUI* ui, LiveTab& live) {
    if (!ui || !ui->m_tabsMenu || !ui->m_createButtonBars || !ui->m_tabsArray) return false;
    if (live.desc.mode != Mode::Build) return false;

    // All Paimon tabs (action + View panel): only inject a toggler into the tab
    // menu. Content is created lazily as a tiny floating card in switchTab —
    // never a full-width host sized like the create bar (that was the gray slab).
    auto* tog = makeTabToggler(live, live.desc.actionOnly ? -3 : -2);
    if (!tog) return false;
    ui->m_tabsMenu->addChild(tog);
    ui->m_tabsArray->addObject(tog);
    ui->m_tabsMenu->updateLayout();
    live.toggler = tog;
    live.bar = nullptr;
    live.content = nullptr; // lazy
    live.buildIndex = live.desc.actionOnly ? -3 : -2;
    live.flushed = true;
    return true;
}

// Small floating panel above the create bar — NEVER full-width chrome.
// Does NOT hide m_createButtonBar (that left empty gray slabs on screen).
void placeCompactDock(EditorUI* ui, CCNode* panel) {
    if (!ui || !panel) return;
    auto win = CCDirector::get()->getWinSize();
    panel->setAnchorPoint({0.5f, 0.f});
    // Sit just above the object bar, centered, as a floating strip.
    float y = 110.f;
    float x = win.width * 0.5f;
    if (ui->m_createButtonBar) {
        auto p = ui->m_createButtonBar->getPosition();
        auto sc = ui->m_createButtonBar->getScale();
        // Use a fixed small lift so multi-row create bars never push us mid-canvas.
        y = std::min(p.y + 55.f * sc, win.height * 0.42f);
        x = p.x;
    }
    panel->setPosition({x, y});
    panel->setScale(0.9f);
}

void restoreCreateBars(EditorUI* ui) {
    if (!ui || ui->m_selectedMode != 2) return;
    if (ui->m_createButtonBar) ui->m_createButtonBar->setVisible(true);
    if (ui->m_createButtonBars) {
        // Only re-show the bar that matches the current vanilla tab; GD owns the rest.
        // Showing all bars stacks gray slabs — leave them to selectBuildTab.
    }
}

void fireActionTab(LiveTab& live) {
    if (live.desc.onActivate) live.desc.onActivate();
    else if (live.desc.onToggle) live.desc.onToggle(true, nullptr);
    fireTabCbs(live.desc.id);
    // Momentary press: leave toggler off so it doesn't look "stuck" selected.
    if (live.toggler) live.toggler->toggle(false);
    st().currentTabId.clear();
}

// Ensure a compact content node exists (View panel only). Never uses EditButtonBar.
void ensurePanelContent(EditorUI* ui, LiveTab& live) {
    if (live.content || !live.desc.createContent) return;
    auto* node = live.desc.createContent();
    if (!node) return;
    live.content = node;
    live.content->setID("paimbnails/tab-content/" + live.desc.id);
    placeCompactDock(ui, live.content.data());

    auto sz = live.content->getContentSize();
    if (sz.width < 10.f) sz = CCSize{200.f, 44.f};
    // Hard cap — a multi-row create-bar height must never leak into our panel.
    sz.width = std::min(sz.width, 340.f);
    sz.height = std::min(sz.height, 88.f);
    live.content->setContentSize(sz);

    if (auto* bg = CCScale9Sprite::create("square02b_001.png")) {
        bg->setColor({15, 15, 22});
        bg->setOpacity(210);
        bg->setContentSize({sz.width + 14.f, sz.height + 10.f});
        bg->setPosition(sz / 2.f);
        live.content->addChild(bg, -1);
    }
    live.content->setVisible(false);
    // Do NOT add to m_uiItems — GD re-shows uiItems after playtest and can
    // reintroduce large empty chrome for nodes it doesn't understand.
    ui->addChild(live.content, 60);
}

// Dock Paimon tab openers as ICON TABS (GJ_tab style), never text "FAVS/VIEW".
// When Alpha EditorTab-API owns m_tabsMenu we cannot inject there, so we park a
// compact row of real-looking tab faces next to the BUILD/EDIT/DELETE column —
// same visual language as vanilla create tabs / BetterEdit View tab.
bool flushFallback(EditorUI* ui, LiveTab& live) {
    if (live.flushed || !ui || !tabActive(live)) return live.flushed;
    auto& s = st();
    if (!s.fallbackMenu) {
        auto win = CCDirector::get()->getWinSize();
        auto* menu = CCMenu::create();
        menu->setID("paimbnails/editor-tabs-fallback-menu");
        // Default: left edge above BUILD/EDIT/DELETE (not mid-canvas).
        menu->setPosition({48.f, 150.f});
        menu->setLayout(
            ColumnLayout::create()
                ->setGap(2.f)
                ->setAxisReverse(true)
                ->setAutoScale(false)
                ->setAxisAlignment(AxisAlignment::Center)
        );

        // Prefer: sit just above the mode-menu (build/edit/delete).
        if (auto* mode = typeinfo_cast<CCMenu*>(ui->getChildByID("toolbar-categories-menu"))) {
            auto world = mode->getParent()
                ? mode->getParent()->convertToWorldSpace(mode->getPosition())
                : mode->getPosition();
            auto local = ui->convertToNodeSpace(world);
            auto cs = mode->getContentSize();
            menu->setPosition({
                local.x + cs.width * mode->getScale() * 0.5f,
                local.y + cs.height * mode->getScale() * 0.5f + 28.f
            });
        } else if (auto* tabs = typeinfo_cast<CCMenu*>(ui->getChildByID("build-tabs-menu"))) {
            // Last resort: left of the category tabs, not centered on them.
            auto world = tabs->getParent()
                ? tabs->getParent()->convertToWorldSpace(tabs->getPosition())
                : tabs->getPosition();
            auto local = ui->convertToNodeSpace(world);
            menu->setPosition({std::max(40.f, local.x - 120.f), local.y + 8.f});
            menu->setLayout(
                RowLayout::create()->setGap(2.f)->setAutoScale(false)
            );
        }

        ui->addChild(menu, 55);
        if (ui->m_uiItems) ui->m_uiItems->addObject(menu);
        s.fallbackMenu = menu;
    }

    // Build GJ_tab faces with the tab's own icon (View/Search/Favs).
    auto makeIcon = [&]() -> CCSprite* {
        CCSprite* icon = nullptr;
        if (live.desc.createIcon) {
            icon = typeinfo_cast<CCSprite*>(live.desc.createIcon());
        }
        if (!icon) {
            icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
        }
        return icon;
    };
    auto* iconOn = makeIcon();
    auto* iconOff = makeIcon(); // second instance for the off face

    auto id = live.desc.id;
    auto* tog = paimon::editor::assets::tabStyleToggler(
        iconOn, iconOff,
        [id](bool /*toggled*/) {
            // Always route through switchTab (handles open/close + exclusivity).
            switchTab(id);
            syncTogglerAfterActivation(id);
        }
    );
    if (!tog) return false;
    tog->setID("paimbnails/tab-fallback/" + live.desc.id);
    tog->toggle(false);
    s.fallbackMenu->addChild(tog);
    s.fallbackMenu->updateLayout();
    // Keep as toggler so switchTab / hideAll / setTabsVisible stay in sync.
    live.toggler = tog;
    live.flushed = true;
    return true;
}

// --- Public API ---

std::string modeIdString(Mode mode, std::string_view custom) {
    switch (mode) {
        case Mode::Build: return "robtop.geometry-dash/build";
        case Mode::Edit: return "robtop.geometry-dash/edit";
        case Mode::Delete: return "robtop.geometry-dash/delete";
        case Mode::Custom: return custom.empty() ? "paimbnails/custom" : std::string(custom);
    }
    return "robtop.geometry-dash/build";
}

Mode modeFromId(std::string_view modeId) {
    if (modeId == "robtop.geometry-dash/build" || modeId == "build") return Mode::Build;
    if (modeId == "robtop.geometry-dash/edit" || modeId == "edit") return Mode::Edit;
    if (modeId == "robtop.geometry-dash/delete" || modeId == "delete") return Mode::Delete;
    return Mode::Custom;
}

void changeModeSprites(bool enabled) { st().modeSprites = enabled; }
bool modeSpritesEnabled() { return st().modeSprites; }

bool registerTab(TabDesc desc) {
    if (desc.id.empty() || !desc.createIcon) return false;
    // Panel tabs need createContent; action-only need onActivate or onToggle.
    if (!desc.actionOnly && !desc.createContent) return false;
    if (desc.actionOnly && !desc.onActivate && !desc.onToggle) return false;
    auto& s = st();
    if (s.tabs.contains(desc.id)) return false;
    std::string id = desc.id;
    LiveTab live;
    live.desc = std::move(desc);
    s.tabs.emplace(id, std::move(live));
    if (auto ui = s.ui.lock()) flushToEditor(ui.data());
    return true;
}

bool unregisterTab(std::string_view id) {
    auto& s = st();
    auto it = s.tabs.find(std::string(id));
    if (it == s.tabs.end()) return false;
    auto& live = it->second;
    if (live.toggler) live.toggler->removeFromParent();
    if (live.bar) live.bar->removeFromParent();
    if (live.content && live.buildIndex < 0) live.content->removeFromParent();
    if (live.fallbackBtn) live.fallbackBtn->removeFromParent();
    s.tabs.erase(it);
    return true;
}

void setTabEnabled(std::string_view id, bool enabled) {
    auto& s = st();
    auto it = s.tabs.find(std::string(id));
    if (it == s.tabs.end()) return;
    it->second.userEnabled = enabled;
    if (it->second.toggler) it->second.toggler->setVisible(enabled && tabActive(it->second));
    if (it->second.fallbackBtn) it->second.fallbackBtn->setVisible(enabled && tabActive(it->second));
    if (!enabled && it->second.content) it->second.content->setVisible(false);
}

bool isTabRegistered(std::string_view id) {
    return st().tabs.contains(std::string(id));
}

std::vector<std::string> registeredTabIds() {
    std::vector<std::string> out;
    for (auto const* live : orderedTabs()) out.push_back(live->desc.id);
    return out;
}

Backend backendInUse() { return st().backend; }

std::optional<std::string> getCurrentTabId() {
    if (st().currentTabId.empty()) return std::nullopt;
    return st().currentTabId;
}

std::optional<Mode> getCurrentMode() { return st().currentMode; }

CCNode* nodeForTab(std::string_view id) {
    auto it = st().tabs.find(std::string(id));
    if (it == st().tabs.end()) return nullptr;
    if (it->second.bar) return it->second.bar.data();
    return it->second.content.data();
}

CCMenuItemToggler* togglerForTab(std::string_view id) {
    auto it = st().tabs.find(std::string(id));
    if (it == st().tabs.end()) return nullptr;
    return it->second.toggler.data();
}

int tabIndex(std::string_view id) {
    auto it = st().tabs.find(std::string(id));
    return it == st().tabs.end() ? -1 : it->second.buildIndex;
}

std::vector<CCNode*> getAllTabNodes() {
    std::vector<CCNode*> out;
    for (auto const& [_, live] : st().tabs) {
        if (live.bar) out.push_back(live.bar.data());
        else if (live.content) out.push_back(live.content.data());
    }
    return out;
}

std::optional<std::string> modeForTab(std::string_view id) {
    auto it = st().tabs.find(std::string(id));
    if (it == st().tabs.end()) return std::nullopt;
    return modeIdString(it->second.desc.mode, it->second.desc.customModeId);
}

std::optional<std::string> idForTabNode(CCNode* tab) {
    if (!tab) return std::nullopt;
    for (auto const& [id, live] : st().tabs) {
        if (live.bar.data() == tab || live.content.data() == tab) return id;
    }
    return std::nullopt;
}

void addModeSwitchCallback(ModeCallback cb) { st().modeCbs.push_back(std::move(cb)); }
void addTabSwitchCallback(TabCallback cb) { st().tabCbs.push_back(std::move(cb)); }

std::function<CCNode*()> iconFromFrame(char const* frameName) {
    std::string frame = frameName ? frameName : "GJ_infoIcon_001.png";
    return [frame]() -> CCNode* {
        auto* spr = CCSprite::createWithSpriteFrameName(frame.c_str());
        if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        return spr;
    };
}

EditButtonBar* createEditButtonBar(std::vector<Ref<CCNode>> const& nodes) {
    auto* arr = CCArray::create();
    for (auto const& n : nodes) {
        if (n) arr->addObject(n);
    }
    int rows = GameManager::get()->getIntGameVariable("0050");
    int cols = GameManager::get()->getIntGameVariable("0049");
    clampTabGrid(rows, cols);
    return EditButtonBar::create(arr, {}, -1, false, cols, rows);
}

// Compact panel for View toggles — NEVER EditButtonBar (full-width gray chrome).
// Fixed small box; grows slightly with rows but hard-capped.
CCNode* makeButtonBar(std::vector<Ref<CCNode>> nodes) {
    auto* root = CCNode::create();
    root->setID("paimbnails/compact-tab-panel");
    auto* menu = CCMenu::create();
    // Two columns of short toggles — stays a small floating card.
    constexpr float itemWidth = 54.f;
    constexpr float itemHeight = 24.f;
    constexpr float gap = 4.f;
    size_t const itemCount = std::max<size_t>(nodes.size(), 1);
    size_t const columns = std::clamp<size_t>(itemCount, 1, 3);
    size_t const rows = (itemCount + columns - 1) / columns;
    CCSize const panelSize{
        static_cast<float>(columns) * itemWidth + static_cast<float>(columns - 1) * gap,
        static_cast<float>(rows) * itemHeight + static_cast<float>(rows - 1) * gap
    };
    menu->setContentSize(panelSize);
    menu->setLayout(
        RowLayout::create()
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setGap(gap)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
    );
    for (auto& n : nodes) {
        if (n) menu->addChild(n);
    }
    menu->updateLayout();
    menu->setPosition(panelSize / 2.f);
    root->setContentSize(panelSize);
    root->addChild(menu);
    return root;
}

void hideAllPaimTabContent(EditorUI* ui) {
    for (auto& [_, live] : st().tabs) {
        if (live.content) live.content->setVisible(false);
        if (live.bar) live.bar->setVisible(false);
        // Keep tab openers visible; only clear selected look.
        if (live.toggler && !live.desc.actionOnly) live.toggler->toggle(false);
    }
    st().currentTabId.clear();
    restoreCreateBars(ui);
}

bool switchTab(std::string_view id) {
    auto& s = st();
    auto it = s.tabs.find(std::string(id));
    if (it == s.tabs.end() || !tabActive(it->second)) return false;
    auto ui = s.ui.lock();
    if (!ui) return false;
    auto& live = it->second;

    // Hide other Paimon panels first (never stack).
    for (auto& [oid, other] : s.tabs) {
        if (oid == id) continue;
        if (other.content) other.content->setVisible(false);
        if (other.bar) other.bar->setVisible(false);
        if (other.toggler) other.toggler->toggle(false);
    }

    // --- Action-only (Search / Favs): open overlay, leave canvas alone ---
    if (live.desc.actionOnly || !live.desc.createContent) {
        hideAllPaimTabContent(ui.data());
        restoreCreateBars(ui.data());
        fireActionTab(live);
        return true;
    }

    // Injected EditButtonBar (rare path without Alpha API)
    if (live.buildIndex >= 0 && live.bar) {
        ui->selectBuildTab(live.buildIndex);
        showInjectedBar(ui.data(), live);
        return true;
    }

    // --- Panel tab (View): tiny floating strip, NEVER hide create bars ---
    // Hiding m_createButtonBar left empty gray slabs of toolbar chrome on screen.
    ensurePanelContent(ui.data(), live);
    if (!live.content) {
        // createContent returned null → treat as action
        fireActionTab(live);
        return true;
    }

    bool show = !live.content->isVisible() || s.currentTabId != live.desc.id;
    if (show) {
        live.content->setVisible(true);
        s.currentTabId = live.desc.id;
        if (live.toggler) live.toggler->toggle(true);
        if (live.desc.onToggle) live.desc.onToggle(true, live.content.data());
        if (live.desc.onActivate) live.desc.onActivate();
        fireTabCbs(live.desc.id);
    } else {
        live.content->setVisible(false);
        s.currentTabId.clear();
        if (live.toggler) live.toggler->toggle(false);
        if (live.desc.onToggle) live.desc.onToggle(false, live.content.data());
        restoreCreateBars(ui.data());
    }
    return true;
}

bool switchMode(Mode mode) {
    st().currentMode = mode;
    fireModeCbs(mode);
    return true;
}

void updateTabMenu() {
    auto ui = st().ui.lock();
    if (ui && ui->m_tabsMenu) ui->m_tabsMenu->updateLayout();
    if (st().fallbackMenu) st().fallbackMenu->updateLayout();
}

void flushToEditor(EditorUI* ui) {
    if (!ui) return;
    auto& s = st();
    if (s.flushing) return;
    s.flushing = true;
    s.ui = ui;

    // Alpha EditorTab API owns m_tabsMenu; adding togglers there can crash it.
    // Compact panels never join m_createButtonBars, but toggler inject still
    // touches m_tabsMenu — so with Alpha present we always use docked fallback.
    bool const foreignTabApi = Loader::get()->isModLoaded("alphalaneous.editortab_api");
    // Also require a real tabs menu + create bar before inject (init race).
    bool const canInject = !foreignTabApi && ui->m_tabsMenu && ui->m_createButtonBar;

    int injected = 0, fallback = 0;
    for (auto* live : orderedTabs()) {
        if (!live || live->flushed || !tabActive(*live)) continue;
        if (canInject && tryInjectBuildTab(ui, *live)) ++injected;
        else if (flushFallback(ui, *live)) ++fallback;
    }

    if (injected > 0) s.backend = Backend::Injected;
    else if (fallback > 0) s.backend = Backend::Fallback;

    s.flushing = false;
    // Hide any content panels until user opens a tab.
    hideAllPaimTabContent(ui);
    s.currentTabId.clear();

    log::info(
        "[PaimTabs] flush backend={} injected={} fallback={} total={} foreignApi={}",
        s.backend == Backend::Injected ? "injected"
        : s.backend == Backend::Fallback ? "fallback" : "none",
        injected, fallback, s.tabs.size(), foreignTabApi
    );
    EditorTabsFlushedEvent().send(ui);
}

void setTabsVisible(bool visible) {
    auto& s = st();
    if (s.fallbackMenu) s.fallbackMenu->setVisible(visible);
    for (auto& [_, live] : s.tabs) {
        if (live.toggler) live.toggler->setVisible(visible && tabActive(live));
        if (live.fallbackBtn) live.fallbackBtn->setVisible(visible && tabActive(live));
        if (live.content && !visible && live.buildIndex < 0) live.content->setVisible(false);
    }
}

// Enforce visibility: compact Paimon panels only show in BUILD while selected.
// Never touch m_createButtonBar visibility here (was causing gray slabs).
void syncInjectedBars(EditorUI* ui) {
    if (!ui) return;
    bool inBuild = ui->m_selectedMode == 2;
    auto& s = st();
    for (auto& [id, live] : s.tabs) {
        bool active = false;
        if (live.buildIndex >= 0 && live.bar) {
            active = inBuild && ui->m_selectedTab == live.buildIndex;
            live.bar->setVisible(active);
        } else if (live.content) {
            active = inBuild && s.currentTabId == id;
            live.content->setVisible(active);
            if (!active && s.currentTabId == id) s.currentTabId.clear();
        }
        if (live.toggler && !live.desc.actionOnly) live.toggler->toggle(active);
    }
    if (!inBuild) {
        s.currentTabId.clear();
        for (auto& [_, live] : s.tabs) {
            if (live.content) live.content->setVisible(false);
        }
    }
}

void clearSession() {
    auto& s = st();
    for (auto& [_, live] : s.tabs) {
        live.content = nullptr;
        live.toggler = nullptr;
        live.bar = nullptr;
        live.fallbackBtn = nullptr;
        live.buildIndex = -1;
        live.flushed = false;
    }
    s.fallbackMenu = nullptr;
    s.ui = {};
    s.backend = Backend::None;
    s.currentTabId.clear();
}

} // namespace paimon::editor::tabs

using namespace paimon::editor::tabs;

class $modify(PaimonEditorTabsUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        // Two frames later: create bars + node-ids from other mods are ready,
        // so inject/fallback docking positions are correct.
        Loader::get()->queueInMainThread([self = Ref(this)] {
            Loader::get()->queueInMainThread([self] {
                if (self) flushToEditor(static_cast<EditorUI*>(self.data()));
            });
        });
        return true;
    }

    $override
    void selectBuildTab(int tab) {
        EditorUI::selectBuildTab(tab);
        // Selecting a vanilla create tab clears our current compact tab id.
        auto& s = st();
        bool hitPaim = false;
        for (auto& [id, live] : s.tabs) {
            if (live.buildIndex == tab && live.bar) {
                hitPaim = true;
                s.currentTabId = id;
                fireTabCbs(id);
                if (live.desc.onToggle) live.desc.onToggle(true, live.bar.data());
            }
        }
        if (!hitPaim) {
            // User clicked a normal object tab — hide all Paimon compact panels.
            s.currentTabId.clear();
            for (auto& [_, live] : s.tabs) {
                if (live.content && live.buildIndex == -2) live.content->setVisible(false);
                if (live.toggler && live.buildIndex == -2) live.toggler->toggle(false);
                if (live.desc.onToggle && live.buildIndex == -2) {
                    live.desc.onToggle(false, live.content.data());
                }
            }
            if (m_createButtonBar) m_createButtonBar->setVisible(true);
        }
        syncInjectedBars(this);
    }

    $override
    void toggleMode(CCObject* sender) {
        EditorUI::toggleMode(sender);
        // Leaving build mode must hide every Paimon panel.
        if (m_selectedMode != 2) {
            st().currentTabId.clear();
            hideAllPaimTabContent(this);
        }
        syncInjectedBars(this);
    }

    $override
    void playtestStopped() {
        EditorUI::playtestStopped();
        // GD re-shows every m_uiItems entry; re-assert tab visibility.
        syncInjectedBars(this);
    }

    $override
    void showUI(bool show) {
        EditorUI::showUI(show);
        setTabsVisible(show);
        if (!show) {
            for (auto& [_, live] : st().tabs) {
                if (live.bar) live.bar->setVisible(false);
            }
        } else {
            syncInjectedBars(this);
        }
    }
};

class $modify(PaimonEditorTabsLEL, LevelEditorLayer) {
    struct Fields {
        bool alive = false;
        ~Fields() {
            clearSession();
            paimon::editor::EditorExitEvent().send();
        }
    };

    $override
    bool init(GJGameLevel* level, bool noUI) {
        if (!LevelEditorLayer::init(level, noUI)) return false;
        m_fields->alive = true;
        // Touch Fields so destructor always runs on teardown.
        (void)m_fields->alive;
        return true;
    }
};
