#include "LiveSlotEditor.hpp"

#include "ParamSliderRow.hpp"
#include "../engine/ColorPresets.hpp"
#include "../persist/SlotStore.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/ProjectShare.hpp"
#include "../services/LiveSlotRuntime.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/FileDialog.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::texture_studio {
namespace {
CCLabelBMFont* label(CCNode* parent, char const* text, CCPoint pos, float scale = .4f) {
    auto* result = CCLabelBMFont::create(text, "bigFont.fnt");
    result->setScale(scale);
    result->setPosition(pos);
    parent->addChild(result);
    return result;
}

void button(CCMenu* menu, char const* text, CCPoint pos, std::function<void()> callback,
            char const* background = "GJ_button_04.png", float scale = .38f) {
    auto* sprite = ButtonSprite::create(text, "bigFont.fnt", background, scale);
    auto* item = CCMenuItemExt::createSpriteExtra(sprite,
        [callback = std::move(callback)](CCMenuItemSpriteExtra*) { callback(); });
    item->setPosition(pos);
    menu->addChild(item);
}
}

void LiveSlotEditor::open(std::string const& id) {
    auto* editor = new LiveSlotEditor;
    if (editor->init(id)) {
        editor->autorelease();
        geode::pushSceneWithLayer(editor);
    } else delete editor;
}

bool LiveSlotEditor::init(std::string const& id) {
    if (!CCLayer::init()) return false;
    auto loaded = SlotStore::get().loadSlot(id);
    if (!loaded) {
        Notification::create(loaded.unwrapErr(), NotificationIcon::Error)->show();
        return false;
    }
    m_project = loaded.unwrap();
    LiveSlotRuntime::normalize(m_project);
    setID("packgen-live-editor"_spr);
    setKeypadEnabled(true);
    auto win = CCDirector::get()->getWinSize();
    auto* bg = CCLayerColor::create({17, 15, 28, 255});
    bg->setContentSize(win);
    addChild(bg, -1);

    label(this, "Pack Gen", {78.f, win.height - 23.f}, .62f);
    m_name = TextInput::create(std::min(220.f, win.width - 260.f), "Slot name");
    m_name->setString(m_project.name);
    m_name->setMaxCharCount(40);
    m_name->setPosition({win.width * .57f, win.height - 23.f});
    m_name->setCallback([this](std::string const& name) {
        m_project.name = name;
        m_dirty = true;
    });
    addChild(m_name);

    float leftW = win.width * .52f - 18.f;
    m_controls = CCNode::create();
    m_controls->setContentSize({leftW, win.height - 114.f});
    m_controls->setPosition({12.f, 42.f});
    addChild(m_controls);
    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    addChild(menu, 2);
    button(menu, "Export", {win.width - 35.f, win.height - 23.f},
        [this] { exportConfig(); }, "GJ_button_05.png", .3f);
    char const* tabs[] = {"Palette", "Effects", "Regions"};
    for (int i = 0; i < 3; ++i) {
        button(menu, tabs[i], {12.f + leftW * (i + .5f) / 3.f, win.height - 59.f},
            [this, i] { buildControls(i); }, "GJ_button_04.png", .3f);
    }
    button(menu, "Back", {39.f, 19.f}, [this] { back(); });
    button(menu, "Save & Apply", {win.width - 68.f, 19.f}, [this] { save(); }, "GJ_button_01.png");
    m_status = label(this, "", {win.width * .47f, 19.f}, .3f);
    buildControls(0);
    buildPreview();
    applyPreview();
    schedule(schedule_selector(LiveSlotEditor::refreshStatus), .2f);
    return true;
}

