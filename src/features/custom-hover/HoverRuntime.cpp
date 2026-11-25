#include "CustomHover.hpp"
#include "../../core/RuntimeLifecycle.hpp"
#include "../../core/Settings.hpp"
#include "../../core/modules/ModuleRegistry.hpp"
#include "../../utils/SafeNodeHandle.hpp"

#include <Geode/binding/CCScrollLayerExt.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/cocos/layers_scenes_transitions_nodes/CCTransition.h>
#include <Geode/modify/CCTouchDispatcher.hpp>
#include <Geode/ui/SceneEvent.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/Keyboard.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using namespace geode::prelude;

namespace paimon::hover {
namespace {

CCPoint touchPoint;
std::set<int> activeTouches;
std::set<int> swallowedTouches;

bool allowed() {
    return !paimon::isRuntimeShuttingDown() &&
        paimon::modules::isEnabled("paimbnails.customhover.global");
}

bool pointInNodeBounds(CCNode* node, CCPoint const& worldPoint) {
    if (!node) return false;
    auto const size = node->getContentSize();
    if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
        size.width <= 0.f || size.height <= 0.f) {
        return false;
    }
    auto const local = node->convertToNodeSpace(worldPoint);
    if (!std::isfinite(local.x) || !std::isfinite(local.y)) return false;
    return CCRect{0.f, 0.f, size.width, size.height}.containsPoint(local);
}

// Non-mutating scene hit-test: reads z-order into a temporary list instead of calling
// sortAllChildren() during CCScheduler::update().
CCMenuItemSpriteExtra* hit(CCNode* node, CCPoint const& point, int& budget, bool& blocked) {
    if (!node || --budget < 0 || !node->isVisible() || !node->isRunning()) return nullptr;
    if (node->getID() == "custom-hover-popup"_spr ||
        typeinfo_cast<PlayLayer*>(node) || typeinfo_cast<LevelEditorLayer*>(node) ||
        typeinfo_cast<CCLabelBMFont*>(node) || typeinfo_cast<CCParticleSystem*>(node)) {
        return nullptr;
    }

    if (auto* scroll = typeinfo_cast<ScrollLayer*>(node)) {
        if (!pointInNodeBounds(scroll, point)) return nullptr;
    } else if (auto* scrollExt = typeinfo_cast<CCScrollLayerExt*>(node)) {
        if (scrollExt->m_cutContent && !pointInNodeBounds(scrollExt, point)) return nullptr;
    }

    if (auto* menu = typeinfo_cast<CCMenu*>(node); menu && !menu->isTouchEnabled()) {
        return nullptr;
    }

    if (auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
        if (!item->isEnabled() || !item->getNormalImage()) return nullptr;
        return pointInNodeBounds(item, point) ? item : nullptr;
    }

    auto* children = node->getChildren();
    if (!children || children->count() == 0) return nullptr;

    struct ChildOrder {
        CCNode* node;
        int zOrder;
        unsigned int arrival;
    };

    auto const count = children->count();
    std::vector<ChildOrder> ordered;
    ordered.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        if (auto* child = typeinfo_cast<CCNode*>(children->objectAtIndex(i))) {
            ordered.push_back({child, child->getZOrder(), child->getOrderOfArrival()});
        }
    }

    if (ordered.size() > 1) {
        std::sort(ordered.begin(), ordered.end(), [](ChildOrder const& a, ChildOrder const& b) {
            if (a.zOrder != b.zOrder) return a.zOrder < b.zOrder;
            return a.arrival < b.arrival;
        });
    }

    for (size_t i = ordered.size(); i > 0; --i) {
        auto* child = ordered[i - 1].node;
        bool childBlocked = false;
        if (auto* found = hit(child, point, budget, childBlocked)) {
            return found;
        }
        if (childBlocked) {
            blocked = true;
            return nullptr;
        }
        // Visible dialogs block buttons behind them, including their empty backdrop area.
        if (child->isVisible() && typeinfo_cast<FLAlertLayer*>(child)) {
            blocked = true;
            return nullptr;
        }
    }
    return nullptr;
}

