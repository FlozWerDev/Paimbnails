#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/CCTouchDispatcher.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

using namespace geode::prelude;

// Z-order based touch routing, reworked from alk.better-touch-prio (Unlicense).
// GD picks the touched node by touch-priority value, which lets you press
// buttons behind unclosable popups. This replaces CCTouchDispatcher::touches so
// the front-most node (by Z order + order of arrival) wins instead.
//
// Improvements over the original:
//   - runtime toggle (touch-prio-enable) instead of always-on
//   - cedes automatically if the standalone alk.better-touch-prio is installed,
//     so two Replace hooks never fight over the same function
//   - honors both our "steals-touch" node flag and alk's, so nodes already
//     tagged for the standalone mod keep working here

namespace {
    bool s_enabled = false;
    bool s_ceded   = false; // standalone mod present -> let it own the dispatch
    bool s_debug   = false;

    bool active() { return s_enabled && !s_ceded; }

    void refreshCeded() {
        s_ceded = Loader::get()->isModLoaded("alk.better-touch-prio");
    }
}

// RTTI shim: matches the editor tab scroller from alphalaneous.editortab_api by
// mangled type name so typeinfo_cast can recognize it without a hard dependency.
namespace tabcore::layout {
    class Scroller : public cocos2d::CCNode {};
}

$on_mod(Loaded) {
    auto* mod = Mod::get();
    refreshCeded();
    s_enabled = mod->getSettingValue<bool>("touch-prio-enable");
    s_debug   = mod->getSettingValue<bool>("touch-prio-debug-logs");

    listenForSettingChanges<bool>("touch-prio-enable", [](bool v) { s_enabled = v; });
    listenForSettingChanges<bool>("touch-prio-debug-logs", [](bool v) { s_debug = v; });

    if (s_ceded) {
        log::info("[TouchPrio] alk.better-touch-prio is loaded; ceding touch dispatch to it.");
    }
}

struct PaimonTouchDispatcher : Modify<PaimonTouchDispatcher, CCTouchDispatcher> {
    static void onModify(auto& self) {
        (void)self.setHookPriority("cocos2d::CCTouchDispatcher::touches", Priority::Replace);
    }

    template <class Handler>
    struct ParentPath {
        std::vector<CCNode*> path;
        Handler* handler = nullptr;
        mutable bool hasInvalidRoot = false;

        ParentPath(Handler* handler) : handler(handler) {}
        ParentPath(ParentPath&&) = default;
        ParentPath& operator=(ParentPath&&) = default;

        ParentPath(CCNode* node, Handler* handler) : handler(handler) {
            while (node) {
                path.push_back(node);
                node = node->getParent();
            }
        }

        static std::optional<ParentPath> filtered(CCNode* node, Handler* handler, CCNode* filter) {
            ParentPath ret{handler};
            bool confirmed = false;
            while (node) {
                ret.path.push_back(node);
                if (node == filter) confirmed = true;
                node = node->getParent();
            }
            if (confirmed) return ret;
            return std::nullopt;
        }

        CCNode* leaf() const { return path.empty() ? nullptr : path.front(); }
        CCNode* root() const { return path.empty() ? nullptr : path.back(); }
        CCNode* nth(size_t n) const {
            return (n < path.size()) ? path[path.size() - 1 - n] : nullptr;
        }

        bool swallows() const {
            if constexpr (std::is_same_v<Handler, CCTargetedTouchHandler>) {
                return handler->m_bSwallowsTouches;
            }
            return false;
        }

        static bool isStealer(CCNode* node) {
            if (node->getUserFlag("steals-touch"_spr)) return true;
            if (node->getUserFlag("alk.better-touch-prio/steals-touch")) return true;

            if (typeinfo_cast<TableView*>(node)) return true;
            if (typeinfo_cast<BoomScrollLayer*>(node)) return true;
            if (typeinfo_cast<tabcore::layout::Scroller*>(node)) return true;
            if (auto scroll = typeinfo_cast<ScrollLayer*>(node)) {
                return scroll->isStealingTouches();
            }
            return false;
        }

