#include "PaimonIconsConfigPopup.hpp"

#include "../services/IconConfigStore.hpp"

#include <Geode/Geode.hpp>

#include <array>
#include <string>

using namespace geode::prelude;

namespace paimon::icons::ui {

namespace {

constexpr float kPopupW = 460.f;
constexpr float kPopupH = 280.f;

// Small UI primitives so tab builders read declaratively.

CCLabelBMFont* makeLabel(std::string const& text, const char* font = "bigFont.fnt", float scale = 0.45f) {
    auto* lbl = CCLabelBMFont::create(text.c_str(), font);
    if (lbl) lbl->setScale(scale);
    return lbl;
}

// A wrapper CCObject that holds a std::function callback, since Cocos2d
// menu items only accept (target, menu_selector) callbacks. We retain the
// callback object on the toggler via setUserObject so it lives as long as
// the toggler does.
class TogglerCallback final : public CCObject {
public:
    std::function<void(bool)> m_cb;
    Ref<CCMenuItemToggler> m_toggler;

    static TogglerCallback* create(std::function<void(bool)> cb) {
        auto* o = new TogglerCallback();
        o->m_cb = std::move(cb);
        o->autorelease();
        return o;
    }
    void onToggle(CCObject*) {
        if (!m_toggler) return;
        if (m_cb) m_cb(!m_toggler->isToggled());
    }
};

// A single toggle row: [label] [toggle].
CCNode* makeToggleRow(std::string const& label, bool initial, std::function<void(bool)> onChange) {
    auto* row = CCNode::create();
    row->setContentSize({420.f, 26.f});

    if (auto* lbl = makeLabel(label)) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, 13.f});
        lbl->limitLabelWidth(280.f, 0.45f, 0.25f);
        row->addChild(lbl);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    auto* cb = TogglerCallback::create(std::move(onChange));
    auto* tog = CCMenuItemToggler::createWithStandardSprites(
        cb, menu_selector(TogglerCallback::onToggle), 0.7f);
    if (tog) {
        tog->toggle(initial);
        tog->setPosition({400.f, 13.f});
        cb->m_toggler = tog;
        tog->setUserObject(cb);  // keep the callback alive
        menu->addChild(tog);
    }
    row->addChild(menu);
    return row;
}

// Slider row: [label] [slider track] [value]
CCNode* makeSliderRow(std::string const& label, float minVal, float maxVal, float current,
                     std::function<void(float)> onChange) {
    auto* row = CCNode::create();
    row->setContentSize({420.f, 30.f});

    if (auto* lbl = makeLabel(label)) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, 22.f});
        lbl->limitLabelWidth(180.f, 0.4f, 0.25f);
        row->addChild(lbl);
    }

    auto* valueLbl = makeLabel(numToString(current, 2), "chatFont.fnt", 0.5f);
    if (valueLbl) {
        valueLbl->setAnchorPoint({1.f, 0.5f});
        valueLbl->setPosition({410.f, 22.f});
        row->addChild(valueLbl);
    }
    Ref<CCLabelBMFont> valueRef = valueLbl;

    // Slider needs a wrapper due to anchor quirks.
    auto* sliderWrap = CCNode::create();
    sliderWrap->setContentSize({200.f, 16.f});
    sliderWrap->setAnchorPoint({0.f, 0.5f});
    sliderWrap->setPosition({200.f, 7.f});
    row->addChild(sliderWrap);

    Slider* slider = Slider::create(nullptr, nullptr, 0.6f);
    if (slider) {
        if (maxVal > minVal) {
            slider->setValue(std::clamp((current - minVal) / (maxVal - minVal), 0.f, 1.f));
        }
        slider->setPosition({100.f, 8.f});
        sliderWrap->addChild(slider);

        // Cocos doesn't give us a "value changed" callback for the bare
        // Slider, so we poll at 15Hz (sufficient for UI responsiveness).
        struct Watcher : public CCNode {
            std::function<void(float)> cb;
            Ref<Slider> slider;
            Ref<CCLabelBMFont> valLbl;
            float minV, maxV;
            float lastNorm = -1.f;
            void check(float) {
                if (!slider) return;
                float v = slider->getValue();
                if (std::fabs(v - lastNorm) < 1e-4f) return;
                lastNorm = v;
                float mapped = minV + (maxV - minV) * v;
                if (valLbl) valLbl->setString(numToString(mapped, 2).c_str());
                if (cb) cb(mapped);
            }
        };
        auto* watcher = new Watcher();
        watcher->autorelease();
        watcher->cb     = std::move(onChange);
        watcher->slider = slider;
        watcher->valLbl = valueRef;
        watcher->minV   = minVal;
        watcher->maxV   = maxVal;
        watcher->schedule(schedule_selector(Watcher::check), 1.f / 15.f);
        sliderWrap->addChild(watcher);
    }

    return row;
}

