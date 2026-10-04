#include "LevelCellSettingsPopup.hpp"
#include "../services/CompactListRefresh.hpp"
#include "../../../blur/PopupBlurService.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <algorithm>
#include <unordered_set>

using namespace geode::prelude;
using namespace cocos2d;

namespace kit = paimon::configkit;
namespace ui = paimon::ui;

namespace {
// external blurapi marks these nodes separately from our paiblurnode.
constexpr char const* kBlurApiTag = "thesillydoggo.blur-api/blur-options";
constexpr char const* kSavedTab = "levelcell-settings-tab";

constexpr float kPopupW = 380.f;
constexpr float kPopupH = 290.f;
constexpr float kScrollX = 12.f;
constexpr float kScrollY = 44.f;
constexpr float kScrollbarW = 8.f;
constexpr float kTabsGap = 6.f;

struct Defaults {
    static constexpr char const* bgType = "thumbnail";
    static constexpr float thumbWidth = 0.5f;
    static constexpr float blur = 3.0f;
    static constexpr float darkness = 0.2f;
    static constexpr float edgeBlend = 0.65f;
    static constexpr char const* animType = "zoom-slide";
    static constexpr float animSpeed = 1.0f;
    static constexpr char const* animEffect = "none";
};

int indexOf(std::vector<std::string> const& list, std::string const& value) {
    auto it = std::find(list.begin(), list.end(), value);
    return it == list.end() ? 0 : static_cast<int>(it - list.begin());
}

std::function<std::string(double)> fixedFormat(int precision, char const* suffix = "") {
    return [precision, suffix](double v) {
        return precision == 1 ? fmt::format("{:.1f}{}", v, suffix) : fmt::format("{:.2f}{}", v, suffix);
    };
}

int childTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}
}

std::string LevelCellSettingsPopup::getBgTypeDisplayName(std::string const& type) {
    if (type == "gradient") return "Gradient";
    if (type == "legacy-gradient") return "Legacy Gradient";
    if (type == "thumbnail") return "Thumbnail";
    return type;
}

std::string LevelCellSettingsPopup::getAnimTypeDisplayName(std::string const& type) {
    if (type == "none") return "None";
    if (type == "zoom-slide") return "Zoom Slide";
    if (type == "zoom") return "Zoom";
    if (type == "slide") return "Slide";
    if (type == "bounce") return "Bounce";
    if (type == "rotate") return "Rotate";
    if (type == "rotate-content") return "Rotate Content";
    if (type == "shake") return "Shake";
    if (type == "pulse") return "Pulse";
    if (type == "swing") return "Swing";
    return type;
}

std::string LevelCellSettingsPopup::getAnimEffectDisplayName(std::string const& effect) {
    if (effect == "none") return "None";
    if (effect == "brightness") return "Brightness";
    if (effect == "darken") return "Darken";
    if (effect == "sepia") return "Sepia";
    if (effect == "red") return "Red";
    if (effect == "blue") return "Blue";
    if (effect == "gold") return "Gold";
    if (effect == "fade") return "Fade";
    if (effect == "grayscale") return "Grayscale";
    if (effect == "blur") return "Blur";
    if (effect == "invert") return "Invert";
    if (effect == "glitch") return "Glitch";
    if (effect == "sharpen") return "Sharpen";
    if (effect == "edge-detection") return "Edge Detection";
    if (effect == "vignette") return "Vignette";
    if (effect == "pixelate") return "Pixelate";
    if (effect == "posterize") return "Posterize";
    if (effect == "chromatic") return "Chromatic";
    if (effect == "scanlines") return "Scanlines";
    if (effect == "solarize") return "Solarize";
    if (effect == "rainbow") return "Rainbow";
    return effect;
}

