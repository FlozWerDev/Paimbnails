// Group Summary popup: list used group IDs in the level (BetterEdit-inspired).
// Opens from editor pause menu button + keybind Ctrl+Shift+U.

#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"
#include "../EditorUIKit.hpp"
#include "../api/TriggerUtil.hpp"

#include <Geode/binding/EditorPauseLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <map>
#include <set>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {

bool on() { return moduleEnabled("editor-mod-group-summary"); }

struct GroupInfo {
    int objects = 0;
    int asTarget = 0;
    int asCenter = 0;
};

std::map<int, GroupInfo> collectGroups(LevelEditorLayer* lel) {
    std::map<int, GroupInfo> map;
    if (!lel || !lel->m_objects) return map;
    for (auto* o : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (!o) continue;
        if (o->m_groups) {
            for (short i = 0; i < o->m_groupCount; ++i) {
                int g = o->m_groups->at(static_cast<size_t>(i));
                if (g > 0) map[g].objects++;
            }
        }
        if (auto* e = typeinfo_cast<EffectGameObject*>(o)) {
            if (e->m_targetGroupID > 0) map[e->m_targetGroupID].asTarget++;
            if (e->m_centerGroupID > 0) map[e->m_centerGroupID].asCenter++;
        }
    }
    return map;
}

class GroupSummaryLayer : public CCLayer {
public:
    static GroupSummaryLayer* create(LevelEditorLayer* lel) {
        auto* r = new GroupSummaryLayer();
        if (r && r->init(lel)) {
            r->autorelease();
            return r;
        }
        CC_SAFE_DELETE(r);
        return nullptr;
    }

    bool init(LevelEditorLayer* lel) {
        if (!CCLayer::init()) return false;
        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);
        this->setID("paimbnails/group-summary-layer");

        auto win = CCDirector::get()->getWinSize();
        this->addChild(CCLayerColor::create({0, 0, 0, 170}, win.width, win.height), -1);

        auto* panel = CCScale9Sprite::create("GJ_square01.png");
        if (!panel) panel = CCScale9Sprite::create("square02_001.png");
        panel->setContentSize({320.f, 260.f});
        panel->setPosition(win / 2.f);
        this->addChild(panel);

        auto* title = CCLabelBMFont::create("Group Summary", "goldFont.fnt");
        title->setScale(0.6f);
        title->setPosition(win / 2.f + ccp(0.f, 110.f));
        this->addChild(title);

        auto groups = collectGroups(lel);
        auto* scroll = ScrollLayer::create({280.f, 180.f});
        scroll->setPosition(win / 2.f + ccp(-140.f, -95.f));
        scroll->m_contentLayer->setLayout(
            ColumnLayout::create()
                ->setAxisReverse(true)
                ->setAutoScale(false)
                ->setGap(3.f)
                ->setAxisAlignment(AxisAlignment::End)
        );

        if (groups.empty()) {
            auto* empty = CCLabelBMFont::create("No groups in use", "bigFont.fnt");
            empty->setScale(0.35f);
            scroll->m_contentLayer->addChild(empty);
        } else {
            for (auto const& [id, info] : groups) {
                auto line = fmt::format(
                    "G{}  objs:{}  tgt:{}  ctr:{}",
                    id, info.objects, info.asTarget, info.asCenter
                );
                auto* lab = CCLabelBMFont::create(line.c_str(), "chatFont.fnt");
                lab->setScale(0.45f);
                lab->setAnchorPoint({0.f, 0.5f});
                scroll->m_contentLayer->addChild(lab);
            }
        }
        scroll->m_contentLayer->updateLayout();
        scroll->scrollToTop();
        this->addChild(scroll);

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        this->addChild(menu);
        auto* close = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"),
            this, menu_selector(GroupSummaryLayer::onClose)
        );
        close->setPosition(win / 2.f + ccp(145.f, 115.f));
        menu->addChild(close);

        auto* count = CCLabelBMFont::create(
            fmt::format("{} groups", groups.size()).c_str(), "chatFont.fnt"
        );
        count->setScale(0.4f);
        count->setPosition(win / 2.f + ccp(0.f, -115.f));
        this->addChild(count);
        return true;
    }

    void keyBackClicked() override { this->removeFromParentAndCleanup(true); }
    void onClose(CCObject*) { this->removeFromParentAndCleanup(true); }
};

void openSummary(LevelEditorLayer* lel) {
    if (!lel) return;
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || scene->getChildByID("paimbnails/group-summary-layer")) return;
    if (auto* layer = GroupSummaryLayer::create(lel)) {
        scene->addChild(layer, 250);
    }
}

} // namespace

class $modify(PaimonGroupSummaryPause, EditorPauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorPauseLayer::init");
    }

    void onOpenSummary(CCObject*) {
        if (on()) openSummary(m_editorLayer);
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorPauseLayer::init(lel)) return false;
        if (!on()) return true;

        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("info-menu"));
        if (!menu) menu = typeinfo_cast<CCMenu*>(this->getChildByID("settings-menu"));
        if (!menu) return true;

        // Custom: paim_group-summary.png  |  Fallback: text "Groups"
        auto* btn = assets::iconOrTextButton(
            assets::files::groupSummary, {},
            "Groups", "GJ_button_05.png", 0.4f, CircleBaseColor::Cyan,
            [this] { this->onOpenSummary(nullptr); }
        );
        if (!btn) return true;
        btn->setID("paimbnails/group-summary-btn");
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }
};

class $modify(PaimonGroupSummaryKeys, EditorUI) {
    $override
    void keyDown(enumKeyCodes key, double timestamp) {
        if (on() && key == KEY_U) {
            auto* kd = CCKeyboardDispatcher::get();
            if (kd && kd->getControlKeyPressed() && kd->getShiftKeyPressed()) {
                openSummary(m_editorLayer);
                return;
            }
        }
        EditorUI::keyDown(key, timestamp);
    }
};