// Color swatch row: [label] [color square button]
CCNode* makeColorRow(std::string const& label, ccColor3B initial,
                     std::function<void(ccColor3B)> onChange) {
    auto* row = CCNode::create();
    row->setContentSize({420.f, 26.f});

    if (auto* lbl = makeLabel(label)) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, 13.f});
        lbl->limitLabelWidth(260.f, 0.45f, 0.25f);
        row->addChild(lbl);
    }

    auto* swatch = CCLayerColor::create({initial.r, initial.g, initial.b, 255}, 22.f, 22.f);
    swatch->setAnchorPoint({0.5f, 0.5f});
    swatch->ignoreAnchorPointForPosition(false);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    Ref<CCLayerColor> swatchRef = swatch;
    auto* btn = CCMenuItemExt::createSpriteExtra(swatch,
        [cb = std::move(onChange), swatchRef](CCMenuItemSpriteExtra*) {
            ccColor4B current{255, 255, 255, 255};
            if (swatchRef) {
                auto c = swatchRef->getColor();
                current = {c.r, c.g, c.b, 255};
            }
            auto* popup = ColorPickPopup::create(current);
            if (!popup) return;
            popup->setCallback([cb, swatchRef](ccColor4B const& picked) {
                cb({picked.r, picked.g, picked.b});
                if (swatchRef) {
                    swatchRef->setColor({picked.r, picked.g, picked.b});
                }
            });
            popup->show();
        });
    btn->setPosition({400.f, 13.f});
    menu->addChild(btn);
    row->addChild(menu);
    return row;
}

// Cycle row for an enum: [label] [<] [value name] [>]
CCNode* makeCycleRow(std::string const& label, std::vector<std::string> const& options,
                     int initialIdx, std::function<void(int)> onChange) {
    auto* row = CCNode::create();
    row->setContentSize({420.f, 26.f});

    if (auto* lbl = makeLabel(label)) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, 13.f});
        lbl->limitLabelWidth(140.f, 0.45f, 0.25f);
        row->addChild(lbl);
    }

    auto* valueLbl = makeLabel(initialIdx >= 0 && initialIdx < (int)options.size()
                                   ? options[initialIdx]
                                   : "?",
                               "bigFont.fnt", 0.45f);
    valueLbl->setAnchorPoint({0.5f, 0.5f});
    valueLbl->setPosition({280.f, 13.f});
    valueLbl->limitLabelWidth(140.f, 0.45f, 0.25f);
    row->addChild(valueLbl);
    Ref<CCLabelBMFont> valueRef = valueLbl;

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});

    auto changeBy = [valueRef, options, cb = onChange,
                     idxPtr = std::make_shared<int>(initialIdx)](int delta) {
        if (options.empty()) return;
        int newIdx = (*idxPtr + delta + (int)options.size()) % (int)options.size();
        *idxPtr = newIdx;
        if (valueRef) valueRef->setString(options[newIdx].c_str());
        if (cb) cb(newIdx);
    };

    auto* leftSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (leftSpr) leftSpr->setScale(0.4f);
    auto* leftBtn = CCMenuItemExt::createSpriteExtra(leftSpr ? leftSpr : CCSprite::create(),
        [changeBy](CCMenuItemSpriteExtra*) { changeBy(-1); });
    leftBtn->setPosition({220.f, 13.f});
    menu->addChild(leftBtn);

    auto* rightSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (rightSpr) {
        rightSpr->setScale(0.4f);
        rightSpr->setFlipX(true);
    }
    auto* rightBtn = CCMenuItemExt::createSpriteExtra(rightSpr ? rightSpr : CCSprite::create(),
        [changeBy](CCMenuItemSpriteExtra*) { changeBy(1); });
    rightBtn->setPosition({340.f, 13.f});
    menu->addChild(rightBtn);
    row->addChild(menu);

    return row;
}

}  // anonymous namespace