void LevelCellSettingsPopup::onExit() {
    this->unschedule(schedule_selector(LevelCellSettingsPopup::checkDragState));
    // restore external blur on close-mid-drag.
    if (m_dragHiding) {
        m_dragHiding = false;
        m_activeDragSlider = nullptr;
        restoreDragVisibility();
        paimon::popupblur::setLivePreviewMode(this, false, 0.f);
        if (m_savedBlurApiOptions) {
            this->setUserObject(kBlurApiTag, m_savedBlurApiOptions.data());
            m_savedBlurApiOptions = nullptr;
        }
    }
    Popup::onExit();
}

void LevelCellSettingsPopup::loadSettings() {
    auto* mod = Mod::get();
    m_currentBgType = mod->getSavedValue<std::string>("levelcell-background-type", Defaults::bgType);
    m_currentThumbWidth = static_cast<float>(mod->getSettingValue<double>("level-thumb-width"));
    m_currentBlur = static_cast<float>(mod->getSavedValue<double>("levelcell-background-blur", Defaults::blur));
    m_currentDarkness = static_cast<float>(mod->getSavedValue<double>("levelcell-background-darkness", Defaults::darkness));
    m_currentEdgeBlend = static_cast<float>(paimon::settings::thumbnails::thumbnailEdgeBlend());
    m_showSeparator = mod->getSavedValue<bool>("levelcell-show-separator", true);
    m_showViewButton = mod->getSavedValue<bool>("levelcell-show-view-button", true);
    m_compactMode = mod->getSettingValue<bool>("compact-list-mode");
    m_compactShowQuickToggle = mod->getSavedValue<bool>("compact-list-show-toggle", true);
    m_transparentMode = mod->getSavedValue<bool>("transparent-list-mode", false);
    m_hoverEffects = mod->getSettingValue<bool>("levelcell-hover-effects");
    m_currentAnimType = mod->getSavedValue<std::string>("levelcell-anim-type", Defaults::animType);
    m_currentAnimSpeed = static_cast<float>(mod->getSavedValue<double>("levelcell-anim-speed", Defaults::animSpeed));
    m_currentAnimEffect = mod->getSavedValue<std::string>("levelcell-anim-effect", Defaults::animEffect);
    m_effectOnGradient = mod->getSavedValue<bool>("levelcell-effect-on-gradient", false);
    m_mythicParticles = mod->getSavedValue<bool>("levelcell-mythic-particles", true);
    m_animatedGradient = mod->getSavedValue<bool>("levelcell-animated-gradient", true);

    m_bgTypeIndex = indexOf(m_bgTypes, m_currentBgType);
    m_animTypeIndex = indexOf(m_animTypes, m_currentAnimType);
    m_animEffectIndex = indexOf(m_animEffects, m_currentAnimEffect);
}

void LevelCellSettingsPopup::saveSettings() {
    // same types levelcell and settings.hpp read.
    auto* mod = Mod::get();
    mod->setSavedValue<std::string>("levelcell-background-type", m_currentBgType);
    mod->setSettingValue<double>("level-thumb-width", static_cast<double>(m_currentThumbWidth));
    mod->setSavedValue<double>("levelcell-background-blur", static_cast<double>(m_currentBlur));
    mod->setSavedValue<double>("levelcell-background-darkness", static_cast<double>(m_currentDarkness));
    mod->setSavedValue<double>("levelcell-thumbnail-edge-blend", static_cast<double>(m_currentEdgeBlend));
    mod->setSavedValue<bool>("levelcell-show-separator", m_showSeparator);
    mod->setSavedValue<bool>("levelcell-show-view-button", m_showViewButton);
    mod->setSettingValue<bool>("compact-list-mode", m_compactMode);
    mod->setSavedValue<bool>("compact-list-show-toggle", m_compactShowQuickToggle);
    mod->setSavedValue<bool>("transparent-list-mode", m_transparentMode);
    mod->setSettingValue<bool>("levelcell-hover-effects", m_hoverEffects);
    mod->setSavedValue<std::string>("levelcell-anim-type", m_currentAnimType);
    mod->setSavedValue<double>("levelcell-anim-speed", static_cast<double>(m_currentAnimSpeed));
    mod->setSavedValue<std::string>("levelcell-anim-effect", m_currentAnimEffect);
    mod->setSavedValue<bool>("levelcell-effect-on-gradient", m_effectOnGradient);
    mod->setSavedValue<bool>("levelcell-mythic-particles", m_mythicParticles);
    mod->setSavedValue<bool>("levelcell-animated-gradient", m_animatedGradient);

    // invalidate cell watcher + shared settings cache.
    paimon::settings::internal::invalidateSettingsCache();
    s_settingsVersion++;

    if (m_onSettingsChanged) m_onSettingsChanged();
}

