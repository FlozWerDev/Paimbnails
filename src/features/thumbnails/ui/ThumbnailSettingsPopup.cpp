#include "ThumbnailSettingsPopup.hpp"

#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../visuals/ui/ExtraEffectsPopup.hpp"
#include "LocalThumbnailViewPopup.hpp"

using namespace geode::prelude;

namespace {
namespace kit = paimon::configkit;

int findIndex(std::vector<std::string> const& values, std::string const& value) {
    auto it = std::find(values.begin(), values.end(), value);
    return it == values.end() ? -1 : static_cast<int>(std::distance(values.begin(), it));
}

class PeekButtonHandler : public CCNode {
public:
    ThumbnailSettingsPopup* m_popup = nullptr;

    static PeekButtonHandler* create(ThumbnailSettingsPopup* popup) {
        auto ret = new PeekButtonHandler();
        if (ret && ret->init()) {
            ret->m_popup = popup;
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void onPeekToggle(CCObject*) {
        if (m_popup) m_popup->togglePeek();
    }
};
}

bool ThumbnailSettingsPopup::init() {
    if (!Popup::init(420.f, 300.f)) return false;

    setTitle("Thumbnail Settings");

    m_allStyles = {
        "normal", "pixel", "blur", "paimonblur", "grayscale", "sepia",
        "vignette", "scanlines", "bloom", "chromatic", "radial-blur", "glitch",
        "posterize", "rain", "matrix", "neon-pulse", "wave-distortion", "crt",
        "shockwave", "vortex", "magnetic", "spotlight", "ripple", "plasma-cursor",
        "freeze", "pixelate-cursor", "kaleidoscope", "sonar", "electric-arc",
        "prism-split", "gravity-well", "shatter", "heat-haze", "liquify",
        "ink-spread", "hologram", "time-warp", "underwater", "neon-trail",
        "synthwave", "neon-city", "ocean", "galaxy"
    };
    m_styles = m_allStyles;

    m_popupTransitions = {
        "crossfade", "slide-left", "slide-right", "elastic-slide",
        "directional-elastic", "zoom-in", "zoom-out", "bounce",
        "flip-horizontal", "flip-vertical", "dissolve", "wave-slide",
        "card-flip", "spin-zoom"
    };
    m_bgTransitions = m_popupTransitions;
    m_popupStyles = {
        "paimonUI", "jelly", "spiral", "drop-bounce", "skew-pop", "elastic",
        "bounce", "slide-up", "slide-down", "slide-left", "slide-right",
        "zoom-fade", "flip", "fold", "pop-rotate", "elastic-drop",
        "glitch-shake", "card-turn", "fly-spin"
    };

    m_currentStyle = Mod::get()->getSavedValue<std::string>(
        "levelinfo-background-style-override", "");
    if (m_currentStyle.empty()) {
        m_currentStyle = Mod::get()->getSettingValue<std::string>(
            "levelinfo-background-style");
    }
    m_currentIntensity = std::clamp(
        Mod::get()->getSavedValue<int>("levelinfo-effect-intensity", 4), 1, 10);
    m_currentDarkness = std::clamp(
        Mod::get()->getSavedValue<int>("levelinfo-bg-darkness", 27), 0, 50);
    m_dynamicSong = Mod::get()->getSettingValue<bool>("dynamic-song");
    m_dynamicShaders = Mod::get()->getSavedValue<bool>(
        "levelinfo-dynamic-shaders", false);
    m_dynamicShadersDelay = std::clamp(Mod::get()->getSavedValue<float>(
        "levelinfo-dynamic-shaders-delay", 0.f), 0.f, 2.f);

    updateStylesForDynamicShaders();

    m_currentPopupTransition = Mod::get()->getSavedValue<std::string>(
        "popup-gallery-transition", "directional-elastic");
    m_currentBgTransition = Mod::get()->getSavedValue<std::string>(
        "levelinfo-bg-transition", "crossfade");
    m_currentPopupStyle = Mod::get()->getSavedValue<std::string>(
        "dynamic-popup-style", "paimonUI");
    m_dynamicPopup = Mod::get()->getSettingValue<bool>("dynamic-popup-enabled");
    m_currentPopupSpeed = std::clamp(Mod::get()->getSavedValue<double>(
        "dynamic-popup-speed", 1.0), 0.2, 3.0);
    m_dynamicExit = Mod::get()->getSettingValue<bool>("dynamic-exit-enabled");
    m_currentExitSpeed = std::clamp(Mod::get()->getSavedValue<double>(
        "dynamic-exit-speed", 1.0), 0.2, 3.0);

    m_popupTransitionIndex = findIndex(m_popupTransitions, m_currentPopupTransition);
    if (m_popupTransitionIndex < 0) {
        m_popupTransitionIndex = 0;
        m_currentPopupTransition = m_popupTransitions.front();
    }
    m_bgTransitionIndex = findIndex(m_bgTransitions, m_currentBgTransition);
    if (m_bgTransitionIndex < 0) {
        m_bgTransitionIndex = 0;
        m_currentBgTransition = m_bgTransitions.front();
    }
    m_popupStyleIndex = findIndex(m_popupStyles, m_currentPopupStyle);
    if (m_popupStyleIndex < 0) {
        m_popupStyleIndex = 0;
        m_currentPopupStyle = m_popupStyles.front();
    }

    paimon::markDynamicPopup(this);
    rebuild();
    addPeekButton();
    return true;
}

void ThumbnailSettingsPopup::scheduleRebuild() {
    retain();
    Loader::get()->queueInMainThread([this] {
        if (getParent()) rebuild();
        release();
    });
}

void ThumbnailSettingsPopup::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }

    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 44.f;
    float innerW = kit::cardInnerWidth(scrollW);

