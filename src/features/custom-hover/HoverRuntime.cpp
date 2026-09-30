#include "CustomHover.hpp"
#include "../../core/RuntimeLifecycle.hpp"
#include "../../core/Settings.hpp"
#include "../../core/modules/ModuleRegistry.hpp"

#include <Geode/modify/CCTouchDispatcher.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

#include <algorithm>
#include <set>

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

CCMenuItemSpriteExtra* hit(CCNode* node, CCPoint point, int& budget, bool& blocked) {
    if (!node || --budget < 0 || !node->isVisible()) return nullptr;
    if (node->getID() == "custom-hover-popup"_spr ||
        typeinfo_cast<PlayLayer*>(node) || typeinfo_cast<LevelEditorLayer*>(node)) {
        return nullptr;
    }
    if (auto* scroll = typeinfo_cast<ScrollLayer*>(node)) {
        auto bounds = CCRect{0, 0, scroll->getContentWidth(), scroll->getContentHeight()};
        if (!bounds.containsPoint(scroll->convertToNodeSpace(point))) return nullptr;
    }
    if (auto* menu = typeinfo_cast<CCMenu*>(node); menu && !menu->isTouchEnabled()) {
        return nullptr;
    }
    if (auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
        auto bounds = CCRect{0, 0, item->getContentWidth(), item->getContentHeight()};
        return item->isEnabled() && item->isRunning() &&
            bounds.containsPoint(item->convertToNodeSpace(point)) ? item : nullptr;
    }

    node->sortAllChildren();
    auto* children = node->getChildren();
    if (!children) return nullptr;
    for (int i = static_cast<int>(children->count()) - 1; i >= 0; --i) {
        auto* child = static_cast<CCNode*>(children->objectAtIndex(i));
        bool childBlocked = false;
        if (auto* found = hit(child, point, budget, childBlocked)) return found;
        if (childBlocked) {
            blocked = true;
            return nullptr;
        }
        // open dialogs block buttons behind them, including their empty area.
        if (child->isVisible() && typeinfo_cast<FLAlertLayer*>(child)) {
            blocked = true;
            return nullptr;
        }
    }
    return nullptr;
}

CCMenuItemSpriteExtra* under(CCPoint point) {
    int budget = 6000;
    bool blocked = false;
    return hit(CCDirector::get()->getRunningScene(), point, budget, blocked);
}

struct Animated {
    WeakRef<CCMenuItemSpriteExtra> item;
    WeakRef<CCNode> image;
    Config config;
    Pose applied;
    float amount = 0;
    float from = 0;
    float progress = 0;
    float age = 0;
    float elapsed = 0;
    bool hovered = false;

    void undo() {
        if (auto node = image.lock()) {
            node->setPosition(node->getPosition() - CCPoint{applied.x, applied.y});
            node->setScaleX(node->getScaleX() / applied.sx);
            node->setScaleY(node->getScaleY() / applied.sy);
            node->setRotation(node->getRotation() - applied.rotation);
        }
        applied = {};
    }

    void step(float dt, bool on) {
        undo();
        auto node = image.lock();
        if (!node) return;

        if (on != hovered) {
            hovered = on;
            from = amount;
            progress = 0;
            age = 0;
        }
        age += dt;
        elapsed += dt;
        if (!on || age >= config.delay) {
            progress = std::min(1.f, progress + dt / (on ? config.enter : config.exit));
        }
        amount = from + ((on ? 1.f : 0.f) - from) *
            ease(progress, on ? config.easing : 1);
        applied = pose(config, amount, elapsed);
        applied.sx = std::max(.1f, applied.sx);
        applied.sy = std::max(.1f, applied.sy);
        node->setPosition(node->getPosition() + CCPoint{applied.x, applied.y});
        node->setScaleX(node->getScaleX() * applied.sx);
        node->setScaleY(node->getScaleY() * applied.sy);
        node->setRotation(node->getRotation() + applied.rotation);
    }
};

class Ticker : public CCNode {
public:
    std::vector<Animated> animations;
    WeakRef<CCMenuItemSpriteExtra> current;
    float scan = 0;

