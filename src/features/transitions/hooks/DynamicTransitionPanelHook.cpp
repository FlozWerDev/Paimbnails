#include "../services/DynamicPanelTransitions.hpp"
#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/CCBlockLayer.hpp>
#include <Geode/modify/GJDropDownLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/RetryLevelLayer.hpp>
#include <Geode/modify/ChallengesPage.hpp>
#include <Geode/modify/ColorSelectPopup.hpp>
#include <Geode/modify/CommunityCreditsPage.hpp>
#include <Geode/modify/CustomSongLayer.hpp>
#include <Geode/modify/InfoLayer.hpp>
#include <Geode/modify/SetIDPopup.hpp>
#include <Geode/modify/SetupPulsePopup.hpp>
#include <Geode/modify/UploadPopup.hpp>
#include <Geode/modify/DialogLayer.hpp>
#ifdef GEODE_IS_MACOS
#include <Geode/modify/SlideInLayer.hpp>
#endif

using namespace geode::prelude;
namespace dynamic = paimon::transitions::dynamic;

class $modify(PaimonDynamicPanelNodes, CCNode) {
    void addChild(CCNode* child, int z, int tag) {
        dynamic::panelWillChange(child, this, true);
        CCNode::addChild(child, z, tag);
    }

    void removeChild(CCNode* child, bool cleanup) {
        dynamic::panelWillChange(child, this, false);
        CCNode::removeChild(child, cleanup);
    }

    void removeAllChildrenWithCleanup(bool cleanup) {
        if (static_cast<CCNode*>(this) == CCDirector::get()->getRunningScene()) {
            dynamic::finishPanelAnimation();
            CCNode::removeAllChildrenWithCleanup(cleanup);
            return;
        }
        if (auto* children = getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (dynamic::panelWillChange(child, this, false)) break;
            }
        }
        CCNode::removeAllChildrenWithCleanup(cleanup);
    }
};

class $modify(PaimonDynamicEndLevel, EndLevelLayer) {
    void showLayer(bool instant) {
        bool animated = dynamic::panelWillChange(this, getParent(), true);
        EndLevelLayer::showLayer(animated || instant);
    }
};

class $modify(PaimonDynamicRetryLevel, RetryLevelLayer) {
    void showLayer(bool instant) {
        bool animated = dynamic::panelWillChange(this, getParent(), true);
        RetryLevelLayer::showLayer(animated || instant);
    }
};

// Several popup classes share each of these native show implementations.
class $modify(PaimonDynamicChallenges, ChallengesPage) {
    void show() { dynamic::PanelShowGuard guard(this); ChallengesPage::show(); }
};

class $modify(PaimonDynamicColorSelect, ColorSelectPopup) {
    void show() { dynamic::PanelShowGuard guard(this); ColorSelectPopup::show(); }
};

class $modify(PaimonDynamicCommunityCredits, CommunityCreditsPage) {
    void show() { dynamic::PanelShowGuard guard(this); CommunityCreditsPage::show(); }
};

class $modify(PaimonDynamicCustomSong, CustomSongLayer) {
    void show() { dynamic::PanelShowGuard guard(this); CustomSongLayer::show(); }
};

class $modify(PaimonDynamicInfo, InfoLayer) {
    void show() { dynamic::PanelShowGuard guard(this); InfoLayer::show(); }
};

class $modify(PaimonDynamicSetID, SetIDPopup) {
    void show() { dynamic::PanelShowGuard guard(this); SetIDPopup::show(); }
};

class $modify(PaimonDynamicSetupPulse, SetupPulsePopup) {
    void show() { dynamic::PanelShowGuard guard(this); SetupPulsePopup::show(); }
};

class $modify(PaimonDynamicUpload, UploadPopup) {
    void show() { dynamic::PanelShowGuard guard(this); UploadPopup::show(); }
};

class $modify(PaimonDynamicDialog, DialogLayer) {
    void animateInRandomSide() {
        bool animated = dynamic::panelWillChange(this, getParent(), true);
        float duration = m_animateTime;
        if (animated) m_animateTime = 0.f;
        DialogLayer::animateInRandomSide();
        m_animateTime = duration;
    }
};

#ifdef GEODE_IS_MACOS
class $modify(PaimonDynamicSlideIn, SlideInLayer) {
    void showLayer(bool instant) {
        bool animated = dynamic::panelWillChange(this, getParent(), true);
        SlideInLayer::showLayer(animated || instant);
    }
    void hideLayer(bool instant) {
        bool animated = dynamic::panelWillChange(this, getParent(), false);
        SlideInLayer::hideLayer(animated || instant);
    }
};
#endif

class $modify(PaimonDynamicDropdown, GJDropDownLayer) {
    void showLayer(bool instant) {
        bool animated = dynamic::panelWillChange(this, getParent(), true);
        GJDropDownLayer::showLayer(animated || instant);
    }

    void hideLayer(bool instant) {
        bool animated = dynamic::panelWillChange(this, getParent(), false);
        GJDropDownLayer::hideLayer(animated || instant);
    }
};

class $modify(PaimonDynamicBlockingLayer, CCBlockLayer) {
    void showLayer(bool instant) {
        bool animated = dynamic::panelWillChange(this, getParent(), true);
        CCBlockLayer::showLayer(animated || instant);
    }

    void hideLayer(bool instant) {
        bool animated = dynamic::panelWillChange(this, getParent(), false);
        CCBlockLayer::hideLayer(animated || instant);
    }
};