    auto seconds = [](double value) { return fmt::format("{:.2f}s", value); };
    auto speed = [](double value) { return fmt::format("x{:.2f}", value); };
    auto integer = [](double value) { return fmt::format("{:.0f}", std::round(value)); };

    auto tabs = kit::makeTabBar(
        scrollW, {"Background", "Transitions", "Popups"}, m_tab,
        [this](int tab) {
            m_tab = tab;
            scheduleRebuild();
        });

    std::vector<CCNode*> items = {tabs};

    if (m_tab == 0) {
        std::vector<std::string> styleNames;
        styleNames.reserve(m_styles.size());
        for (auto const& style : m_styles) {
            styleNames.push_back(getStyleDisplayName(style));
        }

        items.push_back(kit::makeHint(scrollW,
            "Choose how the level thumbnail looks behind the level information."));
        items.push_back(kit::makeCard(scrollW, "Appearance", {120, 210, 255}, {
            kit::makeSelectRow(innerW,
                "Background style",
                "Visual effect applied to the thumbnail.",
                std::move(styleNames), m_styleIndex,
                [this](int index) {
                    if (index < 0 || index >= static_cast<int>(m_styles.size())) return;
                    m_styleIndex = index;
                    m_currentStyle = m_styles[static_cast<size_t>(index)];
                    saveSettings();
                }),
            kit::makeSliderRow(innerW,
                "Effect strength",
                "How strongly the selected style is applied.",
                m_currentIntensity, 1.0, 10.0, integer,
                [this](double value) {
                    m_currentIntensity = std::clamp(
                        static_cast<int>(std::round(value)), 1, 10);
                    saveSettings();
                }),
            kit::makeSliderRow(innerW,
                "Background darkness",
                "Darkens busy images so level text stays readable.",
                m_currentDarkness, 0.0, 50.0, integer,
                [this](double value) {
                    m_currentDarkness = std::clamp(
                        static_cast<int>(std::round(value)), 0, 50);
                    saveSettings();
                }),
            kit::makeButtonRow(innerW,
                "Extra effects",
                "Combine the main style with optional overlays.",
                "Configure", [this] { openExtraEffects(); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Interactive background", {130, 240, 170}, {
            kit::makeToggleRow(innerW,
                "Play level song",
                "Starts the song preview when level info opens.",
                m_dynamicSong,
                [this](bool enabled) {
                    m_dynamicSong = enabled;
                    saveSettings();
                }),
            kit::makeToggleRow(innerW,
                "Interactive effects",
                "Effects follow the pointer or last tap. Shows compatible styles only.",
                m_dynamicShaders,
                [this](bool enabled) {
                    m_dynamicShaders = enabled;
                    updateStylesForDynamicShaders();
                    saveSettings();
                    scheduleRebuild();
                }),
            kit::makeSliderRow(innerW,
                "Response smoothing",
                "Used by interactive effects. 0s reacts instantly; more feels smoother.",
                m_dynamicShadersDelay, 0.0, 2.0, seconds,
                [this](double value) {
                    m_dynamicShadersDelay = std::clamp(
                        static_cast<float>(value), 0.f, 2.f);
                    Mod::get()->setSavedValue<float>(
                        "levelinfo-dynamic-shaders-delay", m_dynamicShadersDelay);
                    if (m_onSettingsChanged) m_onSettingsChanged();
                }),
        }));
        items.push_back(kit::makeHint(scrollW,
            "Use the button in the top-right corner to hide the menus and preview the result."));
    } else if (m_tab == 1) {
        std::vector<std::string> bgNames;
        std::vector<std::string> galleryNames;
        for (auto const& transition : m_bgTransitions) {
            bgNames.push_back(getTransitionDisplayName(transition));
        }
        for (auto const& transition : m_popupTransitions) {
            galleryNames.push_back(getTransitionDisplayName(transition));
        }

        auto bgDuration = std::clamp(Mod::get()->getSavedValue<float>(
            "levelinfo-bg-transition-duration", 0.5f), 0.15f, 1.5f);
        auto galleryDuration = std::clamp(Mod::get()->getSavedValue<float>(
            "popup-gallery-transition-duration", 0.45f), 0.15f, 1.5f);

        items.push_back(kit::makeHint(scrollW,
            "Background controls level info. Gallery controls thumbnail browsing."));
        items.push_back(kit::makeCard(scrollW, "Level background", {120, 210, 255}, {
            kit::makeSelectRow(innerW,
                "Change animation",
                "How one background moves into the next.",
                std::move(bgNames), m_bgTransitionIndex,
                [this](int index) {
                    if (index < 0 || index >= static_cast<int>(m_bgTransitions.size())) return;
                    m_bgTransitionIndex = index;
                    m_currentBgTransition = m_bgTransitions[static_cast<size_t>(index)];
                    saveSettings();
                }),
            kit::makeSliderRow(innerW,
                "Animation duration",
                "Time the change takes. Less time = faster.",
                bgDuration, 0.15, 1.5, seconds,
                [this](double value) {
                    Mod::get()->setSavedValue<float>(
                        "levelinfo-bg-transition-duration",
                        std::clamp(static_cast<float>(value), 0.15f, 1.5f));
                    if (m_onSettingsChanged) m_onSettingsChanged();
                }),
        }));
        items.push_back(kit::makeCard(scrollW, "Thumbnail gallery", {200, 180, 255}, {
            kit::makeSelectRow(innerW,
                "Browse animation",
                "How thumbnails change while browsing the gallery.",
                std::move(galleryNames), m_popupTransitionIndex,
                [this](int index) {
                    if (index < 0 || index >= static_cast<int>(m_popupTransitions.size())) return;
                    m_popupTransitionIndex = index;
                    m_currentPopupTransition = m_popupTransitions[static_cast<size_t>(index)];
                    saveSettings();
                }),
            kit::makeSliderRow(innerW,
                "Animation duration",
                "Time the change takes. Less time = faster.",
                galleryDuration, 0.15, 1.5, seconds,
                [this](double value) {
                    Mod::get()->setSavedValue<float>(
                        "popup-gallery-transition-duration",
                        std::clamp(static_cast<float>(value), 0.15f, 1.5f));
                    if (m_onSettingsChanged) m_onSettingsChanged();
                }),
        }));
    } else {
        std::vector<std::string> popupStyleNames;
        popupStyleNames.reserve(m_popupStyles.size());
        for (auto const& style : m_popupStyles) {
            popupStyleNames.push_back(getPopupStyleDisplayName(style));
        }

        items.push_back(kit::makeHint(scrollW,
            "These animations affect every Paimbnails popup, not only thumbnails."));
        items.push_back(kit::makeCard(scrollW, "Opening", {130, 240, 170}, {
            kit::makeToggleRow(innerW,
                "Animate when opening",
                "Makes popups enter with a custom animation.",
                m_dynamicPopup,
                [this](bool enabled) {
                    m_dynamicPopup = enabled;
                    saveSettings();
                }),
            kit::makeSelectRow(innerW,
                "Opening style",
                "The movement used when a popup appears.",
                std::move(popupStyleNames), m_popupStyleIndex,
                [this](int index) {
                    if (index < 0 || index >= static_cast<int>(m_popupStyles.size())) return;
                    m_popupStyleIndex = index;
                    m_currentPopupStyle = m_popupStyles[static_cast<size_t>(index)];
                    saveSettings();
                }),
            kit::makeSliderRow(innerW,
                "Opening speed",
                "Higher values make the popup open faster.",
                m_currentPopupSpeed, 0.2, 3.0, speed,
                [this](double value) {
                    m_currentPopupSpeed = std::clamp(value, 0.2, 3.0);
                    saveSettings();
                }),
        }));
        items.push_back(kit::makeCard(scrollW, "Closing", {255, 170, 120}, {
            kit::makeToggleRow(innerW,
                "Animate when closing",
                "Makes popups leave with a custom animation.",
                m_dynamicExit,
                [this](bool enabled) {
                    m_dynamicExit = enabled;
                    saveSettings();
                }),
            kit::makeSliderRow(innerW,
                "Closing speed",
                "Higher values make the popup close faster.",
                m_currentExitSpeed, 0.2, 3.0, speed,
                [this](double value) {
                    m_currentExitSpeed = std::clamp(value, 0.2, 3.0);
                    saveSettings();
                }),
        }));
    }

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 8.f});
    m_mainLayer->addChild(m_scroll);
}