PaimonIconsConfigPopup* PaimonIconsConfigPopup::open() {
    auto* p = new PaimonIconsConfigPopup();
    if (p->init()) {
        p->autorelease();
        p->show();
        return p;
    }
    delete p;
    return nullptr;
}

bool PaimonIconsConfigPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    IconConfigStore::get().load();
    setTitle("Paimon Icons");
    buildHeader();
    buildTabs();
    buildBody();
    switchTo(Tab::General);
    return true;
}

void PaimonIconsConfigPopup::buildHeader() {
    // Reset button in the bottom-right of the popup body.
    auto* spr = ButtonSprite::create("Reset", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    if (!spr) return;
    spr->setScale(0.55f);
    auto* btn = CCMenuItemExt::createSpriteExtra(spr,
        [self = WeakRef<PaimonIconsConfigPopup>(this)](CCMenuItemSpriteExtra*) {
            geode::createQuickPopup("Reset",
                "Reset all <cy>Paimon Icons</c> settings to defaults?",
                "Cancel", "Reset",
                [self](FLAlertLayer*, bool yes) {
                    if (yes) IconConfigStore::get().resetToDefaults();
                });
        });
    btn->setPosition({0.f, 0.f});
    if (m_buttonMenu) {
        m_buttonMenu->addChildAtPosition(btn, Anchor::BottomRight, {-30.f, 20.f});
    }
}

void PaimonIconsConfigPopup::buildTabs() {
    m_tabMenu = CCMenu::create();
    m_tabMenu->setContentSize({kPopupW - 40.f, 24.f});
    m_tabMenu->ignoreAnchorPointForPosition(false);

    static constexpr std::array<const char*, 6> kNames = {
        "General", "Locked", "Anim", "Apply", "Per-Mode", "Presets"
    };

    for (int i = 0; i < (int)kNames.size(); ++i) {
        auto* spr = ButtonSprite::create(kNames[i], "goldFont.fnt", "GJ_button_05.png", 0.7f);
        if (!spr) continue;
        spr->setScale(0.5f);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [self = WeakRef<PaimonIconsConfigPopup>(this), i](CCMenuItemSpriteExtra*) {
                if (auto p = self.lock()) p->switchTo(static_cast<Tab>(i));
            });
        m_tabMenu->addChild(btn);
    }
    m_tabMenu->setLayout(RowLayout::create()
        ->setGap(4.f)
        ->setAxisAlignment(AxisAlignment::Center));
    m_tabMenu->updateLayout();

    if (m_mainLayer) {
        m_mainLayer->addChildAtPosition(m_tabMenu, Anchor::Top, {0.f, -28.f});
    }
}

void PaimonIconsConfigPopup::buildBody() {
    m_body = CCNode::create();
    m_body->setContentSize({kPopupW - 40.f, kPopupH - 80.f});
    m_body->ignoreAnchorPointForPosition(false);
    m_body->setAnchorPoint({0.5f, 0.5f});
    if (m_mainLayer) {
        m_mainLayer->addChildAtPosition(m_body, Anchor::Center, {0.f, -10.f});
    }
}