// frame-polled slider drag state, no hooks.
void LevelCellSettingsPopup::checkDragState(float) {
    Slider* dragging = nullptr;
    for (auto& row : m_sliderRows) {
        if (row.slider && row.slider->getLiveDragging()) {
            dragging = row.slider;
            break;
        }
    }

    if (dragging != m_activeDragSlider) {
        m_activeDragSlider = dragging;
        applyDragVisibility(dragging);
    }

    if (dragging) {
        updateDragCaption(dragging);
    }
}

// live value caption stays above the active slider.
void LevelCellSettingsPopup::updateDragCaption(Slider* active) {
    if (!m_dragCaptionPill || !active) return;

    std::string title;
    std::string valueText = "0.00";
    for (auto& row : m_sliderRows) {
        if (row.slider == active) {
            title = row.title;
            if (row.valueLabel) valueText = row.valueLabel->getString();
            break;
        }
    }
    if (m_dragCaptionLabel) {
        m_dragCaptionLabel->setString(fmt::format("{}: {}", title, valueText).c_str());
        m_dragCaptionLabel->limitLabelWidth(m_dragCaptionPill->getContentSize().width - 14.f, 0.38f, 0.2f);
    }

    auto* thumb = active->getThumb();
    CCPoint worldPos = thumb
        ? thumb->convertToWorldSpace({thumb->getContentSize().width * 0.5f, thumb->getContentSize().height})
        : active->convertToWorldSpace({0.f, 0.f});
    CCPoint localPos = m_mainLayer->convertToNodeSpace(worldPos);
    localPos.y += 20.f;

    auto size = m_mainLayer->getContentSize();
    float halfW = m_dragCaptionPill->getContentSize().width * 0.5f;
    localPos.x = std::clamp(localPos.x, halfW + 4.f, size.width - halfW - 4.f);
    localPos.y = std::clamp(localPos.y, 20.f, size.height - 10.f);

    m_dragCaptionPill->setPosition(localPos);
}

void LevelCellSettingsPopup::restoreDragVisibility() {
    for (auto& node : m_dragHidden) {
        if (node) node->setVisible(true);
    }
    m_dragHidden.clear();
}