void ThumbnailSettingsPopup::addPeekButton() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto handler = PeekButtonHandler::create(this);
    if (!handler) return;
    handler->setID("paimon-peek-handler"_spr);

    m_peekMenu = CCMenu::create();
    m_peekMenu->setPosition({0.f, 0.f});
    m_peekMenu->setID("paimon-peek-menu"_spr);
    handler->addChild(m_peekMenu);

    auto eye = paimon::SpriteHelper::safeCreateWithFrameName("GJ_sMagicIcon_001.png");
    if (!eye) eye = paimon::SpriteHelper::safeCreateWithFrameName("GJ_viewProfileTxt_001.png");
    if (!eye) eye = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
    if (eye) {
        auto button = CCMenuItemSpriteExtra::create(
            eye, handler, menu_selector(PeekButtonHandler::onPeekToggle));
        auto winSize = CCDirector::get()->getWinSize();
        button->setID("peek-toggle-btn"_spr);
        button->setPosition({winSize.width - 30.f, winSize.height - 30.f});
        m_peekMenu->addChild(button);
    }

    scene->addChild(handler, 99999);
}

void ThumbnailSettingsPopup::openExtraEffects() {
    auto popup = ExtraEffectsPopup::create();
    if (!popup) return;
    popup->setOnChanged(m_onSettingsChanged);
    popup->show();
}

