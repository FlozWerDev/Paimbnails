#include "EditorHelpers.hpp"

#include "EditorEvents.hpp"
#include "../backgrounds/services/LayerBackgroundManager.hpp"
#include "../transitions/services/DynamicPanelTransitions.hpp"
#include "../../framework/HookConventions.hpp"
#include "../../utils/EditorContext.hpp"
#include "../../utils/ExtendedKeybind.hpp"
#include "../../utils/ScissorClipNode.hpp"
#if defined(GEODE_IS_DESKTOP)
#include "../smooth-scroll/services/SmoothScrollController.hpp"
#include "../volume-scroll/services/VolumeScrollManager.hpp"
#include <Geode/modify/CCMouseDispatcher.hpp>
#endif

#include <Geode/binding/CCBlockLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJDropDownLayer.hpp>
#include <Geode/modify/CCTextInputNode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

bool isBlockingOverlayNode(CCNode* node) {
    if (!node || !node->isVisible()) return false;
    return typeinfo_cast<FLAlertLayer*>(node) != nullptr
        || typeinfo_cast<CCBlockLayer*>(node) != nullptr
        || typeinfo_cast<GJDropDownLayer*>(node) != nullptr;
}

class ToolbarScrollTicker : public CCNode {
public:
    using StepFn = bool (*)(EditorUI*, float);

    static ToolbarScrollTicker* create(EditorUI* owner, StepFn step) {
        auto* node = new (std::nothrow) ToolbarScrollTicker();
        if (node && node->init()) {
            node->m_owner = owner;
            node->m_step = step;
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    ~ToolbarScrollTicker() override {
        detach();
    }

    void detach() {
        stop();
        m_owner = nullptr;
        m_step = nullptr;
    }

    void onExit() override {
        stop();
        CCNode::onExit();
    }

    void cleanup() override {
        detach();
        CCNode::cleanup();
    }

    void kick() {
        if (!m_owner || !m_step) return;
        if (auto* director = CCDirector::get()) {
            if (auto* scheduler = director->getScheduler()) {
                // Schedule unpaused and explicitly resume so prior pauseTarget calls never stall it.
                scheduler->scheduleUpdateForTarget(this, 0, false);
                scheduler->resumeTarget(this);
            }
        }
    }

    void stop() {
        if (auto* director = CCDirector::get()) {
            if (auto* scheduler = director->getScheduler()) {
                scheduler->unscheduleUpdateForTarget(this);
            }
        }
    }

    void update(float dt) override {
        if (!m_owner || !m_step || !m_step(m_owner, dt)) {
            stop();
        }
    }

private:
    EditorUI* m_owner = nullptr;
    StepFn m_step = nullptr;
};

} // namespace

class $modify(PaimonEditorSuiteUI, EditorUI) {
    struct ToolbarItemEntry {
        WeakRef<CCMenuItem> item;
        bool scaled = false;
        bool disabledByScroll = false;
    };

    struct ToolbarScrollState {
        WeakRef<paimon::ScissorClipNode> clip;
        WeakRef<CCMenu> menu;
        WeakRef<CCLayerColor> track;
        WeakRef<CCLayerColor> thumb;
        WeakRef<ToolbarScrollTicker> ticker;
        std::vector<ToolbarItemEntry> items;
        unsigned int lastMenuChildren = 0;
        CCSize lastWinSize = {0.f, 0.f};
        float viewW = 0.f;
        float viewH = 0.f;
        float cellSize = 27.f;
        float contentH = 0.f;
        float maxScroll = 0.f;
        float scrollOffset = 0.f;
        float targetScroll = 0.f;
        bool active = false;
    };

    struct Fields {
        std::vector<WeakRef<CCNode>> hideableNodes;
        ToolbarScrollState toolbarScroll;

        ~Fields() {
            if (auto ticker = toolbarScroll.ticker.lock()) {
                ticker->detach();
            }
        }
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    static bool stepToolbarScrollThunk(EditorUI* ui, float dt) {
        if (!ui) return false;
        return static_cast<PaimonEditorSuiteUI*>(ui)->stepToolbarScroll(dt);
    }

    void setHideableNodesVisible(bool visible) {
        auto& nodes = m_fields->hideableNodes;
        for (auto it = nodes.begin(); it != nodes.end(); ) {
            if (auto node = it->lock()) {
                node->setVisible(visible);
                ++it;
            } else {
                it = nodes.erase(it);
            }
        }
    }