        bool steals() const {
            if (this->swallows()) return false;
            if constexpr (std::is_same_v<Handler, CCTargetedTouchHandler>) {
                if (auto l = this->leaf()) return isStealer(l);
            }
            return false;
        }

        bool hasValidRoot() {
            if (this->hasInvalidRoot) return false;
            auto* director = CCDirector::get();
            std::array<CCNode*, 2> order{director->m_pRunningScene, director->m_pNotificationNode};
            if (std::find(order.begin(), order.end(), this->root()) == order.end()) {
                this->hasInvalidRoot = true;
                return false;
            }
            return true;
        }

        bool compareRoots(ParentPath const& other) const {
            if (this->root() == other.root()) return false;
            auto* director = CCDirector::get();
            std::array<CCNode*, 2> order{director->m_pRunningScene, director->m_pNotificationNode};
            auto thisIt  = std::find(order.begin(), order.end(), this->root());
            auto otherIt = std::find(order.begin(), order.end(), other.root());
            if (thisIt == order.end())  { this->hasInvalidRoot = true;  return false; }
            if (otherIt == order.end()) { other.hasInvalidRoot = true; return false; }
            // notification node sits above the running scene, so it comes first
            return thisIt > otherIt;
        }

        bool operator<(ParentPath const& other) const {
            if (this->root() != other.root()) return this->compareRoots(other);

            size_t maxLen = std::max(path.size(), other.path.size());
            for (size_t i = 1; i < maxLen; ++i) {
                auto* a = this->nth(i);
                auto* b = other.nth(i);

                // A parent and its own child both respond to touch. Mirroring how
                // GD scroll layers behave: a non-stealing parent lets the child go
                // first; a stealing parent takes it.
                if (!a && b) return this->steals();
                if (!b && a) return !other.steals();

                if (a != b) {
                    if (a->getZOrder() == b->getZOrder()) {
                        return a->getOrderOfArrival() > b->getOrderOfArrival();
                    }
                    return a->getZOrder() > b->getZOrder();
                }
            }
            return false;
        }
    };