void PaimonIconsConfigPopup::switchTo(Tab tab) {
    if (!m_body) return;
    m_activeTab = tab;
    m_body->removeAllChildren();

    CCNode* content = nullptr;
    switch (tab) {
        case Tab::General:     content = buildGeneralTab(); break;
        case Tab::LockedIcons: content = buildLockedTab(); break;
        case Tab::Animations:  content = buildAnimationsTab(); break;
        case Tab::ApplyTo:     content = buildApplyToTab(); break;
        case Tab::PerGamemode: content = buildPerGamemodeTab(); break;
        case Tab::Presets:     content = buildPresetsTab(); break;
    }
    if (!content) return;
    content->setAnchorPoint({0.5f, 0.5f});
    content->setPosition(m_body->getContentSize() / 2.f);
    m_body->addChild(content);
}

// Tab builders return fresh CCNode owned by the popup.

CCNode* PaimonIconsConfigPopup::buildGeneralTab() {
    auto* root = CCNode::create();
    root->setContentSize({420.f, 180.f});
    root->setLayout(ColumnLayout::create()
        ->setGap(2.f)
        ->setAxisReverse(true)
        ->setAxisAlignment(AxisAlignment::Start)
        ->setCrossAxisAlignment(AxisAlignment::Center));

    auto const& cfg = IconConfigStore::get().config();

    // Mode selector.
    static const std::vector<std::string> kModes = {
        "Player", "Custom RGB", "Hue Shift", "Saturation Boost",
        "Random (stable)", "Rainbow", "Gradient", "Per-Gamemode",
        "Inverted", "Monochrome"
    };
    root->addChild(makeCycleRow("Color Mode", kModes, static_cast<int>(cfg.mode),
        [](int idx) {
            IconConfigStore::get().update([&](PaimonIconConfig& c) {
                c.mode = static_cast<ColorMode>(idx);
            });
        }));

    // Show all knobs regardless of mode (cheap, avoids live body rebuild).
    root->addChild(makeColorRow("Custom Primary",   cfg.custom1,
        [](ccColor3B c) { IconConfigStore::get().update([&](auto& cfg) { cfg.custom1 = c; }); }));
    root->addChild(makeColorRow("Custom Secondary", cfg.custom2,
        [](ccColor3B c) { IconConfigStore::get().update([&](auto& cfg) { cfg.custom2 = c; }); }));
    root->addChild(makeColorRow("Custom Glow",      cfg.customGlow,
        [](ccColor3B c) { IconConfigStore::get().update([&](auto& cfg) { cfg.customGlow = c; }); }));
    root->addChild(makeSliderRow("Hue Shift (deg)", 0.f, 360.f, cfg.hueShiftDegrees,
        [](float v) { IconConfigStore::get().update([&](auto& cfg) { cfg.hueShiftDegrees = v; }); }));
    root->addChild(makeSliderRow("Saturation x",    0.0f, 2.0f, cfg.saturationMul,
        [](float v) { IconConfigStore::get().update([&](auto& cfg) { cfg.saturationMul = v; }); }));
    root->addChild(makeSliderRow("Brightness x",    0.0f, 2.0f, cfg.brightnessMul,
        [](float v) { IconConfigStore::get().update([&](auto& cfg) { cfg.brightnessMul = v; }); }));

    root->updateLayout();
    return root;
}