void LevelCellSettingsPopup::applyDragVisibility(Slider* active) {
    bool hiding = (active != nullptr);
    m_dragHiding = hiding;

    restoreDragVisibility();
    if (hiding) {
        // keep only the chain from the dragged slider (and its value) up to the popup layer.
        std::unordered_set<CCNode*> keep;
        auto keepChain = [&](CCNode* node) {
            for (; node && node != m_mainLayer; node = node->getParent()) keep.insert(node);
        };
        keepChain(active);
        for (auto& row : m_sliderRows) {
            if (row.slider == active) keepChain(row.valueLabel);
        }
        keepChain(m_dragCaptionPill);

        std::unordered_set<CCNode*> visitedParents;
        for (auto* node : std::vector<CCNode*>(keep.begin(), keep.end())) {
            auto* parent = node->getParent();
            if (!parent || !visitedParents.insert(parent).second) continue;
            for (auto* child : CCArrayExt<CCNode*>(parent->getChildren())) {
                if (keep.contains(child) || !child->isVisible()) continue;
                child->setVisible(false);
                m_dragHidden.emplace_back(child);
            }
        }
    }

    // fade the dim layer mid-drag to preview the list beneath; restore after.
    if (hiding && m_dimOriginalOpacity == 0) {
        m_dimOriginalOpacity = this->getOpacity();
        if (m_dimOriginalOpacity == 0) m_dimOriginalOpacity = 150;
    }
    this->stopActionByTag(9912);
    auto dimFade = CCFadeTo::create(0.22f, hiding ? 0 : m_dimOriginalOpacity);
    dimFade->setTag(9912);
    this->runAction(dimFade);

    constexpr float kBlurFade = 0.22f;
    paimon::popupblur::setLivePreviewMode(this, hiding, kBlurFade);

    // external blurapi markers detach mid-drag, restore after.
    if (hiding) {
        if (!m_savedBlurApiOptions) {
            if (auto* opts = this->getUserObject(kBlurApiTag)) {
                m_savedBlurApiOptions = opts;
                this->setUserObject(kBlurApiTag, nullptr);
            }
        }
    } else if (m_savedBlurApiOptions) {
        this->setUserObject(kBlurApiTag, m_savedBlurApiOptions.data());
        m_savedBlurApiOptions = nullptr;
    }

    if (m_dragCaptionPill) {
        m_dragCaptionPill->stopAllActions();
        if (hiding) {
            m_dragCaptionPill->setVisible(true);
            m_dragCaptionPill->setScale(0.85f);
            m_dragCaptionPill->setOpacity(0);
            m_dragCaptionPill->runAction(CCSpawn::create(
                CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.f)),
                CCFadeTo::create(0.14f, 255),
                nullptr));
        } else {
            auto fadeOut = CCFadeTo::create(0.14f, 0);
            auto hide = CCCallFunc::create(this, callfunc_selector(LevelCellSettingsPopup::onDragCaptionHidden));
            m_dragCaptionPill->runAction(CCSequence::create(fadeOut, hide, nullptr));
        }
    }

    if (hiding && active) {
        updateDragCaption(active);
    }
}

void LevelCellSettingsPopup::onDragCaptionHidden() {
    if (m_dragCaptionPill) m_dragCaptionPill->setVisible(false);
}