    template <class Handler>
    std::vector<ParentPath<Handler>> getRegisteredPaths(
        CCArray* handlers, std::optional<CCNode*> filter,
        std::vector<ParentPath<Handler>>& invalidRoots
    ) const {
        std::vector<ParentPath<Handler>> paths;
        for (auto handler : CCArrayExt<Handler*>(handlers)) {
            if (!handler) continue;
            auto* delegate = handler->getDelegate();
            if (!delegate) continue;
            auto* node = typeinfo_cast<CCNode*>(delegate);
            if (!node) continue;

            if (filter) {
                auto f = ParentPath<Handler>::filtered(node, handler, *filter);
                if (!f) continue;
                if (f->hasValidRoot()) paths.push_back(std::move(*f));
                else invalidRoots.push_back(std::move(*f));
            } else {
                ParentPath<Handler> p(node, handler);
                if (p.hasValidRoot()) paths.push_back(std::move(p));
                else invalidRoots.push_back(std::move(p));
            }
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }

    template <class Handler>
    void logInvalidRoots(std::vector<ParentPath<Handler>> const& invalidRoots) {
        if (!s_debug) return;
        for (auto& p : invalidRoots) {
            log::warn("[TouchPrio] Handler {} has an invalid root (leaked node?).", p.leaf());
        }
    }

    template <class Handler>
    bool handleSingleTargetedHandlers(
        CCSet* touches, CCTouch* touch, CCEvent* event, unsigned int index,
        std::vector<ParentPath<Handler>> const& registeredPaths
    ) {
        bool touchClaimed = false;
        for (auto& path : registeredPaths) {
            if (path.hasInvalidRoot) continue;

            auto* delegate = path.handler->getDelegate();
            auto* claimedTouches = path.handler->m_pClaimedTouches;
            bool swallows = path.handler->m_bSwallowsTouches;

            bool claimed = false;
            if (index == CCTOUCHBEGAN) {
                claimed = delegate->ccTouchBegan(touch, event);
                if (claimed) {
                    if (s_debug) log::debug("[TouchPrio] {} claimed touch", path.leaf());
                    claimedTouches->addObject(touch);
                }
            } else if (claimedTouches->containsObject(touch)) {
                claimed = true;
                switch (m_sHandlerHelperData[index].m_type) {
                    case CCTOUCHMOVED: delegate->ccTouchMoved(touch, event); break;
                    case CCTOUCHENDED:
                        delegate->ccTouchEnded(touch, event);
                        claimedTouches->removeObject(touch);
                        break;
                    case CCTOUCHCANCELLED:
                        delegate->ccTouchCancelled(touch, event);
                        claimedTouches->removeObject(touch);
                        break;
                }
            }

            if (claimed && swallows) {
                touchClaimed = true;
                if (touches) touches->removeObject(touch);
                break;
            }
        }
        return touchClaimed;
    }

    void handleTargetedHandlers(
        CCSet* touches, CCEvent* event, unsigned int index,
        std::optional<CCNode*> filter = std::nullopt
    ) {
        std::vector<ParentPath<CCTargetedTouchHandler>> invalidRoots;
        auto paths = this->getRegisteredPaths<CCTargetedTouchHandler>(m_pTargetedHandlers, filter, invalidRoots);
        if (index == CCTOUCHBEGAN) logInvalidRoots(invalidRoots);

        std::vector<CCTouch*> copy;
        for (auto touch : *touches) copy.push_back(static_cast<CCTouch*>(touch));
        for (auto touch : copy) {
            this->handleSingleTargetedHandlers(touches, touch, event, index, paths);
        }
    }

    void handleStandardHandlers(CCSet* touches, CCEvent* event, unsigned int index) {
        std::vector<ParentPath<CCStandardTouchHandler>> invalidRoots;
        auto paths = this->getRegisteredPaths<CCStandardTouchHandler>(m_pStandardHandlers, std::nullopt, invalidRoots);
        if (index == CCTOUCHBEGAN) logInvalidRoots(invalidRoots);

        for (auto& path : paths) {
            auto* delegate = path.handler->getDelegate();
            switch (m_sHandlerHelperData[index].m_type) {
                case CCTOUCHBEGAN:     delegate->ccTouchesBegan(touches, event); break;
                case CCTOUCHMOVED:     delegate->ccTouchesMoved(touches, event); break;
                case CCTOUCHENDED:     delegate->ccTouchesEnded(touches, event); break;
                case CCTOUCHCANCELLED: delegate->ccTouchesCancelled(touches, event); break;
            }
        }
    }

    bool handleSingleTargetedHandlersWithFilter(
        CCTouch* touch, CCEvent* event, unsigned int index, CCNode* filter
    ) {
        std::vector<ParentPath<CCTargetedTouchHandler>> invalidRoots;
        auto paths = this->getRegisteredPaths<CCTargetedTouchHandler>(m_pTargetedHandlers, filter, invalidRoots);
        if (index == CCTOUCHBEGAN) logInvalidRoots(invalidRoots);
        return this->handleSingleTargetedHandlers(nullptr, touch, event, index, paths);
    }

    $override
    void touches(CCSet* touches, CCEvent* event, unsigned int index) {
        if (!active()) {
            CCTouchDispatcher::touches(touches, event, index);
            return;
        }

        m_bLocked = true;

        if (m_pTargetedHandlers->count() > 0) {
            this->handleTargetedHandlers(touches, event, index);
        }
        if (m_pStandardHandlers->count() > 0 && touches->m_pSet->size() > 0) {
            this->handleStandardHandlers(touches, event, index);
        }

        m_bLocked = false;

        if (m_bToRemove) {
            m_bToRemove = false;
            for (unsigned int i = 0; i < m_pHandlersToRemove->num; ++i) {
                for (auto handler : CCArrayExt<CCTargetedTouchHandler*>(m_pTargetedHandlers)) {
                    if (handler->getDelegate() == m_pHandlersToRemove->arr[i]) {
                        m_pTargetedHandlers->removeObject(handler);
                        break;
                    }
                }
                for (auto handler : CCArrayExt<CCStandardTouchHandler*>(m_pStandardHandlers)) {
                    if (handler->getDelegate() == m_pHandlersToRemove->arr[i]) {
                        m_pStandardHandlers->removeObject(handler);
                        break;
                    }
                }
            }
            m_pHandlersToRemove->num = 0;
        }

        if (m_bToAdd) {
            m_bToAdd = false;
            for (auto handler : CCArrayExt<CCTouchHandler*>(m_pHandlersToAdd)) {
                if (!handler) continue;
                if (typeinfo_cast<CCTargetedTouchHandler*>(handler)) {
                    if (!m_pTargetedHandlers->containsObject(handler)) {
                        m_pTargetedHandlers->addObject(handler);
                    }
                } else if (!m_pStandardHandlers->containsObject(handler)) {
                    m_pStandardHandlers->addObject(handler);
                }
            }
            m_pHandlersToAdd->removeAllObjects();
        }

        if (m_bToQuit) {
            m_bToQuit = false;
            m_pStandardHandlers->removeAllObjects();
            m_pTargetedHandlers->removeAllObjects();
        }
    }
};

// RobTop leaves the editor UI layer under the object layers; bump it up so the
// z-order routing above reaches editor buttons correctly.
struct PaimonEditorGamePrio : Modify<PaimonEditorGamePrio, GJBaseGameLayer> {
    $override
    bool init() {
        if (!GJBaseGameLayer::init()) return false;
        if (active() && m_uiLayer) m_uiLayer->setZOrder(2);
        return true;
    }
};

// Route touches through the editor's object layers first so objects behind the
// UI can still be grabbed, matching how the dispatch above orders things.
struct PaimonEditorUIPrio : Modify<PaimonEditorUIPrio, EditorUI> {
    struct Fields {
        bool m_inObjectsLayer = false;
    };