CCMenuItemSpriteExtra* under(CCPoint const& point) {
    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) return nullptr;
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return nullptr;

    int budget = 6000;
    bool blocked = false;
    return hit(scene, point, budget, blocked);
}

void undoPoseOnNode(CCNode* node, Pose& applied) {
    if (isIdentityPose(applied)) {
        applied = {};
        return;
    }
    if (node) {
        auto const pos = node->getPosition();
        float const sx = node->getScaleX();
        float const sy = node->getScaleY();
        float const rot = node->getRotation();
        if (std::isfinite(pos.x) && std::isfinite(pos.y) &&
            std::isfinite(applied.x) && std::isfinite(applied.y)) {
            node->setPosition({pos.x - applied.x, pos.y - applied.y});
        }
        if (std::isfinite(sx) && std::isfinite(applied.sx) && applied.sx >= 0.05f) {
            node->setScaleX(sx / applied.sx);
        }
        if (std::isfinite(sy) && std::isfinite(applied.sy) && applied.sy >= 0.05f) {
            node->setScaleY(sy / applied.sy);
        }
        if (std::isfinite(rot) && std::isfinite(applied.rotation)) {
            node->setRotation(rot - applied.rotation);
        }
    }
    applied = {};
}

void applyPoseOnNode(CCNode* node, Pose const& next, Pose& applied) {
    if (!node || !isFinitePose(next)) {
        applied = {};
        return;
    }
    Pose safe = next;
    safe.sx = std::max(0.1f, safe.sx);
    safe.sy = std::max(0.1f, safe.sy);
    if (isIdentityPose(safe)) {
        applied = {};
        return;
    }
    auto const pos = node->getPosition();
    float const sx = node->getScaleX();
    float const sy = node->getScaleY();
    float const rot = node->getRotation();
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) ||
        !std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(rot)) {
        applied = {};
        return;
    }
    node->setPosition({pos.x + safe.x, pos.y + safe.y});
    node->setScaleX(sx * safe.sx);
    node->setScaleY(sy * safe.sy);
    node->setRotation(rot + safe.rotation);
    applied = safe;
}

struct HoverEntry {
    paimon::SafeNodeHandle<CCMenuItemSpriteExtra> item;
    paimon::SafeNodeHandle<CCNode> image;
    std::string key;
    uint64_t configRevision = 0;
    Config config;
    HoverTrackState track;
    Pose applied;

    void undo() {
        undoPoseOnNode(image.get(), applied);
    }

    bool step(float dt, bool on, uint64_t managerRevision) {
        auto* itemPtr = item.get();
        auto* nodePtr = image.get();
        if (!itemPtr || !itemPtr->isRunning() || !nodePtr) {
            undoPoseOnNode(nodePtr, applied);
            return false;
        }

        // If another mod (e.g. ButtonSkins) swapped the normal sprite, release the old one cleanly.
        auto* currentNormal = itemPtr->getNormalImage();
        if (!currentNormal) {
            undoPoseOnNode(nodePtr, applied);
            return false;
        }
        if (currentNormal != nodePtr) {
            undoPoseOnNode(nodePtr, applied);
            image.reset(currentNormal);
            nodePtr = image.get();
            if (!nodePtr) return false;
        }

        if (!geode::cocos::nodeIsVisible(itemPtr) || !itemPtr->isEnabled()) {
            on = false;
        }

        if (configRevision != managerRevision) {
            config = Manager::get().resolve(key);
            configRevision = managerRevision;
        }

        if (!config.enabled) {
            on = false;
        }

        undoPoseOnNode(nodePtr, applied);
        Pose const next = track.step(config, dt, on);
        if (track.isFinishedExit()) {
            return false;
        }
        applyPoseOnNode(nodePtr, next, applied);
        return true;
    }
};