    void syncHideableWithVanilla() {
        bool const playing = m_editorLayer && m_editorLayer->m_playbackMode != PlaybackMode::Not;
        if (playing) {
            setHideableNodesVisible(false);
            return;
        }
        if (m_swipeBtn) {
            bool visible = !m_uiHidden && m_swipeBtn->isVisible();
            for (auto* p = m_swipeBtn->getParent(); visible && p && p != this; p = p->getParent()) {
                if (!p->isVisible()) visible = false;
            }
            setHideableNodesVisible(visible);
        } else {
            setHideableNodesVisible(!m_uiHidden);
        }
    }

    void repositionToolbarClip() {
        auto& st = m_fields->toolbarScroll;
        if (!st.active) return;
        auto* director = CCDirector::get();
        if (!director) return;
        auto const win = director->getWinSize();
        if (std::abs(win.width - st.lastWinSize.width) < 0.5f
            && std::abs(win.height - st.lastWinSize.height) < 0.5f) {
            return;
        }
        st.lastWinSize = win;
        if (auto clip = st.clip.lock()) {
            clip->setPosition({win.width - st.viewW - 2.f, 3.f});
        }
    }

    void syncToolbarItems(bool force = false) {
        auto& st = m_fields->toolbarScroll;
        if (!st.active) return;
        auto menu = st.menu.lock();
        if (!menu) return;

        unsigned int const childCount = menu->getChildrenCount();
        if (!force && childCount == st.lastMenuChildren && !st.items.empty()) {
            return;
        }
        st.lastMenuChildren = childCount;

        std::vector<ToolbarItemEntry> previous = std::move(st.items);
        st.items.clear();

        auto findPrev = [&](CCMenuItem* ptr) -> ToolbarItemEntry const* {
            for (auto const& e : previous) {
                if (auto locked = e.item.lock(); locked && locked.data() == ptr) {
                    return &e;
                }
            }
            return nullptr;
        };

        if (auto* children = menu->getChildren()) {
            st.items.reserve(children->count());
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* item = typeinfo_cast<CCMenuItem*>(child);
                if (!item) continue;

                ToolbarItemEntry entry;
                entry.item = item;
                if (auto const* prev = findPrev(item)) {
                    entry.scaled = prev->scaled;
                    entry.disabledByScroll = prev->disabledByScroll;
                }

                if (!entry.scaled) {
                    auto const sz = item->getContentSize();
                    float const maxSide = std::max(sz.width, sz.height);
                    if (maxSide > 1.f) {
                        float const s = st.cellSize / maxSide;
                        item->setScale(s);
                        if (auto* extra = typeinfo_cast<CCMenuItemSpriteExtra*>(item)) {
                            extra->m_baseScale = s;
                        }
                    }
                    entry.scaled = true;
                }

                st.items.push_back(std::move(entry));
            }
        }

        constexpr int kCols = 3;
        constexpr float kGapY = 3.f;
        constexpr float kPadY = 4.f;
        int const count = static_cast<int>(st.items.size());
        int const rows = (count + kCols - 1) / kCols;
        st.contentH = kPadY * 2.f + rows * st.cellSize + std::max(0, rows - 1) * kGapY;
        st.maxScroll = std::max(0.f, st.contentH - st.viewH);
        if (!std::isfinite(st.scrollOffset)) st.scrollOffset = 0.f;
        if (!std::isfinite(st.targetScroll)) st.targetScroll = 0.f;
        st.scrollOffset = std::clamp(st.scrollOffset, 0.f, st.maxScroll);
        st.targetScroll = std::clamp(st.targetScroll, 0.f, st.maxScroll);

        bool const scrollable = st.maxScroll > 0.5f;
        if (auto track = st.track.lock()) track->setVisible(scrollable);
        if (auto thumb = st.thumb.lock()) thumb->setVisible(scrollable);
    }

    void enforceClippedButtons() {
        auto& st = m_fields->toolbarScroll;
        if (!st.active) return;

        constexpr int kCols = 3;
        constexpr float kGapY = 3.f;
        constexpr float kPadY = 4.f;

        int idx = 0;
        for (auto& entry : st.items) {
            auto item = entry.item.lock();
            if (!item) continue;
            int const row = idx / kCols;
            float const y = (st.viewH - kPadY - st.cellSize * 0.5f)
                - row * (st.cellSize + kGapY) + st.scrollOffset;
            bool const inView = (y + st.cellSize * 0.45f >= 0.f)
                && (y - st.cellSize * 0.45f <= st.viewH);
            if (!inView) {
                if (item->isEnabled()) {
                    entry.disabledByScroll = true;
                    item->setEnabled(false);
                }
            } else if (entry.disabledByScroll) {
                entry.disabledByScroll = false;
                item->setEnabled(true);
            }
            ++idx;
        }
    }

