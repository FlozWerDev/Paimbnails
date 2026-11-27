#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/NodeEvent.hpp>

#include "../services/FrameInterpolator.hpp"

using namespace geode::prelude;

namespace {
inline paimon::frameinterp::FrameInterpolator& interp() {
    return paimon::frameinterp::FrameInterpolator::get();
}
}

class $modify(PaimonFrameInterpBaseLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // last in the chain wraps the real draw: any other mod hooking visit
        // still sees the true positions.
        (void)self.setHookPriority("GJBaseGameLayer::visit", geode::Priority::VeryLate);
    }

    bool init() {
        if (!GJBaseGameLayer::init()) return false;
        this->addOnExitCallback([] {
            interp().reset();
            return true;
        });
        return true;
    }

    double getModifiedDelta(float dt) {
        double const stepped = GJBaseGameLayer::getModifiedDelta(dt);
        interp().onStepped(stepped, m_extraDelta);
        return stepped;
    }

    void visit() {
        interp().beginVisit(this);
        GJBaseGameLayer::visit();
        interp().endVisit();
    }
};

class $modify(PaimonFrameInterpPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        interp().reset();
    }

    void onQuit() {
        interp().reset();
        PlayLayer::onQuit();
    }
};

class $modify(PaimonFrameInterpEditorLayer, LevelEditorLayer) {
    void onPlaytest() {
        interp().reset();
        LevelEditorLayer::onPlaytest();
    }

    void onStopPlaytest() {
        interp().reset();
        LevelEditorLayer::onStopPlaytest();
    }
};