    bool dispatchToLayers(CCTouch* touch, CCEvent* event, int type) {
        auto* dispatcher = static_cast<PaimonTouchDispatcher*>(CCTouchDispatcher::get());
        for (auto* layer : {m_editorLayer->m_inShaderObjectLayer,
                            m_editorLayer->m_aboveShaderObjectLayer,
                            m_editorLayer->m_objectLayer}) {
            if (dispatcher->handleSingleTargetedHandlersWithFilter(touch, event, type, layer)) {
                return true;
            }
        }
        return false;
    }

    $override
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        if (active() && dispatchToLayers(touch, event, CCTOUCHBEGAN)) {
            m_fields->m_inObjectsLayer = true;
            return true;
        }
        return EditorUI::ccTouchBegan(touch, event);
    }

    $override
    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        if (active() && m_fields->m_inObjectsLayer) {
            dispatchToLayers(touch, event, CCTOUCHMOVED);
            return;
        }
        EditorUI::ccTouchMoved(touch, event);
    }

    $override
    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        if (active() && m_fields->m_inObjectsLayer) {
            dispatchToLayers(touch, event, CCTOUCHENDED);
            m_fields->m_inObjectsLayer = false;
            return;
        }
        EditorUI::ccTouchEnded(touch, event);
    }

    $override
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        if (active() && m_fields->m_inObjectsLayer) {
            dispatchToLayers(touch, event, CCTOUCHCANCELLED);
            m_fields->m_inObjectsLayer = false;
            return;
        }
        EditorUI::ccTouchCancelled(touch, event);
    }
};