    void applyToolbarScroll() {
        auto& st = m_fields->toolbarScroll;
        if (!st.active) return;

        repositionToolbarClip();
        syncToolbarItems(false);

        constexpr int kCols = 3;
        constexpr float kGapX = 3.f;
        constexpr float kGapY = 3.f;
        constexpr float kPadX = 3.f;
        constexpr float kPadY = 4.f;

        if (!std::isfinite(st.scrollOffset)) st.scrollOffset = 0.f;
        st.scrollOffset = std::clamp(st.scrollOffset, 0.f, st.maxScroll);

        int idx = 0;
        for (auto& entry : st.items) {
            auto item = entry.item.lock();
            if (!item) continue;
            int const col = idx % kCols;
            int const row = idx / kCols;
            float const x = kPadX + col * (st.cellSize + kGapX) + st.cellSize * 0.5f;
            float const y = (st.viewH - kPadY - st.cellSize * 0.5f)
                - row * (st.cellSize + kGapY) + st.scrollOffset;
            item->setPosition({x, y});

            // Prevent scrolled-out buttons from intercepting clicks outside the bar
            // without clobbering buttons disabled by vanilla EditorUI::updateButtons.
            bool const inView = (y + st.cellSize * 0.45f >= 0.f)
                && (y - st.cellSize * 0.45f <= st.viewH);
            if (!inView) {
                if (item->isEnabled()) {
                    entry.disabledByScroll = true;
                    item->setEnabled(false);
                }
            } else if (entry.disabledByScroll) {
                entry.disabledByScroll = false;
                item->setEnabled(true);
            }
            ++idx;
        }

        if (auto thumb = st.thumb.lock()) {
            float const trackH = std::max(8.f, st.viewH - 8.f);
            float const ratio = st.contentH > 0.f
                ? std::clamp(st.viewH / st.contentH, 0.18f, 1.f)
                : 1.f;
            float const thumbH = std::max(14.f, trackH * ratio);
            float const travel = std::max(0.f, trackH - thumbH);
            float const frac = st.maxScroll > 0.001f ? (st.scrollOffset / st.maxScroll) : 0.f;
            float const thumbY = 4.f + travel * (1.f - frac);
            thumb->setContentSize({thumb->getContentSize().width, thumbH});
            thumb->setPositionY(thumbY);
        }
    }

    void setupScrollableToolbar() {
#if !defined(GEODE_IS_DESKTOP)
        // Mouse-wheel scrolling is desktop-only; clipping/disabling overflow buttons on mobile makes them unreachable.
        return;
#endif
        auto& st = m_fields->toolbarScroll;
        if (st.active) {
            syncToolbarItems(true);
            applyToolbarScroll();
            return;
        }

        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("toolbar-toggles-menu"));
        if (!menu && m_swipeBtn) {
            menu = typeinfo_cast<CCMenu*>(m_swipeBtn->getParent());
        }
        if (!menu || !menu->getChildren()) return;

        int itemCount = 0;
        for (auto* child : CCArrayExt<CCNode*>(menu->getChildren())) {
            if (typeinfo_cast<CCMenuItem*>(child)) {
                ++itemCount;
            }
        }
        // 2 rows of 3 fit comfortably without scrolling.
        if (itemCount <= 6) return;

        float btnSize = 27.f;
        if (m_swipeBtn) {
            float const h = m_swipeBtn->getContentSize().height * std::abs(m_swipeBtn->getScaleY());
            if (h > 14.f && h < 42.f) btnSize = h;
        }
        btnSize = std::clamp(btnSize, 24.f, 28.f);

        constexpr int kCols = 3;
        constexpr float kGapX = 3.f;
        constexpr float kPadX = 3.f;
        constexpr float kBarW = 3.5f;

        float const gridW = kCols * btnSize + (kCols - 1) * kGapX;
        float const viewW = gridW + kPadX * 2.f + kBarW + 3.f;
        float const barH = (m_toolbarHeight > 60.f && m_toolbarHeight < 130.f)
            ? m_toolbarHeight : 90.f;
        float const viewH = std::clamp(barH - 6.f, 76.f, 86.f);