void ThumbnailSettingsPopup::updateStylesForDynamicShaders() {
    static std::vector<std::string> const dynamicStyles = {
        "chromatic", "radial-blur", "glitch", "rain", "matrix", "neon-pulse",
        "wave-distortion", "crt", "shockwave", "vortex", "magnetic", "spotlight",
        "ripple", "plasma-cursor", "freeze", "pixelate-cursor", "kaleidoscope",
        "sonar", "electric-arc", "prism-split", "gravity-well", "shatter",
        "heat-haze", "liquify", "ink-spread", "hologram", "time-warp",
        "underwater", "neon-trail"
    };

    m_styles = m_dynamicShaders ? dynamicStyles : m_allStyles;
    m_styleIndex = findIndex(m_styles, m_currentStyle);
    if (m_styleIndex < 0 && !m_styles.empty()) {
        m_styleIndex = 0;
        m_currentStyle = m_styles.front();
    }
}

void ThumbnailSettingsPopup::saveSettings() {
    Mod::get()->setSettingValue<std::string>(
        "levelinfo-background-style", m_currentStyle);
    Mod::get()->setSavedValue<std::string>(
        "levelinfo-background-style-override", m_currentStyle);
    Mod::get()->setSavedValue<int>(
        "levelinfo-effect-intensity", m_currentIntensity);
    Mod::get()->setSavedValue<int>(
        "levelinfo-bg-darkness", m_currentDarkness);
    Mod::get()->setSettingValue<bool>("dynamic-song", m_dynamicSong);
    Mod::get()->setSavedValue<bool>(
        "levelinfo-dynamic-shaders", m_dynamicShaders);
    Mod::get()->setSavedValue<std::string>(
        "popup-gallery-transition", m_currentPopupTransition);
    Mod::get()->setSavedValue<std::string>(
        "levelinfo-bg-transition", m_currentBgTransition);
    Mod::get()->setSettingValue<bool>(
        "dynamic-popup-enabled", m_dynamicPopup);
    Mod::get()->setSavedValue<std::string>(
        "dynamic-popup-style", m_currentPopupStyle);
    Mod::get()->setSavedValue<double>(
        "dynamic-popup-speed", m_currentPopupSpeed);
    Mod::get()->setSettingValue<bool>(
        "dynamic-exit-enabled", m_dynamicExit);
    Mod::get()->setSavedValue<double>(
        "dynamic-exit-speed", m_currentExitSpeed);

    if (m_onSettingsChanged) m_onSettingsChanged();
}