bool LevelCellSettingsPopup::init() {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    this->setTitle("LevelCell Settings");
    this->addCorners();
    this->addInfoButton("LevelCell Settings",
        "Customize how level cells look in lists.\n"
        "<cy>Background</c>: style, thumbnail size, blur, darkness and quick presets.\n"
        "<cy>Display</c>: separator, view button, compact and transparent lists.\n"
        "<cy>Hover</c>: animation, speed and color effect.\n"
        "Drag any slider and the popup fades out so you can <cg>preview the list live</c>.");

    m_bgTypes = {"gradient", "legacy-gradient", "thumbnail"};
    m_animTypes = {
        "none", "zoom-slide", "zoom", "slide", "bounce",
        "rotate", "rotate-content", "shake", "pulse", "swing"
    };
    m_animEffects = {
        "none", "brightness", "darken", "sepia", "red", "blue", "gold",
        "fade", "grayscale", "blur", "invert", "glitch", "sharpen",
        "edge-detection", "vignette", "pixelate", "posterize", "chromatic",
        "scanlines", "solarize", "rainbow"
    };

    loadSettings();
    m_tab = std::clamp(static_cast<int>(Mod::get()->getSavedValue<int64_t>(kSavedTab, 0)), 0, 2);

    auto const content = m_mainLayer->getContentSize();
    float const scrollW = content.width - kScrollX * 2.f - kScrollbarW - 4.f;

    auto* tabs = kit::makeTabBar(scrollW, {"Background", "Display", "Hover"}, m_tab,
        [this](int tab) {
            m_tab = tab;
            Mod::get()->setSavedValue<int64_t>(kSavedTab, tab);
            scheduleRebuild();
        });
    tabs->setPosition({kScrollX, content.height - 40.f - kit::kTabBarHeight + kTabsGap});
    tabs->setID("levelcell-tabs"_spr);
    m_mainLayer->addChild(tabs, 5);

    auto* bottomMenu = CCMenu::create();
    bottomMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(bottomMenu, 5);

    auto* resetBtn = ui::makeButton("Reset", [this] {
        WeakRef<LevelCellSettingsPopup> self = this;
        geode::createQuickPopup("Reset LevelCell",
            "Restore every <cy>LevelCell</c> setting to its default value?",
            "Cancel", "Reset",
            [self](FLAlertLayer*, bool confirmed) {
                if (!confirmed) return;
                if (auto popup = self.lock()) popup->resetToDefaults();
            });
    }, ui::Btn::Red, 96.f, 0.6f);
    resetBtn->setID("levelcell-reset-btn"_spr);
    resetBtn->setPosition({content.width * 0.5f - 56.f, 22.f});
    bottomMenu->addChild(resetBtn);

    auto* doneBtn = ui::makeButton("Done", [this] { this->onClose(nullptr); }, ui::Btn::Green, 96.f, 0.6f);
    doneBtn->setID("levelcell-done-btn"_spr);
    doneBtn->setPosition({content.width * 0.5f + 56.f, 22.f});
    bottomMenu->addChild(doneBtn);

    if (auto pill = ui::makeInset({190.f, 24.f}, 200)) {
        pill->setAnchorPoint({0.5f, 0.5f});
        pill->setPosition({content.width / 2.f, content.height + 24.f});
        pill->setVisible(false);
        m_mainLayer->addChild(pill, 50);
        m_dragCaptionPill = pill;

        m_dragCaptionLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_dragCaptionLabel->setScale(0.38f);
        m_dragCaptionLabel->setColor(ui::palette::gold);
        m_dragCaptionLabel->setPosition({95.f, 12.f});
        pill->addChild(m_dragCaptionLabel);
    }

    rebuild();

    this->schedule(schedule_selector(LevelCellSettingsPopup::checkDragState), 0.f);

    paimon::markDynamicPopup(this);
    return true;
}

void LevelCellSettingsPopup::scheduleRebuild(bool keepScroll) {
    WeakRef<LevelCellSettingsPopup> self = this;
    Loader::get()->queueInMainThread([self, keepScroll] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (auto popup = self.lock(); popup && popup->getParent()) popup->rebuild(keepScroll);
    });
}

void LevelCellSettingsPopup::rebuild(bool keepScroll) {
    if (m_dragHiding) applyDragVisibility(nullptr);
    m_activeDragSlider = nullptr;
    m_sliderRows.clear();

    float scrollY = 0.f;
    bool const restore = keepScroll && m_scrollLayer;
    if (restore) scrollY = m_scrollLayer->m_contentLayer->getPositionY();
    if (m_scrollLayer) m_scrollLayer->removeFromParent();
    if (m_scrollbar) m_scrollbar->removeFromParent();
    m_scrollLayer = nullptr;
    m_scrollbar = nullptr;

    auto const content = m_mainLayer->getContentSize();
    float const scrollW = content.width - kScrollX * 2.f - kScrollbarW - 4.f;
    float const scrollTop = content.height - 40.f - kit::kTabBarHeight;
    float const scrollH = scrollTop - kScrollY;

    std::vector<CCNode*> items;
    switch (m_tab) {
        case 1: items = buildDisplayTab(scrollW); break;
        case 2: items = buildHoverTab(scrollW); break;
        default: items = buildBackgroundTab(scrollW); break;
    }

    m_scrollLayer = kit::makeScrollStack({scrollW, scrollH}, items, 6.f);
    m_scrollLayer->setPosition({kScrollX, kScrollY});
    m_scrollLayer->setID("levelcell-scroll"_spr);
    m_mainLayer->addChild(m_scrollLayer, 4);

    if (restore) {
        float const minY = scrollH - m_scrollLayer->m_contentLayer->getContentSize().height;
        m_scrollLayer->m_contentLayer->setPositionY(std::clamp(scrollY, std::min(minY, 0.f), 0.f));
    }

    if (auto* bar = Scrollbar::create(m_scrollLayer)) {
        bar->setContentSize({kScrollbarW, scrollH - 4.f});
        bar->setPosition({kScrollX + scrollW + 4.f + kScrollbarW * 0.5f, kScrollY + scrollH * 0.5f});
        m_mainLayer->addChild(bar, 5);
        m_scrollbar = bar;
    }
}