        auto const win = CCDirector::get()->getWinSize();
        float const viewX = win.width - viewW - 2.f;
        float const viewY = 3.f;

        auto* stencil = CCLayerColor::create({255, 255, 255, 255}, viewW, viewH);
        auto* clip = paimon::ScissorClipNode::create(stencil);
        if (!clip) return;
        clip->setID("paimon-toolbar-scroll-clip"_spr);
        clip->ignoreAnchorPointForPosition(false);
        clip->setAnchorPoint({0.f, 0.f});
        clip->setContentSize({viewW, viewH});
        clip->setPosition({viewX, viewY});
        this->addChild(clip, menu->getZOrder());

        Ref<CCMenu> keepMenu = menu;
        menu->removeFromParentAndCleanup(false);
        menu->setLayout(nullptr);
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({0.f, 0.f});
        menu->setContentSize({viewW, viewH});
        menu->setPosition({0.f, 0.f});
        clip->addChild(menu, 1);

        auto* track = CCLayerColor::create({255, 255, 255, 34}, kBarW, std::max(8.f, viewH - 8.f));
        track->ignoreAnchorPointForPosition(false);
        track->setAnchorPoint({0.f, 0.f});
        track->setPosition({viewW - kBarW - 1.5f, 4.f});
        clip->addChild(track, 5);

        auto* thumb = CCLayerColor::create({255, 255, 255, 150}, kBarW, 20.f);
        thumb->ignoreAnchorPointForPosition(false);
        thumb->setAnchorPoint({0.f, 0.f});
        thumb->setPosition({viewW - kBarW - 1.5f, 4.f});
        clip->addChild(thumb, 6);

        auto* ticker = ToolbarScrollTicker::create(this, &PaimonEditorSuiteUI::stepToolbarScrollThunk);
        if (ticker) {
            clip->addChild(ticker);
        }

        st.clip = clip;
        st.menu = menu;
        st.track = track;
        st.thumb = thumb;
        st.ticker = ticker;
        st.lastWinSize = win;
        st.lastMenuChildren = 0;
        st.viewW = viewW;
        st.viewH = viewH;
        st.cellSize = btnSize;
        st.scrollOffset = 0.f;
        st.targetScroll = 0.f;
        st.active = true;

        paimon::editor::registerHideableUI(this, clip);
        syncToolbarItems(true);
        applyToolbarScroll();
    }

    void onToolbarChildAdded() {
        auto& st = m_fields->toolbarScroll;
        if (st.active) {
            syncToolbarItems(true);
            applyToolbarScroll();
        } else {
            setupScrollableToolbar();
        }
    }

    bool stepToolbarScroll(float dt) {
        auto& st = m_fields->toolbarScroll;
        if (!st.active) return false;
        if (!std::isfinite(dt) || dt < 0.f) dt = 0.f;
        if (!std::isfinite(st.scrollOffset)) st.scrollOffset = 0.f;
        if (!std::isfinite(st.targetScroll)) st.targetScroll = st.scrollOffset;
        st.targetScroll = std::clamp(st.targetScroll, 0.f, st.maxScroll);

        float const clampedDt = std::clamp(dt, 0.f, 0.05f);
        float const alpha = 1.f - std::exp(-16.f * clampedDt);
        st.scrollOffset += (st.targetScroll - st.scrollOffset) * alpha;
        bool keepRunning = true;
        if (std::abs(st.targetScroll - st.scrollOffset) < 0.08f) {
            st.scrollOffset = st.targetScroll;
            keepRunning = false;
        }
        applyToolbarScroll();
        return keepRunning;
    }