void ThumbnailSettingsPopup::togglePeek() {
    m_peekMode = !m_peekMode;
    bool visible = !m_peekMode;
    setVisible(visible);
    setTouchEnabled(visible);

    auto scene = CCDirector::get()->getRunningScene();
    if (!scene || !scene->getChildren()) return;

    for (auto child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (!child || child == this) continue;
        if (auto popup = typeinfo_cast<LocalThumbnailViewPopup*>(child)) {
            popup->setVisible(visible);
            popup->setTouchEnabled(visible);
            break;
        }
    }
}

void ThumbnailSettingsPopup::onClose(CCObject* sender) {
    if (m_peekMode) togglePeek();

    if (auto scene = CCDirector::get()->getRunningScene()) {
        if (auto handler = scene->getChildByID("paimon-peek-handler"_spr)) {
            handler->removeFromParent();
        }
    }
    m_peekMenu = nullptr;
    Popup::onClose(sender);
}

std::string ThumbnailSettingsPopup::getTransitionDisplayName(
    std::string const& transition
) const {
    if (transition == "crossfade") return "Crossfade";
    if (transition == "slide-left") return "Slide Left";
    if (transition == "slide-right") return "Slide Right";
    if (transition == "elastic-slide") return "Elastic Slide";
    if (transition == "directional-elastic") return "Directional Elastic";
    if (transition == "zoom-in") return "Zoom In";
    if (transition == "zoom-out") return "Zoom Out";
    if (transition == "bounce") return "Bounce";
    if (transition == "flip-horizontal") return "Flip Horizontal";
    if (transition == "flip-vertical") return "Flip Vertical";
    if (transition == "dissolve") return "Dissolve";
    if (transition == "wave-slide") return "Wave Slide";
    if (transition == "card-flip") return "Card Flip";
    if (transition == "spin-zoom") return "Spin Zoom";
    return transition;
}