CCNode* LevelCellSettingsPopup::trackedSlider(float width, char const* title, char const* desc,
    double value, double minV, double maxV, int precision, std::function<void(double)> onChange) {
    Slider* slider = nullptr;
    CCLabelBMFont* valueLabel = nullptr;
    auto* row = kit::makeSliderRow(width, title, desc, value, minV, maxV, fixedFormat(precision),
        std::move(onChange), &slider, &valueLabel);
    if (slider) m_sliderRows.push_back({slider, valueLabel, title});
    return row;
}

std::vector<CCNode*> LevelCellSettingsPopup::buildBackgroundTab(float width) {
    float const inner = kit::cardInnerWidth(width);

    std::vector<std::string> bgNames;
    for (auto const& type : m_bgTypes) bgNames.push_back(getBgTypeDisplayName(type));

    std::vector<CCNode*> items;
    items.push_back(kit::makeCard(width, "Style", {255, 190, 100}, {
        kit::makeSelectRow(inner, "Background Style",
            "Gradient blurs the thumbnail, Legacy uses two colors, Thumbnail shows the image.",
            bgNames, m_bgTypeIndex,
            [this](int index) {
                m_bgTypeIndex = std::clamp(index, 0, static_cast<int>(m_bgTypes.size()) - 1);
                m_currentBgType = m_bgTypes[static_cast<size_t>(m_bgTypeIndex)];
                saveSettings();
            }),
        kit::makeToggleRow(inner, "Animated Gradient", "Color-shifting animation on gradient backgrounds.",
            m_animatedGradient,
            [this](bool on) { m_animatedGradient = on; saveSettings(); }),
    }));

    items.push_back(kit::makeCard(width, "Thumbnail", {120, 210, 255}, {
        trackedSlider(inner, "Thumbnail Size", "How much of the cell width the thumbnail covers.",
            m_currentThumbWidth, 0.2, 0.95, 2,
            [this](double v) { m_currentThumbWidth = static_cast<float>(v); saveSettings(); }),
        trackedSlider(inner, "Edge Blend", "Blends the diagonal edge into the background. 0 is a hard cut.",
            m_currentEdgeBlend, 0.0, 1.0, 2,
            [this](double v) { m_currentEdgeBlend = static_cast<float>(v); saveSettings(); }),
    }));

    items.push_back(kit::makeCard(width, "Background", {180, 150, 255}, {
        trackedSlider(inner, "Blur", "0 is sharp, 10 is the strongest blur.",
            m_currentBlur, 0.0, 10.0, 1,
            [this](double v) { m_currentBlur = static_cast<float>(v); saveSettings(); }),
        trackedSlider(inner, "Darkness", "Dark overlay drawn over the background.",
            m_currentDarkness, 0.0, 1.0, 2,
            [this](double v) { m_currentDarkness = static_cast<float>(v); saveSettings(); }),
    }));

    {
        constexpr float kRowH = 34.f;
        constexpr float kGap = 6.f;
        auto* row = CCNode::create();
        row->setAnchorPoint({0.f, 0.f});
        row->setContentSize({inner, kRowH});
        row->addChild(ui::makeInset({inner, kRowH}, kit::kRowAlpha), -1);

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(childTouchPrio());
        row->addChild(menu, 5);

        struct Preset { char const* name; float blur, darkness, edge; ui::Btn skin; };
        Preset const presets[] = {
            {"Default", Defaults::blur, Defaults::darkness, Defaults::edgeBlend, ui::Btn::Gray},
            {"Soft",  6.0f, 0.30f, 0.85f, ui::Btn::Blue},
            {"Sharp", 0.5f, 0.10f, 0.30f, ui::Btn::Cyan},
            {"Dark",  4.0f, 0.55f, 0.70f, ui::Btn::Pink},
        };
        int const count = static_cast<int>(std::size(presets));
        float const buttonW = (inner - 16.f - kGap * (count - 1)) / count;
        for (int i = 0; i < count; ++i) {
            auto const p = presets[i];
            auto* btn = ui::makeButton(p.name, [this, p] { applyPreset(p.blur, p.darkness, p.edge); },
                p.skin, buttonW, 0.55f);
            btn->setPosition({8.f + buttonW * 0.5f + i * (buttonW + kGap), kRowH * 0.5f});
            menu->addChild(btn);
        }
        items.push_back(kit::makeCard(width, "Quick Presets", {255, 150, 210}, {
            row,
            kit::makeHint(inner, "Presets only change blur, darkness and edge blend."),
        }));
    }

    items.push_back(kit::makeHint(width,
        "Tip: drag a slider and the popup fades out so you can see the list update live."));
    return items;
}