void LiveSlotEditor::buildControls(int page) {
    m_page = page;
    m_controls->removeAllChildren();
    auto size = m_controls->getContentSize();
    float w = size.width, h = size.height;
    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_controls->addChild(menu);
    auto slider = [&](char const* text, float y, float min, float max, float step,
                      float initial, std::function<void(float)> change, bool regions = false) {
        auto* row = ParamSliderRow::create(text, min, max, step, initial, w,
            [this, change, regions](float value) { change(value); edited(regions); });
        row->setPosition({0, y});
        m_controls->addChild(row);
    };
    if (page == 0) {
        char const* names[] = {"Primary", "Secondary", "Glow", "Detail"};
        ccColor3B TextureProject::* colors[] = {&TextureProject::color1, &TextureProject::color2,
            &TextureProject::colorGlow, &TextureProject::colorDetail};
        for (int i = 0; i < 4; ++i) {
            float x = w * (i + .5f) / 4.f;
            auto member = colors[i];
            auto* swatch = CCSprite::create("square.png");
            swatch->setColor(m_project.*member);
            swatch->setScale(26.f / swatch->getContentSize().width);
            auto* item = CCMenuItemExt::createSpriteExtra(swatch,
                [this, member](CCMenuItemSpriteExtra*) {
                    auto c = m_project.*member;
                    auto* picker = ColorPickPopup::create(c);
                    if (!picker) return;
                    m_pickerTarget = CCSprite::create();
                    m_pickerTarget->setColor(c);
                    m_pickerColor = member;
                    picker->setColorTarget(m_pickerTarget);
                    schedule(schedule_selector(LiveSlotEditor::previewPicker));
                    WeakRef<LiveSlotEditor> weak(this);
                    picker->setCallback([weak, member](ccColor4B const& color) {
                        auto self = weak.lock();
                        if (!self) return;
                        self->unschedule(schedule_selector(LiveSlotEditor::previewPicker));
                        self->m_pickerColor = nullptr;
                        self->m_project.*member = {color.r, color.g, color.b};
                        self->edited();
                        self->buildControls(0);
                    });
                    picker->show();
                });
            item->setPosition({x, h - 22.f});
            menu->addChild(item);
            label(m_controls, names[i], {x, h - 46.f}, .23f);
        }
        slider("Intensity", h - 78.f, 0, 1, .01f, m_project.tintStrength,
            [this](float v) { m_project.tintStrength = v; });
        slider("Brightness", h - 108.f, 1, 1000, 1, m_project.brightness,
            [this](float v) { m_project.brightness = std::lround(v); });
        button(menu, "Next preset", {w * .28f, h - 146.f}, [this] {
            auto const& presets = ColorPresets::list();
            if (presets.empty()) return;
            m_preset = (m_preset + 1) % static_cast<int>(presets.size());
            auto const& preset = presets[m_preset];
            m_project.color1 = preset.color1;
            m_project.color2 = preset.color2;
            m_project.colorGlow = preset.colorGlow;
            m_project.brightness = preset.brightness;
            edited();
            buildControls(0);
        }, "GJ_button_05.png", .3f);
        button(menu, "Reset", {w * .78f, h - 146.f}, [this] {
            auto defaults = TextureProject{};
            defaults.id = m_project.id;
            defaults.name = m_project.name;
            defaults.author = m_project.author;
            defaults.createdAt = m_project.createdAt;
            defaults.sheets = m_project.sheets;
            LiveSlotRuntime::normalize(defaults);
            m_project = std::move(defaults);
            edited(true);
            buildControls(m_page);
        }, "GJ_button_06.png", .3f);
        label(m_controls, "White detail keeps the original whites", {w / 2, h - 174.f}, .22f);
    } else if (page == 1) {
        slider("Saturation", h - 20.f, 0, 3, .01f, m_project.saturation,
            [this](float v) { m_project.saturation = v; });
        slider("Contrast", h - 50.f, -1, 1, .01f, m_project.contrast,
            [this](float v) { m_project.contrast = v; });
        slider("Glow", h - 80.f, 0, 1, .01f, m_project.glowStrength,
            [this](float v) { m_project.glowStrength = v; });
        slider("Dark protect", h - 110.f, 0, 255, 1, m_project.outlineProtect,
            [this](float v) { m_project.outlineProtect = std::lround(v); });
        button(menu, m_project.alternativeGlowOverlay ? "Glow: replace" : "Glow: blend",
            {w / 2, h - 147.f}, [this] {
                m_project.alternativeGlowOverlay = !m_project.alternativeGlowOverlay;
                edited();
                buildControls(1);
            }, "GJ_button_04.png", .32f);
        label(m_controls, "Glow adjusts the existing glow regions", {w / 2, h - 174.f}, .22f);
    } else {
        slider("Softness", h - 20.f, 0, 1, .01f, m_project.maskSoftness,
            [this](float v) { m_project.maskSoftness = v; }, true);
        slider("Precision", h - 50.f, 2, 10, 1, m_project.clusterPrecision,
            [this](float v) { m_project.clusterPrecision = std::lround(v); }, true);
        slider("Edge clean", h - 80.f, 0, 4, 1, m_project.edgeCleanup,
            [this](float v) { m_project.edgeCleanup = std::lround(v); }, true);
        button(menu, m_project.tintScope == TintScope::ButtonsOnly ? "Buttons only" : "Buttons + menu UI",
            {w / 2, h - 116.f}, [this] {
                m_project.tintScope = m_project.tintScope == TintScope::ButtonsOnly
                    ? TintScope::ButtonsAndMenuUi : TintScope::ButtonsOnly;
                edited(true);
                buildControls(2);
            }, "GJ_button_04.png", .32f);
        label(m_controls, "Region changes need a new analysis.", {w / 2, h - 150.f}, .23f);
        label(m_controls, "Colors update immediately after that.", {w / 2, h - 168.f}, .23f);
    }
}