    $override
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        WeakRef<EditorUI> weakUI = this;
        Loader::get()->queueInMainThread([weakUI] {
            if (auto ui = weakUI.lock()) {
                static_cast<PaimonEditorSuiteUI*>(ui.data())->setupScrollableToolbar();
            }
        });
        return true;
    }

    bool handleToolbarScrollWheel(float y, float x) {
        if (!std::isfinite(y)) y = 0.f;
        if (!std::isfinite(x)) x = 0.f;
        if (std::abs(y) < 0.0001f && std::abs(x) < 0.0001f) return false;

#if defined(GEODE_IS_DESKTOP)
        if (paimon::keybinds::hasScrollCaptor() || paimon::volscroll::isVolumeGestureActive()) {
            return false;
        }
        auto& smoothCtrl = paimon::smoothscroll::SmoothScrollController::get();
        if (smoothCtrl.isReplaying()) {
            return false;
        }
#endif

        auto& st = m_fields->toolbarScroll;
        if (!st.active) {
            setupScrollableToolbar();
            if (!st.active) return false;
        }

        repositionToolbarClip();
        syncToolbarItems(false);
        if (st.maxScroll <= 0.5f) return false;

        auto clip = st.clip.lock();
        bool const uiShown = !m_uiHidden && this->isVisible()
            && (!m_swipeBtn || m_swipeBtn->isVisible());
        if (!clip || !clip->isVisible() || !uiShown) return false;
        if (paimon::editor::hasBlockingModal(m_editorLayer, this)) return false;

        auto const local = clip->convertToNodeSpace(geode::cocos::getMousePos());
        if (local.x < -4.f || local.y < -4.f
            || local.x > st.viewW + 4.f || local.y > st.viewH + 4.f) {
            return false;
        }

#if defined(GEODE_IS_DESKTOP)
        smoothCtrl.stop();
#endif
        float const raw = std::abs(y) > 0.001f ? y : x;
        float const delta = (std::abs(raw) > 2.f) ? (raw * 2.4f) : (raw * 28.f);
        if (!std::isfinite(st.targetScroll)) st.targetScroll = 0.f;
        st.targetScroll = std::clamp(st.targetScroll + delta, 0.f, st.maxScroll);
        if (auto ticker = st.ticker.lock()) {
            ticker->kick();
        } else {
            st.scrollOffset = st.targetScroll;
            applyToolbarScroll();
        }
        return true;
    }

    $override
    void showUI(bool show) {
        EditorUI::showUI(show);
        bool const actualVisible = m_swipeBtn ? m_swipeBtn->isVisible() : (!m_uiHidden && show);
        setHideableNodesVisible(actualVisible);
        paimon::editor::EditorUIShowEvent().send(this, actualVisible);
    }

    $override
    void updateButtons() {
        EditorUI::updateButtons();
        enforceClippedButtons();
    }

    $override
    void onPlaytest(CCObject* sender) {
        EditorUI::onPlaytest(sender);
        syncHideableWithVanilla();
    }

    $override
    void playtestStopped() {
        EditorUI::playtestStopped();
        syncHideableWithVanilla();
    }
};

#if defined(GEODE_IS_DESKTOP)
// Intercept hover scroll over the bottom-right editor toolbar at CCMouseDispatcher level
// ahead of InputScroll, VolumeScroll, SmoothScroll, and EditorUI panning/zooming.
class $modify(PaimonEditorToolbarScrollDispatcher, CCMouseDispatcher) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre(
            "cocos2d::CCMouseDispatcher::dispatchScrollMSG",
            geode::Priority::FirstPre
        );
    }

    bool dispatchScrollMSG(float y, float x) {
        if (auto* lel = LevelEditorLayer::get()) {
            if (auto* ui = lel->m_editorUI) {
                if (static_cast<PaimonEditorSuiteUI*>(ui)->handleToolbarScrollWheel(y, x)) {
                    return true;
                }
            }
        }
        return CCMouseDispatcher::dispatchScrollMSG(y, x);
    }
};
#endif

// Evict stale parked background videos off-thread and bypass CCNode panel checks while
// LevelEditorLayer::init builds thousands of level objects and EditorUI widgets.
class $modify(PaimonEditorInitFastPath, LevelEditorLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("LevelEditorLayer::init", geode::Priority::VeryEarly);
    }

    $override
    bool init(GJGameLevel* level, bool noUI) {
        if (!noUI) {
            LayerBackgroundManager::get().forceEvictAllStaleVideos();
        }
        paimon::transitions::dynamic::FastBypassGuard bypassGuard;
        return LevelEditorLayer::init(level, noUI);
    }
};