std::vector<CCNode*> LevelCellSettingsPopup::buildDisplayTab(float width) {
    float const inner = kit::cardInnerWidth(width);
    auto refreshList = [] { paimon::thumbnails::refreshActiveLevelBrowserForCompactToggle(); };

    std::vector<CCNode*> items;
    items.push_back(kit::makeCard(width, "Cell Layout", {120, 210, 255}, {
        kit::makeToggleRow(inner, "Separator Line",
            "Thin line between the cell content and the thumbnail. Only visible with Edge Blend at 0.",
            m_showSeparator,
            [this](bool on) { m_showSeparator = on; saveSettings(); }),
        kit::makeToggleRow(inner, "View Button",
            "When off, the View button is hidden and the whole cell is clickable.",
            m_showViewButton,
            [this, refreshList](bool on) {
                m_showViewButton = on;
                saveSettings();
                // vanilla view button needs a full list rebuild.
                refreshList();
            }),
    }));

    items.push_back(kit::makeCard(width, "Lists", {150, 235, 170}, {
        kit::makeToggleRow(inner, "Compact Mode", "Shorter level cells in list views.",
            m_compactMode,
            [this, refreshList](bool on) { m_compactMode = on; saveSettings(); refreshList(); }),
        kit::makeToggleRow(inner, "Compact Toggle Button", "Quick compact-mode button in the level browser.",
            m_compactShowQuickToggle,
            [this](bool on) { m_compactShowQuickToggle = on; saveSettings(); }),
        kit::makeToggleRow(inner, "Transparent Lists", "Transparent list cell backgrounds.",
            m_transparentMode,
            [this, refreshList](bool on) {
                m_transparentMode = on;
                saveSettings();
                // transparent mode restructures cells: rebuild.
                refreshList();
            }),
    }));

    items.push_back(kit::makeCard(width, "Extras", {255, 210, 100}, {
        kit::makeToggleRow(inner, "Mythic Particles", "Particles on Mythic and Legendary rated levels.",
            m_mythicParticles,
            [this](bool on) { m_mythicParticles = on; saveSettings(); }),
    }));
    return items;
}