    void clear() {
        for (auto& animation : animations) animation.undo();
        animations.clear();
        current = nullptr;
    }

    void update(float dt) override {
        if (!allowed() || paimon::settings::smoothui::reducedMotion()) {
            clear();
            return;
        }
        dt = std::clamp(dt, 0.f, .05f);
        scan -= dt;
        if (scan <= 0) {
            scan = 1.f / 30.f;
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
            current = activeTouches.empty() ? nullptr : under(touchPoint);
#else
            current = under(geode::cocos::getMousePos());
#endif
        }

        auto target = current.lock();
        if (target) {
            auto config = Manager::get().resolve(buttonKey(target.data()));
            if (!config.enabled || !target->isRunning()) {
                target = nullptr;
            } else if (std::none_of(animations.begin(), animations.end(),
                       [&](auto& animation) { return animation.item.lock() == target; })) {
                if (auto* normal = target->getNormalImage()) {
                    Animated animation;
                    animation.item = target.data();
                    animation.image = normal;
                    animation.config = config;
                    animations.push_back(animation);
                }
            }
        }

        for (auto it = animations.begin(); it != animations.end();) {
            auto item = it->item.lock();
            if (!item || !item->isRunning() || !it->image.lock()) {
                it->undo();
                it = animations.erase(it);
                continue;
            }
            bool on = target && item == target;
            if (on) it->config = Manager::get().resolve(buttonKey(item.data()));
            it->step(dt, on);
            if (!on && it->progress >= 1.f) {
                it->undo();
                it = animations.erase(it);
            } else {
                ++it;
            }
        }
    }
};

Ref<Ticker> ticker;

} // namespace

void init() {
    if (ticker) return;
    Manager::get().load();
    auto* node = new Ticker();
    node->init();
    ticker = node;
    node->release();
    CCDirector::get()->getScheduler()->scheduleUpdateForTarget(ticker.data(), 0, false);

    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        if (!allowed() || data.action != KeyboardInputData::Action::Press ||
            data.key != KEY_D ||
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
    if (ticker) {
        ticker->clear();
        CCDirector::get()->getScheduler()->unscheduleUpdateForTarget(ticker.data());
        ticker = nullptr;
    }
}

class $modify(PaimonHoverTouch, CCTouchDispatcher) {
    void touchesBegan(CCSet* touches, CCEvent* event) {
        auto* pass = CCSet::create();
        for (auto it = touches->begin(); it != touches->end(); ++it) {
            auto* object = *it;
            auto* touch = static_cast<CCTouch*>(object);
            int id = touch->getID();
            touchPoint = touch->getLocation();
            activeTouches.insert(id);

            auto* keyboard = CCDirector::get()->getKeyboardDispatcher();
            bool edit = Manager::get().picking ||
                (keyboard && keyboard->getControlKeyPressed());
            if (allowed() && edit && !popupOpen()) {
                if (auto* item = under(touchPoint)) {
                    auto key = buttonKey(item);
                    swallowedTouches.insert(id);
                    Manager::get().picking = false;
                    Loader::get()->queueInMainThread([key] {
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
            auto* touch = static_cast<CCTouch*>(object);
            touchPoint = touch->getLocation();
            if (!swallowedTouches.count(touch->getID())) pass->addObject(object);
            if (end) {
                activeTouches.erase(touch->getID());
                swallowedTouches.erase(touch->getID());
            }
        }
        return pass;
    }

    void touchesMoved(CCSet* touches, CCEvent* event) {
        auto* pass = forward(touches, false);
        if (pass->count()) CCTouchDispatcher::touchesMoved(pass, event);
    }

    void touchesEnded(CCSet* touches, CCEvent* event) {
        auto* pass = forward(touches, true);
        if (pass->count()) CCTouchDispatcher::touchesEnded(pass, event);
    }

    void touchesCancelled(CCSet* touches, CCEvent* event) {
        auto* pass = forward(touches, true);
        if (pass->count()) CCTouchDispatcher::touchesCancelled(pass, event);
    }
};

} // namespace paimon::hover