namespace paimon::editor {

namespace {
WeakRef<CCTextInputNode> g_focusedInput;
}

bool hasBlockingModal(LevelEditorLayer* lel, EditorUI* ui) {
    if (ui && ui->m_isPaused) return true;
    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    auto checkContainer = [](CCNode* container) -> bool {
        if (!container || !container->getChildren()) return false;
        for (auto* child : CCArrayExt<CCNode*>(container->getChildren())) {
            if (isBlockingOverlayNode(child)) return true;
        }
        return false;
    };
    if (checkContainer(scene)) return true;
    if (checkContainer(lel)) return true;
    if (checkContainer(ui)) return true;
    return false;
}

CCMenu* hostToolbarMenu(EditorUI* ui) {
    if (!ui) return nullptr;
    auto* modUI = static_cast<PaimonEditorSuiteUI*>(ui);
    if (auto menu = modUI->m_fields->toolbarScroll.menu.lock()) {
        return menu.data();
    }
    if (auto* menu = typeinfo_cast<CCMenu*>(ui->getChildByID("toolbar-toggles-menu"))) return menu;
    if (ui->m_swipeBtn) {
        if (auto* menu = typeinfo_cast<CCMenu*>(ui->m_swipeBtn->getParent())) return menu;
    }
    for (auto const* id : {"editor-buttons-menu", "undo-menu"}) {
        if (auto* menu = typeinfo_cast<CCMenu*>(ui->getChildByID(id))) return menu;
    }
    if (ui->m_undoBtn) {
        if (auto* menu = typeinfo_cast<CCMenu*>(ui->m_undoBtn->getParent())) return menu;
    }
    return nullptr;
}

float toolbarToggleSize(EditorUI* ui) {
    if (!ui) return 0.f;
    for (auto* btn : {ui->m_swipeBtn, ui->m_freeMoveBtn, ui->m_snapBtn}) {
        if (btn) return btn->getContentSize().height;
    }
    return 0.f;
}

void registerHideableUI(EditorUI* ui, CCNode* node) {
    if (!ui || !node) return;
    if (ui->m_uiItems && !ui->m_uiItems->containsObject(node)) {
        ui->m_uiItems->addObject(node);
    }
    auto* modUI = static_cast<PaimonEditorSuiteUI*>(ui);
    auto& nodes = modUI->m_fields->hideableNodes;
    bool exists = false;
    for (auto it = nodes.begin(); it != nodes.end(); ) {
        auto existing = it->lock();
        if (!existing) {
            it = nodes.erase(it);
            continue;
        }
        if (existing.data() == node) {
            exists = true;
        }
        ++it;
    }
    if (!exists) {
        nodes.emplace_back(node);
    }
    if (ui->m_swipeBtn && !ui->m_swipeBtn->isVisible()) {
        node->setVisible(false);
    }
}

void addToolbarButton(
    EditorUI* ui,
    CCMenuItem* button,
    std::string const& fallbackMenuID,
    CCPoint fallbackPosition
) {
    if (!ui || !button) return;
    if (auto* menu = hostToolbarMenu(ui)) {
        menu->addChild(button);
        if (menu->getLayout()) menu->updateLayout();
        registerHideableUI(ui, button);
        auto* modUI = static_cast<PaimonEditorSuiteUI*>(ui);
        auto scrollMenu = modUI->m_fields->toolbarScroll.menu.lock();
        if (scrollMenu && scrollMenu.data() == menu) {
            modUI->onToolbarChildAdded();
        }
    } else {
        auto* fallback = CCMenu::create();
        if (!fallbackMenuID.empty()) fallback->setID(fallbackMenuID);
        fallback->setPosition(fallbackPosition);
        fallback->addChild(button);
        ui->addChild(fallback, 100);
        registerHideableUI(ui, fallback);
    }
}

void focusCameraOnPoint(LevelEditorLayer* lel, CCPoint objectSpace) {
    if (!lel || !lel->m_objectLayer) return;
    auto* layer = lel->m_objectLayer;
    auto win = CCDirector::get()->getWinSize();
    auto world = layer->convertToWorldSpace(objectSpace);
    auto center = win / 2.f;
    layer->setPosition(layer->getPosition() + center - world);
}

void setFocusedTextInput(CCTextInputNode* node) {
    g_focusedInput = node ? WeakRef<CCTextInputNode>(node) : WeakRef<CCTextInputNode>();
}

Ref<CCTextInputNode> focusedTextInput() {
    return g_focusedInput.lock();
}

} // namespace paimon::editor

// only the editor asks for the focused input, so don't track anywhere else.
class $modify(PaimonFocusedInputNode, CCTextInputNode) {
    $override
    bool onTextFieldAttachWithIME(CCTextFieldTTF* t) {
        auto r = CCTextInputNode::onTextFieldAttachWithIME(t);
        if (r && paimon::isEditorScene()) paimon::editor::setFocusedTextInput(this);
        return r;
    }

    $override
    bool onTextFieldDetachWithIME(CCTextFieldTTF* t) {
        if (paimon::editor::focusedTextInput() == this) {
            paimon::editor::setFocusedTextInput(nullptr);
        }
        return CCTextInputNode::onTextFieldDetachWithIME(t);
    }
};