std::vector<CCNode*> LevelCellSettingsPopup::buildHoverTab(float width) {
    float const inner = kit::cardInnerWidth(width);

    std::vector<std::string> animNames;
    for (auto const& type : m_animTypes) animNames.push_back(getAnimTypeDisplayName(type));
    std::vector<std::string> effectNames;
    for (auto const& effect : m_animEffects) effectNames.push_back(getAnimEffectDisplayName(effect));

    std::vector<CCNode*> items;
    items.push_back(kit::makeHeroToggle(width, "Hover Animation",
        "Animate level cells while the mouse is over them.",
        m_hoverEffects,
        [this](bool on) { m_hoverEffects = on; saveSettings(); }));

    items.push_back(kit::makeCard(width, "Animation", {120, 210, 255}, {
        kit::makeSelectRow(inner, "Type", "Movement played when hovering a cell.",
            animNames, m_animTypeIndex,
            [this](int index) {
                m_animTypeIndex = std::clamp(index, 0, static_cast<int>(m_animTypes.size()) - 1);
                m_currentAnimType = m_animTypes[static_cast<size_t>(m_animTypeIndex)];
                saveSettings();
            }),
        kit::makeSliderRow(inner, "Speed", "How fast the hover animation plays.",
            m_currentAnimSpeed, 0.1, 5.0, fixedFormat(1, "x"),
            [this](double v) { m_currentAnimSpeed = static_cast<float>(v); saveSettings(); }),
    }));

    items.push_back(kit::makeCard(width, "Color Effect", {255, 150, 210}, {
        kit::makeSelectRow(inner, "Effect", "Color or visual filter applied while hovering.",
            effectNames, m_animEffectIndex,
            [this](int index) {
                m_animEffectIndex = std::clamp(index, 0, static_cast<int>(m_animEffects.size()) - 1);
                m_currentAnimEffect = m_animEffects[static_cast<size_t>(m_animEffectIndex)];
                saveSettings();
            }),
        kit::makeToggleRow(inner, "Apply to Background", "Also apply the effect to the gradient background.",
            m_effectOnGradient,
            [this](bool on) { m_effectOnGradient = on; saveSettings(); }),
    }));
    return items;
}

void LevelCellSettingsPopup::applyPreset(float blur, float darkness, float edgeBlend) {
    m_currentBlur = blur;
    m_currentDarkness = darkness;
    m_currentEdgeBlend = edgeBlend;
    saveSettings();
    scheduleRebuild(true);
}

void LevelCellSettingsPopup::resetToDefaults() {
    bool const needsListRebuild = !m_showViewButton || !m_compactMode || m_transparentMode;

    m_currentBgType = Defaults::bgType;
    m_currentThumbWidth = Defaults::thumbWidth;
    m_currentBlur = Defaults::blur;
    m_currentDarkness = Defaults::darkness;
    m_currentEdgeBlend = Defaults::edgeBlend;
    m_showSeparator = true;
    m_showViewButton = true;
    m_compactMode = true;
    m_compactShowQuickToggle = true;
    m_transparentMode = false;
    m_hoverEffects = true;
    m_currentAnimType = Defaults::animType;
    m_currentAnimSpeed = Defaults::animSpeed;
    m_currentAnimEffect = Defaults::animEffect;
    m_effectOnGradient = false;
    m_mythicParticles = true;
    m_animatedGradient = true;
    m_bgTypeIndex = indexOf(m_bgTypes, m_currentBgType);
    m_animTypeIndex = indexOf(m_animTypes, m_currentAnimType);
    m_animEffectIndex = indexOf(m_animEffects, m_currentAnimEffect);

    saveSettings();
    if (needsListRebuild) paimon::thumbnails::refreshActiveLevelBrowserForCompactToggle();
    scheduleRebuild();
    PaimonNotify::show("LevelCell settings restored", NotificationIcon::Success);
}

LevelCellSettingsPopup* LevelCellSettingsPopup::create() {
    auto ret = new LevelCellSettingsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