CCNode* PaimonIconsConfigPopup::buildLockedTab() {
    auto* root = CCNode::create();
    root->setContentSize({420.f, 180.f});
    root->setLayout(ColumnLayout::create()
        ->setGap(2.f)
        ->setAxisReverse(true)
        ->setAxisAlignment(AxisAlignment::Start)
        ->setCrossAxisAlignment(AxisAlignment::Center));

    auto const& cfg = IconConfigStore::get().config();

    static const std::vector<std::string> kStyles = {
        "Default", "Show Dimmed", "Tinted Lock", "Silhouette", "Custom Mix", "Hide Both"
    };
    root->addChild(makeCycleRow("Lock Style", kStyles, static_cast<int>(cfg.lockStyle),
        [](int idx) {
            IconConfigStore::get().update([&](auto& c) {
                c.lockStyle = static_cast<LockStyle>(idx);
            });
        }));
    root->addChild(makeSliderRow("Dim Opacity", 0.f, 255.f, static_cast<float>(cfg.dimOpacity),
        [](float v) { IconConfigStore::get().update([&](auto& cfg) { cfg.dimOpacity = (int)v; }); }));
    root->addChild(makeToggleRow("Dim Unobtainable Icons", cfg.dimUnobtainable,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.dimUnobtainable = v; }); }));
    root->addChild(makeSliderRow("Unobtainable Opacity", 0.f, 255.f,
        static_cast<float>(cfg.unobtainableOpacity),
        [](float v) { IconConfigStore::get().update([&](auto& cfg) { cfg.unobtainableOpacity = (int)v; }); }));
    root->addChild(makeColorRow("Lock Tint", cfg.lockTint,
        [](ccColor3B c) { IconConfigStore::get().update([&](auto& cfg) { cfg.lockTint = c; }); }));
    root->addChild(makeColorRow("Silhouette Color", cfg.silhouetteColor,
        [](ccColor3B c) { IconConfigStore::get().update([&](auto& cfg) { cfg.silhouetteColor = c; }); }));

    root->updateLayout();
    return root;
}

CCNode* PaimonIconsConfigPopup::buildAnimationsTab() {
    auto* root = CCNode::create();
    root->setContentSize({420.f, 180.f});
    root->setLayout(ColumnLayout::create()
        ->setGap(2.f)
        ->setAxisReverse(true)
        ->setAxisAlignment(AxisAlignment::Start)
        ->setCrossAxisAlignment(AxisAlignment::Center));

    auto const& cfg = IconConfigStore::get().config();

    root->addChild(makeToggleRow("Pulse Locked Icons", cfg.anim.pulseLocked,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.anim.pulseLocked = v; }); }));
    root->addChild(makeSliderRow("Pulse Speed", 0.1f, 3.0f, cfg.anim.pulseSpeed,
        [](float v) { IconConfigStore::get().update([&](auto& cfg) { cfg.anim.pulseSpeed = v; }); }));
    root->addChild(makeToggleRow("Idle Float", cfg.anim.idleFloat,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.anim.idleFloat = v; }); }));
    root->addChild(makeSliderRow("Float Amount (px)", 0.f, 8.f, cfg.anim.floatAmount,
        [](float v) { IconConfigStore::get().update([&](auto& cfg) { cfg.anim.floatAmount = v; }); }));
    root->addChild(makeToggleRow("Selected Highlight", cfg.anim.selectedHighlight,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.anim.selectedHighlight = v; }); }));
    root->addChild(makeColorRow("Highlight Color", cfg.anim.highlightColor,
        [](ccColor3B c) { IconConfigStore::get().update([&](auto& cfg) { cfg.anim.highlightColor = c; }); }));
    root->addChild(makeToggleRow("Beat Sync (CPU heavy)", cfg.anim.beatSync,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.anim.beatSync = v; }); }));

    root->updateLayout();
    return root;
}

