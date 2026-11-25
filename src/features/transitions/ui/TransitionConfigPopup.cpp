#include "TransitionConfigPopup.hpp"
#include "CustomTransitionLayer.hpp"
#include "LevelEntryConfigPopup.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

namespace ui = paimon::ui;
namespace kit = paimon::configkit;

static int typeIndexOf(TransitionType type) {
    auto const& types = TransitionManager::allTypes();
    for (int i = 0; i < (int)types.size(); i++) if (types[i] == type) return i;
    return 0;
}

TransitionConfigPopup* TransitionConfigPopup::create() {
    auto ret = new TransitionConfigPopup();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool TransitionConfigPopup::init() {
    if (!PaimonPopup::init(460.f, 340.f)) return false;
    this->setTitle("Transition Settings");
    this->addCorners();
    this->addInfoButton("Transitions",
        "Set the animation played when the game changes screens.\n\n"
        "<cy>Global</c> applies everywhere. <cy>Level Entry</c> can override it when you enter a level.\n"
        "Pick a style, adjust the duration, try <cg>Preview</cg>, then <cg>Save</cg>.\n\n"
        "Choose <cy>Custom (Studio)</c> to build your own OBS-style timeline: clips, easings, "
        "stinger cuts, color washes, shakes and parallel groups, with import/export.");

    auto& tm = TransitionManager::get();
    if (!tm.isEnabled()) tm.loadConfig();
    m_editingGlobal = tm.getGlobalConfig();
    m_enabled = tm.isEnabled();
    m_useLevel = tm.hasLevelEntryConfig();
    m_editingLevel = tm.getLevelEntryConfig();

    auto ws = m_mainLayer->getContentSize();
    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(0.3f);
    m_statusLabel->setColor(ui::palette::success);
    m_statusLabel->setAnchorPoint({0.f, 0.5f});
    m_statusLabel->setPosition({14.f, 18.f});
    m_mainLayer->addChild(m_statusLabel);

    auto saveBtn = ui::makeButton("Save", [this]() {
        auto& mgr = TransitionManager::get();
        mgr.setEnabled(m_enabled);
        mgr.setGlobalConfig(m_editingGlobal);
        if (m_useLevel) mgr.setLevelEntryConfig(m_editingLevel);
        else mgr.clearLevelEntryConfig();
        mgr.saveConfig();
        if (m_statusLabel) m_statusLabel->setString("Saved!");
        PaimonNotify::show("Transitions saved!", NotificationIcon::Success);
    }, ui::Btn::Green, 0.f, 0.7f);
    saveBtn->setPosition({ws.width - 48.f, 18.f});
    m_buttonMenu->addChild(saveBtn);

    rebuild();
    paimon::markDynamicPopup(this);
    return true;
}

void TransitionConfigPopup::previewConfig(TransitionConfig const& cfg) {
    auto director = CCDirector::get();
    auto winSize = director->getWinSize();
    auto destScene = CCScene::create();
    auto bg = CCLayerGradient::create({40, 160, 80, 255}, {80, 40, 160, 255}, {0.5f, 1.0f});
    destScene->addChild(bg);
    auto title = CCLabelBMFont::create("Transition Preview", "goldFont.fnt");
    title->setPosition({winSize.width / 2, winSize.height / 2 + 20});
    title->setScale(0.6f);
    destScene->addChild(title);
    auto sub = CCLabelBMFont::create("Returning automatically...", "chatFont.fnt");
    sub->setPosition({winSize.width / 2, winSize.height / 2 - 20});
    sub->setScale(0.5f);
    destScene->addChild(sub);

    class ReturnNode : public CCNode {
    public:
        static ReturnNode* create() {
            auto* node = new ReturnNode();
            if (node->init()) { node->autorelease(); return node; }
            delete node; return nullptr;
        }
        void onEnterTransitionDidFinish() override {
            CCNode::onEnterTransitionDidFinish();
            scheduleOnce(schedule_selector(ReturnNode::doReturn), 1.2f);
        }
        void onExit() override { unschedule(schedule_selector(ReturnNode::doReturn)); CCNode::onExit(); }
        void doReturn(float) { CCDirector::get()->popScene(); }
    };
    if (auto* node = ReturnNode::create()) destScene->addChild(node);

    auto* trans = TransitionManager::get().createTransition(cfg, destScene, true);
    if (!trans || trans == destScene) {
        PaimonNotify::show("Nothing to preview for this type yet.", NotificationIcon::Info);
        return;
    }
    director->pushScene(trans);
}

void TransitionConfigPopup::rebuild() {
    if (m_scroll) { m_scroll->removeFromParent(); m_scroll = nullptr; }
    auto ws = m_mainLayer->getContentSize();
    float scrollW = ws.width - 24.f;
    float scrollH = ws.height - 78.f;
    float innerW = kit::cardInnerWidth(scrollW);
    WeakRef<TransitionConfigPopup> self = this;
    auto queueRebuild = [self]() {
        Loader::get()->queueInMainThread([self]() { if (auto p = self.lock()) p->rebuild(); });
    };

    auto typeRows = [&](TransitionConfig& cfg, bool isGlobal, std::vector<CCNode*>& rows) {
        std::vector<std::string> names;
        for (auto t : TransitionManager::allTypes()) names.push_back(TransitionManager::typeDisplayName(t));
        rows.push_back(kit::makeSelectRow(innerW, isGlobal ? "Style" : "Style",
            TransitionManager::typeDescription(cfg.type).c_str(),
            names, typeIndexOf(cfg.type),
            [self, isGlobal, queueRebuild](int index) {
                auto p = self.lock(); if (!p) return;
                auto const& types = TransitionManager::allTypes();
                if (index < 0 || index >= (int)types.size()) return;
                (isGlobal ? p->m_editingGlobal : p->m_editingLevel).type = types[index];
                queueRebuild();
            }));
        rows.push_back(kit::makeSliderRow(innerW, "Duration",
            "How long the animation takes (0.05s fast, 1s+ cinematic).",
            cfg.duration, 0.05, 3.0,
            [](double v) { return fmt::format("{:.2f}s", v); },
            [self, isGlobal](double v) {
                auto p = self.lock(); if (!p) return;
                (isGlobal ? p->m_editingGlobal : p->m_editingLevel).duration = (float)v;
            }));
        if (cfg.type == TransitionType::FadeColor) {
            rows.push_back(kit::makeColorRow(innerW, "Fade color", "Color shown in the middle of the fade.",
                {(GLubyte)cfg.colorR, (GLubyte)cfg.colorG, (GLubyte)cfg.colorB},
                [self, isGlobal](ccColor3B c) {
                    auto p = self.lock(); if (!p) return;
                    auto& target = isGlobal ? p->m_editingGlobal : p->m_editingLevel;
                    target.colorR = c.r; target.colorG = c.g; target.colorB = c.b;
                }));
        }
        if (cfg.type == TransitionType::Custom) {
            rows.push_back(kit::makeButtonRow(innerW, "Transition Studio",
                cfg.customName.empty()
                    ? "Full-screen editor: objects, timeline, Move, Alpha, Rotate and Follow Cursor."
                    : fmt::format("\"{}\" - OBS-style timeline editor.", cfg.customName).c_str(),
                "Open", [self, isGlobal]() {
                    auto p = self.lock(); if (!p) return;
                    auto cfgCopy = isGlobal ? p->m_editingGlobal : p->m_editingLevel;
                    auto scene = CustomTransitionLayer::scene(cfgCopy, isGlobal,
                        [self, isGlobal](TransitionConfig updated) {
                            auto owner = self.lock(); if (!owner) return;
                            (isGlobal ? owner->m_editingGlobal : owner->m_editingLevel) = std::move(updated);
                            owner->rebuild();
                        });
                    if (scene) cocos2d::CCDirector::get()->pushScene(scene);
                }, ui::Btn::Cyan));
        }
        if (cfg.type == TransitionType::Stinger) {
            rows.push_back(kit::makeButtonRow(innerW, "Stinger / OBS",
                "Open the full-screen editor with this media and its A/B cut point.",
                "Open", [self, isGlobal]() {
                    auto p = self.lock(); if (!p) return;
                    auto cfgCopy = isGlobal ? p->m_editingGlobal : p->m_editingLevel;
                    if (auto* scene = CustomTransitionLayer::scene(cfgCopy, isGlobal,
                        [self, isGlobal](TransitionConfig updated) {
                            auto owner = self.lock(); if (!owner) return;
                            (isGlobal ? owner->m_editingGlobal : owner->m_editingLevel) = std::move(updated);
                            owner->rebuild();
                        })) cocos2d::CCDirector::get()->pushScene(scene);
                }, ui::Btn::Cyan));
        }
        rows.push_back(kit::makeButtonRow(innerW, "Preview", "Play this transition once and come back.",
            "Play", [self, isGlobal]() {
                auto p = self.lock(); if (!p) return;
                p->previewConfig(isGlobal ? p->m_editingGlobal : p->m_editingLevel);
            }, ui::Btn::Gray));
    };

    std::vector<CCNode*> items;
    items.push_back(kit::makeHeroToggle(scrollW, "Transitions enabled",
        "Master switch for every screen change in the game.",
        m_enabled, [self](bool v) { if (auto p = self.lock()) p->m_enabled = v; }));

    std::vector<CCNode*> globalRows;
    typeRows(m_editingGlobal, true, globalRows);
    items.push_back(kit::makeCard(scrollW, "Global", {255, 202, 105}, globalRows));

    std::vector<CCNode*> levelRows;
    levelRows.push_back(kit::makeToggleRow(innerW, "Separate Level Entry",
        "Use a different transition only when entering a level.",
        m_useLevel, [self, queueRebuild](bool v) {
            auto p = self.lock(); if (!p) return;
            p->m_useLevel = v;
            if (v && !TransitionManager::get().hasLevelEntryConfig()) p->m_editingLevel = p->m_editingGlobal;
            queueRebuild();
        }));
    if (m_useLevel) typeRows(m_editingLevel, false, levelRows);
    levelRows.push_back(kit::makeButtonRow(innerW, "Smooth+...",
        "Live Smooth+ choreography for level entry/exit (replaces the preset while on).",
        "Open", []() {
            if (auto* popup = paimon::transitions::LevelEntryConfigPopup::create()) popup->show();
        }, ui::Btn::Pink));
    items.push_back(kit::makeCard(scrollW, "Level Entry", {115, 215, 255}, levelRows));

    items.push_back(kit::makeHint(scrollW,
        "Tip: Custom (Studio) and Stinger (OBS) open their own editors. "
        "Changes here only apply after Save."));

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 30.f});
    m_mainLayer->addChild(m_scroll, 1);
}