class Ticker final : public CCNode {
public:
    std::vector<HoverEntry> animations;
    paimon::SafeNodeHandle<CCMenuItemSpriteExtra> current;
    float scan = 0.f;
    CCPoint lastMouse{-1e9f, -1e9f};
    CCScene* lastScene = nullptr;

    bool init() override {
        if (!CCNode::init()) return false;
        setID("paimon-hover-ticker"_spr);
        return true;
    }

    void clear() {
        for (auto& animation : animations) {
            animation.undo();
        }
        animations.clear();
        current = nullptr;
        scan = 0.f;
    }

    void update(float dt) override {
        if (!allowed() || paimon::settings::smoothui::reducedMotion()) {
            clear();
            return;
        }

        auto* director = CCDirector::get();
        auto* scene = director ? director->getRunningScene() : nullptr;
        if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) {
            clear();
            lastScene = scene;
            return;
        }
        if (scene != lastScene) {
            clear();
            lastScene = scene;
        }

        dt = std::isfinite(dt) ? std::clamp(dt, 0.f, .05f) : 0.f;
        scan -= dt;
        if (scan <= 0.f) {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
            scan = 1.f / 30.f;
            current = activeTouches.empty() ? nullptr : under(touchPoint);
#else
            CCPoint const mouse = geode::cocos::getMousePos();
            bool const moved = mouse.x != lastMouse.x || mouse.y != lastMouse.y;
            bool const active = moved || !animations.empty() || current.get() != nullptr;
            scan = active ? 1.f / 30.f : 1.f / 6.f;
            current = under(mouse);
            lastMouse = mouse;
#endif
        }

        auto& manager = Manager::get();
        uint64_t const rev = manager.revision();

        auto* target = current.get();
        if (target) {
            if (!target->isRunning() || !target->isEnabled() ||
                !geode::cocos::nodeIsVisible(target)) {
                target = nullptr;
            } else {
                auto const existing = std::find_if(animations.begin(), animations.end(),
                    [&](HoverEntry const& entry) { return entry.item.get() == target; });
                if (existing == animations.end()) {
                    auto key = buttonKey(target);
                    auto config = manager.resolve(key);
                    if (!config.enabled) {
                        target = nullptr;
                    } else if (auto* normal = target->getNormalImage()) {
                        HoverEntry entry;
                        entry.item = target;
                        entry.image = normal;
                        entry.key = std::move(key);
                        entry.configRevision = rev;
                        entry.config = config;
                        animations.push_back(std::move(entry));
                    }
                } else {
                    if (existing->configRevision != rev) {
                        existing->config = manager.resolve(existing->key);
                        existing->configRevision = rev;
                    }
                    if (!existing->config.enabled) {
                        target = nullptr;
                    }
                }
            }
        }

        for (auto it = animations.begin(); it != animations.end();) {
            bool const on = target && it->item.get() == target;
            if (!it->step(dt, on, rev)) {
                it = animations.erase(it);
            } else {
                ++it;
            }
        }
    }
};

Ref<Ticker> ticker;
bool listenersRegistered = false;

} // namespace

void init() {
    if (ticker) return;
    Manager::get().load();

    auto* node = new Ticker();
    if (!node->init()) {
        delete node;
        return;
    }
    ticker = node;
    node->release();

    if (auto* director = CCDirector::get()) {
        if (auto* scheduler = director->getScheduler()) {
            scheduler->scheduleUpdateForTarget(ticker.data(), 0, false);
        }
    }

    if (listenersRegistered) return;
    listenersRegistered = true;

    SceneEvent().listen(+[](CCScene*) -> bool {
        if (ticker) ticker->clear();
        activeTouches.clear();
        swallowedTouches.clear();
        return false;
    }).leak();

    KeyboardInputEvent().listen(+[](KeyboardInputData& data) -> bool {
        if (paimon::isRuntimeShuttingDown() ||
            data.action != KeyboardInputData::Action::Press) {
            return false;
        }
        if (data.key == KEY_Escape && Manager::get().picking) {
            Manager::get().picking = false;
            Notification::create("Seleccion de hover cancelada", NotificationIcon::Info)->show();
            return true;
        }
        if (!allowed() || data.key != KEY_D ||
            !(data.modifiers.value & uint8_t(KeyboardModifier::Control))) {
            return false;
        }
        if (popupOpen()) {
            groupFromPopup();
            return true;
        }
        if (auto* item = under(geode::cocos::getMousePos())) {
            Manager::get().group(Manager::get().resolve(buttonKey(item)));
            Notification::create("Hover: todos vinculados", NotificationIcon::Success)->show();
            return true;
        }
        return false;
    }).leak();
}