CCNode* PaimonIconsConfigPopup::buildApplyToTab() {
    auto* root = CCNode::create();
    root->setContentSize({420.f, 180.f});
    root->setLayout(ColumnLayout::create()
        ->setGap(2.f)
        ->setAxisReverse(true)
        ->setAxisAlignment(AxisAlignment::Start)
        ->setCrossAxisAlignment(AxisAlignment::Center));

    auto const& cfg = IconConfigStore::get().config();
    root->addChild(makeToggleRow("Icon Kit (garage)", cfg.apply.kit,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.apply.kit = v; }); }));
    root->addChild(makeToggleRow("Shops",             cfg.apply.shops,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.apply.shops = v; }); }));
    root->addChild(makeToggleRow("Achievements",      cfg.apply.achievements,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.apply.achievements = v; }); }));
    root->addChild(makeToggleRow("Reward Popups",     cfg.apply.rewards,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.apply.rewards = v; }); }));
    root->addChild(makeToggleRow("Profile Pages",     cfg.apply.profiles,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.apply.profiles = v; }); }));
    root->addChild(makeToggleRow("Comment Cells",     cfg.apply.comments,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.apply.comments = v; }); }));
    root->addChild(makeToggleRow("Level Cells",       cfg.apply.levelCells,
        [](bool v) { IconConfigStore::get().update([&](auto& cfg) { cfg.apply.levelCells = v; }); }));

    root->updateLayout();
    return root;
}

CCNode* PaimonIconsConfigPopup::buildPerGamemodeTab() {
    // Placeholder: 9 buttons, one per gamemode, that open ColorPickPopup
    // with the slot's color1 (we keep this trivial so it compiles; the
    // detail edit popup is a future enhancement).
    auto* root = CCNode::create();
    root->setContentSize({420.f, 180.f});

    auto* msg = makeLabel("Per-Gamemode mode is selected from General > Color Mode.\n"
                          "Customize each gamemode color below.", "chatFont.fnt", 0.55f);
    if (msg) {
        msg->setPosition({210.f, 150.f});
        root->addChild(msg);
    }

    static const std::array<const char*, 9> kNames = {
        "Cube", "Ship", "Ball", "UFO", "Wave", "Robot", "Spider", "Swing", "Jetpack"
    };
    auto const& cfg = IconConfigStore::get().config();
    for (int i = 0; i < 9; ++i) {
        ccColor3B initial = cfg.perMode[i].has_value() ? cfg.perMode[i]->color1 : ccColor3B{255, 255, 255};
        auto* row = makeColorRow(kNames[i], initial, [i](ccColor3B c) {
            IconConfigStore::get().update([&](PaimonIconConfig& cfg) {
                if (!cfg.perMode[i]) cfg.perMode[i] = GamemodePalette{};
                cfg.perMode[i]->color1 = c;
            });
        });
        row->setPosition({0.f, 130.f - i * 14.f});
        row->setAnchorPoint({0.f, 0.5f});
        root->addChild(row);
    }
    return root;
}

CCNode* PaimonIconsConfigPopup::buildPresetsTab() {
    auto* root = CCNode::create();
    root->setContentSize({420.f, 180.f});

    auto* note = makeLabel(
        "Save current settings as a preset to switch between favourites.",
        "chatFont.fnt", 0.55f);
    if (note) {
        note->setPosition({210.f, 150.f});
        note->limitLabelWidth(390.f, 0.55f, 0.3f);
        root->addChild(note);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    auto* spr = ButtonSprite::create("Save Current", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    if (spr) {
        spr->setScale(0.6f);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [](CCMenuItemSpriteExtra*) {
                // Auto-name (MVP; real popup would prompt).
                std::size_t n = IconConfigStore::get().config().presets.size() + 1;
                IconConfigStore::get().saveCurrentAsPreset("Preset " + std::to_string(n));
            });
        btn->setPosition({100.f, 80.f});
        menu->addChild(btn);
    }
    root->addChild(menu);

    // List presets (no scroll).
    auto const& presets = IconConfigStore::get().config().presets;
    for (std::size_t i = 0; i < presets.size() && i < 4; ++i) {
        auto* lbl = makeLabel(presets[i].name, "bigFont.fnt", 0.45f);
        if (lbl) {
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({40.f, 50.f - (float)i * 16.f});
            root->addChild(lbl);
        }
    }
    return root;
}

}  // namespace paimon::icons::ui