void LiveSlotEditor::buildPreview() {
    auto win = CCDirector::get()->getWinSize();
    float x = win.width * .76f, w = win.width * .43f;
    label(this, "Live preview", {x, win.height - 60.f}, .48f);
    char const* files[] = {"GJ_button_01.png", "GJ_button_02.png", "GJ_button_04.png"};
    for (int i = 0; i < 3; ++i) {
        if (auto* sprite = CCSprite::create(files[i])) {
            sprite->setScale(std::min(62.f / sprite->getContentSize().width, .9f));
            sprite->setPosition({x + (i - 1) * w * .3f, win.height - 112.f});
            addChild(sprite);
        }
    }
    char const* frames[] = {"GJ_arrow_01_001.png", "GJ_checkOn_001.png", "GJ_tabOn_001.png"};
    for (int i = 0; i < 3; ++i) {
        if (auto* sprite = CCSprite::createWithSpriteFrameName(frames[i])) {
            sprite->setScale(std::min(48.f / sprite->getContentSize().width, .9f));
            sprite->setPosition({x + (i - 1) * w * .3f, win.height - 171.f});
            addChild(sprite);
        }
    }
    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    addChild(menu);
    button(menu, "Compare original", {x, 96.f}, [this] {
        m_compare = !m_compare;
        if (m_compare) LiveSlotRuntime::get().showOriginal();
        else applyPreview();
    }, "GJ_button_05.png", .33f);
    label(this, "Save applies this slot to the game.", {x, 66.f}, .25f);
    label(this, "Back discards unsaved changes.", {x, 49.f}, .23f);
}

void LiveSlotEditor::edited(bool regions) {
    m_dirty = true;
    m_project.modifiedAt = nowUnixMs();
    unschedule(schedule_selector(LiveSlotEditor::applyPreview));
    if (regions) scheduleOnce(schedule_selector(LiveSlotEditor::applyPreview), .25f);
    else applyPreview();
}

void LiveSlotEditor::applyPreview(float) {
    if (m_compare) return;
    if (auto result = LiveSlotRuntime::get().preview(m_project); !result) {
        Notification::create(result.unwrapErr(), NotificationIcon::Error)->show();
    }
}

void LiveSlotEditor::refreshStatus(float) {
    std::string text = m_compare ? "Original colors" : LiveSlotRuntime::get().status();
    if (m_dirty) text += " *";
    m_status->setString(text.c_str());
    m_status->limitLabelWidth(CCDirector::get()->getWinSize().width - 240.f, .3f, .16f);
}

void LiveSlotEditor::previewPicker(float) {
    if (!m_pickerTarget || !m_pickerColor) return;
    auto color = m_pickerTarget->getColor();
    auto previous = m_project.*m_pickerColor;
    if (color.r == previous.r && color.g == previous.g && color.b == previous.b) return;
    m_project.*m_pickerColor = color;
    edited();
}

void LiveSlotEditor::exportConfig() {
    auto name = SlotPaths::sanitizeFilename(m_project.name) + ".json";
    WeakRef<LiveSlotEditor> weak(this);
    pt::saveJson(name, [weak](geode::Result<std::optional<std::filesystem::path>> result) {
        auto self = weak.lock();
        auto path = std::move(result).unwrapOr(std::nullopt);
        if (!self || !path) return;
        auto exported = ProjectShare::exportTo(*path, self->m_project);
        if (!exported) {
            Notification::create(exported.unwrapErr(), NotificationIcon::Error)->show();
            return;
        }
        Notification::create("Slot configuration exported.", NotificationIcon::Success)->show();
    });
}

void LiveSlotEditor::save() {
    unschedule(schedule_selector(LiveSlotEditor::applyPreview));
    if (m_project.name.find_first_not_of(" \t\r\n") == std::string::npos) {
        Notification::create("Enter a slot name.", NotificationIcon::Warning)->show();
        return;
    }
    LiveSlotRuntime::normalize(m_project);
    m_project.modifiedAt = nowUnixMs();
    auto saved = SlotStore::get().saveSlot(m_project);
    if (!saved) {
        Notification::create("Save failed: " + saved.unwrapErr(), NotificationIcon::Error)->show();
        return;
    }
    auto applied = LiveSlotRuntime::get().activate(m_project);
    if (!applied) {
        Notification::create("Saved; apply failed: " + applied.unwrapErr(), NotificationIcon::Error)->show();
        return;
    }
    m_compare = false;
    m_dirty = false;
    Notification::create("Slot saved and applied.", NotificationIcon::Success)->show();
}

void LiveSlotEditor::onExit() {
    unschedule(schedule_selector(LiveSlotEditor::applyPreview));
    unschedule(schedule_selector(LiveSlotEditor::previewPicker));
    if (!paimon::isRuntimeShuttingDown()) LiveSlotRuntime::get().restoreSaved();
    CCLayer::onExit();
}

void LiveSlotEditor::keyBackClicked() { back(); }

void LiveSlotEditor::back() {
    CCDirector::get()->popSceneWithTransition(.3f, PopTransition::kPopTransitionFade);
}

}