$on_game(Exiting) {
    Manager::get().saveIfDirty();
    activeTouches.clear();
    swallowedTouches.clear();
    if (ticker) {
        ticker->clear();
        if (auto* director = CCDirector::get()) {
            if (auto* scheduler = director->getScheduler()) {
                scheduler->unscheduleUpdateForTarget(ticker.data());
            }
        }
        (void)ticker.take();
    }
}

class $modify(PaimonHoverTouch, CCTouchDispatcher) {
    void touchesBegan(CCSet* touches, CCEvent* event) {
        if (!touches) {
            CCTouchDispatcher::touchesBegan(touches, event);
            return;
        }
        auto* pass = CCSet::create();
        auto* director = CCDirector::get();
        auto* keyboard = director ? director->getKeyboardDispatcher() : nullptr;

        for (auto it = touches->begin(); it != touches->end(); ++it) {
            auto* object = *it;
            auto* touch = typeinfo_cast<CCTouch*>(object);
            if (!touch) {
                if (object) pass->addObject(object);
                continue;
            }
            int const id = touch->getID();
            touchPoint = touch->getLocation();
            activeTouches.insert(id);

            bool const edit = Manager::get().picking ||
                (keyboard && keyboard->getControlKeyPressed());
            if (allowed() && edit && !popupOpen()) {
                if (auto* item = under(touchPoint)) {
                    auto key = buttonKey(item);
                    swallowedTouches.insert(id);
                    Manager::get().picking = false;
                    Loader::get()->queueInMainThread([key = std::move(key)] {
                        if (!paimon::isRuntimeShuttingDown()) open(key);
                    });
                    continue;
                }
            }
            pass->addObject(object);
        }
        if (pass->count()) CCTouchDispatcher::touchesBegan(pass, event);
    }

    CCSet* forward(CCSet* touches, bool end) {
        auto* pass = CCSet::create();
        for (auto it = touches->begin(); it != touches->end(); ++it) {
            auto* object = *it;
            auto* touch = typeinfo_cast<CCTouch*>(object);
            if (!touch) {
                if (object) pass->addObject(object);
                continue;
            }
            int const id = touch->getID();
            touchPoint = touch->getLocation();
            if (!swallowedTouches.count(id)) pass->addObject(object);
            if (end) {
                activeTouches.erase(id);
                swallowedTouches.erase(id);
            }
        }
        return pass;
    }

    void touchesMoved(CCSet* touches, CCEvent* event) {
        if (!touches) {
            CCTouchDispatcher::touchesMoved(touches, event);
            return;
        }
        auto* pass = forward(touches, false);
        if (pass->count()) CCTouchDispatcher::touchesMoved(pass, event);
    }

    void touchesEnded(CCSet* touches, CCEvent* event) {
        if (!touches) {
            CCTouchDispatcher::touchesEnded(touches, event);
            return;
        }
        auto* pass = forward(touches, true);
        if (pass->count()) CCTouchDispatcher::touchesEnded(pass, event);
    }

    void touchesCancelled(CCSet* touches, CCEvent* event) {
        if (!touches) {
            CCTouchDispatcher::touchesCancelled(touches, event);
            return;
        }
        auto* pass = forward(touches, true);
        if (pass->count()) CCTouchDispatcher::touchesCancelled(pass, event);
    }
};

} // namespace paimon::hover