std::string ThumbnailSettingsPopup::getPopupStyleDisplayName(
    std::string const& style
) const {
    if (style == "paimonUI") return "Paimon UI";
    if (style == "jelly") return "Jelly Wobble";
    if (style == "spiral") return "Spiral Swirl";
    if (style == "drop-bounce") return "Drop Bounce";
    if (style == "skew-pop") return "Skew Pop";
    if (style == "elastic") return "Snappy Elastic";
    if (style == "bounce") return "Bouncy Physics";
    if (style == "slide-up") return "Slide Up";
    if (style == "slide-down") return "Slide Down";
    if (style == "slide-left") return "Slide Left";
    if (style == "slide-right") return "Slide Right";
    if (style == "zoom-fade") return "Zoom Fade";
    if (style == "flip") return "Card Flip Horizontal";
    if (style == "fold") return "Card Flip Vertical";
    if (style == "pop-rotate") return "Pop Rotate";
    if (style == "elastic-drop") return "Elastic Drop";
    if (style == "glitch-shake") return "Glitch Shake";
    if (style == "card-turn") return "Card Turn";
    if (style == "fly-spin") return "Fly Spin";
    return style;
}

std::string ThumbnailSettingsPopup::getStyleDisplayName(
    std::string const& style
) const {
    if (style == "normal") return "Normal";
    if (style == "pixel") return "Pixel";
    if (style == "blur") return "Blur";
    if (style == "paimonblur") return "Paimon Blur";
    if (style == "grayscale") return "Grayscale";
    if (style == "sepia") return "Sepia";
    if (style == "vignette") return "Vignette";
    if (style == "scanlines") return "Scanlines";
    if (style == "bloom") return "Bloom";
    if (style == "chromatic") return "Chromatic";
    if (style == "radial-blur") return "Radial Blur";
    if (style == "glitch") return "Glitch";
    if (style == "posterize") return "Posterize";
    if (style == "rain") return "Rain";
    if (style == "matrix") return "Matrix";
    if (style == "neon-pulse") return "Neon Pulse";
    if (style == "wave-distortion") return "Wave";
    if (style == "crt") return "CRT";
    if (style == "shockwave") return "Shockwave";
    if (style == "vortex") return "Vortex";
    if (style == "magnetic") return "Magnetic";
    if (style == "spotlight") return "Spotlight";
    if (style == "ripple") return "Ripple";
    if (style == "plasma-cursor") return "Plasma";
    if (style == "freeze") return "Freeze";
    if (style == "pixelate-cursor") return "Pixelate+";
    if (style == "kaleidoscope") return "Kaleidoscope";
    if (style == "sonar") return "Sonar";
    if (style == "electric-arc") return "Electric Arc";
    if (style == "prism-split") return "Prism Split";
    if (style == "gravity-well") return "Gravity Well";
    if (style == "shatter") return "Shatter";
    if (style == "heat-haze") return "Heat Haze";
    if (style == "liquify") return "Liquify";
    if (style == "ink-spread") return "Ink Spread";
    if (style == "hologram") return "Hologram";
    if (style == "time-warp") return "Time Warp";
    if (style == "underwater") return "Underwater";
    if (style == "neon-trail") return "Neon Trail";
    if (style == "synthwave") return "Synthwave";
    if (style == "neon-city") return "Neon City";
    if (style == "ocean") return "Ocean";
    if (style == "galaxy") return "Galaxy";
    return style;
}

ThumbnailSettingsPopup* ThumbnailSettingsPopup::create() {
    auto ret = new ThumbnailSettingsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}