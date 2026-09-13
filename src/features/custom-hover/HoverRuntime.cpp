#include "CustomHover.hpp"
#include "../../core/RuntimeLifecycle.hpp"
#include "../../core/Settings.hpp"
#include "../../core/modules/ModuleRegistry.hpp"
#include <Geode/modify/CCTouchDispatcher.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <set>

using namespace geode::prelude;
namespace paimon::hover {
namespace {
CCPoint touchPoint;
std::set<int> activeTouches, swallowed;
bool allowed() { return !paimon::isRuntimeShuttingDown() && paimon::modules::isEnabled("paimbnails.customhover.global"); }

CCMenuItemSpriteExtra* hit(CCNode* node, CCPoint point, int& budget) {
    if (!node || --budget<0 || !node->isVisible()) return nullptr;
    if(node->getID()=="custom-hover-popup" || typeinfo_cast<PlayLayer*>(node) || typeinfo_cast<LevelEditorLayer*>(node)) return nullptr;
    if(auto* scroll=typeinfo_cast<ScrollLayer*>(node)) {
        if(!CCRect{0,0,scroll->getContentWidth(),scroll->getContentHeight()}.containsPoint(scroll->convertToNodeSpace(point))) return nullptr;
    }
    if(auto* menu=typeinfo_cast<CCMenu*>(node); menu && !menu->isTouchEnabled()) return nullptr;
    if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
        if(item->isEnabled() && item->isRunning() && CCRect{0,0,item->getContentWidth(),item->getContentHeight()}.containsPoint(item->convertToNodeSpace(point))) return item;
        return nullptr;
    }
    node->sortAllChildren();
    auto children=node->getChildren();
    if(!children) return nullptr;
    for(int i=static_cast<int>(children->count())-1;i>=0;--i) {
        auto child=static_cast<CCNode*>(children->objectAtIndex(i));
        if(auto* found=hit(child,point,budget)) return found;
        // An open modal blocks all buttons underneath, including its empty area.
        if(child->isVisible() && typeinfo_cast<FLAlertLayer*>(child)) return nullptr;
    }
    return nullptr;
}
CCMenuItemSpriteExtra* under(CCPoint p) {
    int budget=6000; return hit(CCDirector::get()->getRunningScene(),p,budget);
}
struct Animated {
    WeakRef<CCMenuItemSpriteExtra> item;
    WeakRef<CCNode> image;
    Config config;
    Pose applied;
    float amount=0, from=0, progress=0, age=0, elapsed=0;
    bool hovered=false;
    // Remove only our own contribution: native press actions target the item,
    // while hover decorates its normal image without changing the hitbox.
    void undo() {
        if(auto n=image.lock()) {
            n->setPosition(n->getPosition()-CCPoint{applied.x,applied.y});
            n->setScaleX(n->getScaleX()/applied.sx); n->setScaleY(n->getScaleY()/applied.sy);
            n->setRotation(n->getRotation()-applied.rotation);
        }
        applied={};
    }
    void step(float dt, bool on) {
        undo();
        auto n=image.lock(); if(!n) return;
        if(on!=hovered) { hovered=on; from=amount; progress=0; age=0; }
        age+=dt; elapsed+=dt;
        if(!on || age>=config.delay) progress=std::min(1.f,progress+dt/(on?config.enter:config.exit));
        amount=from+((on?1.f:0.f)-from)*ease(progress,on?config.easing:1);
        applied=pose(config,amount,elapsed);
        // Overshooting easings must never produce zero/negative scales.
        applied.sx=std::max(.1f,applied.sx); applied.sy=std::max(.1f,applied.sy);
        n->setPosition(n->getPosition()+CCPoint{applied.x,applied.y});
        n->setScaleX(n->getScaleX()*applied.sx); n->setScaleY(n->getScaleY()*applied.sy);
        n->setRotation(n->getRotation()+applied.rotation);
    }
};
class Ticker : public CCNode {
public:
    std::vector<Animated> animations;
    WeakRef<CCMenuItemSpriteExtra> current;
    float scan=0;
    void clear() { for(auto& a:animations) a.undo(); animations.clear(); current=nullptr; }
    void update(float dt) override {
        if(!allowed() || paimon::settings::smoothui::reducedMotion()) { clear(); return; }
        dt=std::clamp(dt,0.f,.05f); scan-=dt;
        if(scan<=0) {
            scan=1.f/30.f;
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
            current=activeTouches.empty()?nullptr:under(touchPoint);
#else
            current=under(geode::cocos::getMousePos());
#endif
        }
        auto target=current.lock();
        if(target) {
            auto c=Manager::get().resolve(buttonKey(target.data()));
            if(!c.enabled || !target->isRunning()) target=nullptr;
            else if(std::none_of(animations.begin(),animations.end(),[&](auto& a){return a.item.lock()==target;})) {
                if(auto* normal=target->getNormalImage()) {
                    Animated a; a.item=target.data(); a.image=normal; a.config=c; animations.push_back(a);
                }
            }
        }
        for(auto it=animations.begin();it!=animations.end();) {
            auto item=it->item.lock();
            if(!item || !item->isRunning() || !it->image.lock()) { it->undo(); it=animations.erase(it); continue; }
            bool on=target && item==target;
            if(on) it->config=Manager::get().resolve(buttonKey(item.data()));
            it->step(dt,on);
            if(!on && it->progress>=1.f) { it->undo(); it=animations.erase(it); } else ++it;
        }
    }
};
Ref<Ticker> ticker;
}
void init() {
    if(ticker) return;
    Manager::get().load();
    auto* t=new Ticker(); t->init(); ticker=t; t->release();
    CCDirector::get()->getScheduler()->scheduleUpdateForTarget(ticker.data(),0,false);
    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        if(!allowed() || data.action!=KeyboardInputData::Action::Press || data.key!=KEY_D ||
           !(data.modifiers.value & uint8_t(KeyboardModifier::Control))) return false;
        if(popupOpen()) { groupFromPopup(); return true; }
        if(auto* item=under(geode::cocos::getMousePos())) {
            Manager::get().group(Manager::get().resolve(buttonKey(item)));
            Notification::create("Hover: todos vinculados",NotificationIcon::Success)->show();
            return true;
        }
        return false;
    }).leak();
}
$on_game(Exiting) {
    if(ticker) {
        ticker->clear();
        CCDirector::get()->getScheduler()->unscheduleUpdateForTarget(ticker.data());
        ticker=nullptr;
    }
}
// Intercept the entire configuration gesture before any native button is
// selected. Ordinary touch gestures propagate unchanged, including scrolling.
class $modify(PaimonHoverTouch, CCTouchDispatcher) {
    void touchesBegan(CCSet* touches, CCEvent* event) {
        auto* pass=CCSet::create();
        for(auto it=touches->begin(); it!=touches->end(); ++it) {
            auto* obj=*it;
            auto* touch=static_cast<CCTouch*>(obj); int id=touch->getID();
            touchPoint=touch->getLocation(); activeTouches.insert(id);
            auto* kb=CCDirector::get()->getKeyboardDispatcher();
            bool edit=Manager::get().picking || (kb && kb->getControlKeyPressed());
            if(allowed() && edit && !popupOpen()) {
                if(auto* item=under(touchPoint)) {
                    auto key=buttonKey(item); swallowed.insert(id); Manager::get().picking=false;
                    Loader::get()->queueInMainThread([key]{if(!paimon::isRuntimeShuttingDown()) open(key);});
                    continue;
                }
            }
            pass->addObject(obj);
        }
        if(pass->count()) CCTouchDispatcher::touchesBegan(pass,event);
    }
    CCSet* forward(CCSet* touches, bool end) {
        auto* pass=CCSet::create();
        for(auto it=touches->begin(); it!=touches->end(); ++it) {
            auto* obj=*it;
            auto* t=static_cast<CCTouch*>(obj); touchPoint=t->getLocation();
            if(!swallowed.count(t->getID())) pass->addObject(obj);
            if(end) { activeTouches.erase(t->getID()); swallowed.erase(t->getID()); }
        }
        return pass;
    }
    void touchesMoved(CCSet* touches, CCEvent* event) { auto* p=forward(touches,false); if(p->count()) CCTouchDispatcher::touchesMoved(p,event); }
    void touchesEnded(CCSet* touches, CCEvent* event) { auto* p=forward(touches,true); if(p->count()) CCTouchDispatcher::touchesEnded(p,event); }
    void touchesCancelled(CCSet* touches, CCEvent* event) { auto* p=forward(touches,true); if(p->count()) CCTouchDispatcher::touchesCancelled(p,event); }
};
}
