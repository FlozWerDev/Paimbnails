#include "DynamicTransitionConfigPopup.hpp"
#include "DynamicTransitionScene.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include <Geode/ui/NineSlice.hpp>
#include <fmt/format.h>
#include <array>
#include <limits>

using namespace geode::prelude;

namespace paimon::transitions::dynamic {
namespace {
namespace kit = paimon::configkit;
namespace ui = paimon::ui;

constexpr float kHold = .55f;
constexpr float kStageBox = 114.f;
constexpr float kGraphHeight = 56.f;
constexpr float kCaptionGap = 20.f;
constexpr float kColumn = 220.f;
constexpr float kColumnWidth = 246.f;

char const* text(char const* spanish, char const* english) {
    return Localization::get().getLanguage() == Localization::Language::SPANISH ? spanish : english;
}

std::vector<std::string> styleNames() {
    return {text("App desde boton", "App from button"), text("Tarjeta", "Card"), text("Deslizar", "Slide"),
        "Zoom", text("Empujar", "Push"), text("Hoja", "Sheet"), text("Fundido", "Fade"),
        text("Revelado circular", "Circular reveal")};
}

std::vector<std::string> curveNames() {
    return {text("Fluida", "Smooth"), text("Resorte", "Spring"), text("Suave (S)", "Soft (S)"),
        text("Lineal", "Linear"), text("Exponencial", "Exponential"), text("Enfatizada", "Emphasized")};
}

std::vector<std::string> originNames() {
    return {text("Boton pulsado", "Pressed button"), text("Centro", "Center"), text("Abajo", "Bottom"),
        text("Izquierda", "Left"), text("Derecha", "Right"), text("Arriba", "Top")};
}

std::vector<std::string> presetNames() {
    return {text("Equilibrado", "Balanced"), text("Rapido", "Snappy"), text("Sedoso", "Silky"),
        text("Resorte", "Springy"), text("Cinematico", "Cinematic"), text("Hoja", "Sheet"),
        text("Empujar", "Push"), text("Revelar", "Reveal"), text("Minimo", "Minimal")};
}

std::vector<std::string> panelStyleNames() {
    auto names = styleNames();
    names.insert(names.begin(), text("Igual que layers", "Same as layers"));
    return names;
}

template<class Enum>
std::string nameOf(std::vector<std::string> const& names, Enum value) {
    auto index = static_cast<size_t>(value);
    return index < names.size() ? names[index] : std::string();
}

Config previewConfig(Config raw, bool panel, bool& instant) {
    instant = false;
    auto config = animationConfig(raw);
    if (panel) config = panelConfig(config);
    if (paimon::settings::smoothui::reducedMotion()) {
        if (config.reducedMotion == ReducedMotion::Instant) instant = true;
        else config = reducedMotionConfig(config);
    }
    return config;
}

void roundedFill(CCDrawNode* draw, CCSize size, float radius) {
    constexpr int kSteps = 6;
    std::array<CCPoint, (kSteps + 1) * 4> points;
    radius = std::clamp(radius, 0.f, std::min(size.width, size.height) / 2.f);
    std::array<CCPoint, 4> centers{{{size.width - radius, radius}, {size.width - radius, size.height - radius},
        {radius, size.height - radius}, {radius, radius}}};
    size_t count = 0;
    for (int corner = 0; corner < 4; ++corner) {
        for (int step = 0; step <= kSteps; ++step) {
            float angle = (static_cast<float>(corner - 1) + static_cast<float>(step) / kSteps) * kPi * .5f;
            points[count++] = {centers[corner].x + radius * std::cos(angle), centers[corner].y + radius * std::sin(angle)};
        }
    }
    draw->drawPolygon(points.data(), static_cast<unsigned>(count), {1.f, 1.f, 1.f, 1.f}, 0.f, {0.f, 0.f, 0.f, 0.f});
}

CCSprite* frameSprite(char const* name) {
    return paimon::SpriteHelper::safeCreateWithFrameName(name);
}

CCLabelBMFont* label(char const* value, char const* font, float scale, CCPoint position, CCNode* parent, int z = 0) {
    auto* node = CCLabelBMFont::create(value, font);
    node->setScale(scale);
    node->setPosition(position);
    parent->addChild(node, z);
    return node;
}

// Demo screens are laid out at window size so the miniature keeps real proportions.
CCNode* menuScreen(CCSize size, Rect& button) {
    auto* root = CCNode::create();
    root->setContentSize(size);
    if (auto* background = geode::createLayerBG()) {
        background->setColor({0, 102, 255});
        root->addChild(background, -2);
    }
    for (bool right : {false, true}) {
        if (auto* art = frameSprite("GJ_sideArt_001.png")) {
            art->setFlipX(right);
            art->setAnchorPoint({right ? 1.f : 0.f, 0.f});
            art->setPosition({right ? size.width : 0.f, 0.f});
            root->addChild(art, -1);
        }
    }
    auto* ground = CCLayerColor::create({0, 0, 0, 70}, size.width, 44.f);
    root->addChild(ground, -1);
    label("Paimbnails", "goldFont.fnt", 1.1f, {size.width / 2.f, size.height - 62.f}, root);
    struct Entry { char const* frame; float offset; };
    std::array<Entry, 3> entries{{{"GJ_garageBtn_001.png", -112.f}, {"GJ_playBtn_001.png", 0.f},
        {"GJ_creatorBtn_001.png", 112.f}}};
    float y = size.height / 2.f - 8.f;
    button = {size.width / 2.f - 136.f, y - 24.f, 48.f, 48.f};
    for (auto const& entry : entries) {
        CCSprite* sprite = frameSprite(entry.frame);
        if (!sprite) sprite = ui::makeCircleSprite("GJ_starsIcon_001.png", geode::CircleBaseColor::Green,
            entry.offset == 0.f ? geode::CircleBaseSize::Large : geode::CircleBaseSize::Big);
        if (!sprite) continue;
        CCPoint at{size.width / 2.f + entry.offset, y};
        sprite->setPosition(at);
        root->addChild(sprite);
        if (entry.offset < 0.f) {
            auto box = sprite->getContentSize() * sprite->getScale();
            button = {at.x - box.width / 2.f, at.y - box.height / 2.f, box.width, box.height};
        }
    }
    return root;
}

CCNode* layerScreen(CCSize size, bool hint) {
    auto* root = CCNode::create();
    root->setContentSize(size);
    if (auto* background = geode::createLayerBG()) {
        background->setColor({120, 60, 200});
        root->addChild(background, -2);
    }
    label(text("Nuevo layer", "New layer"), "goldFont.fnt", .85f, {size.width / 2.f, size.height - 26.f}, root);
    if (auto* arrow = frameSprite("GJ_arrow_01_001.png")) {
        arrow->setScale(.85f);
        arrow->setPosition({28.f, size.height - 28.f});
        root->addChild(arrow);
    }
    constexpr std::array<ccColor3B, 5> kTints{{{255, 210, 90}, {120, 230, 160}, {110, 210, 255},
        {255, 130, 170}, {200, 160, 255}}};
    float cell = 54.f, gap = 12.f;
    float left = size.width / 2.f - (cell * 5.f + gap * 4.f) / 2.f;
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 5; ++column) {
            auto* slot = ui::makeInset({cell, cell}, 110);
            slot->setPosition({left + column * (cell + gap), size.height / 2.f + 8.f - row * (cell + gap)});
            root->addChild(slot);
            auto tint = kTints[static_cast<size_t>((column + row * 2) % 5)];
            auto* icon = CCLayerColor::create({tint.r, tint.g, tint.b, 255}, 26.f, 26.f);
            icon->setPosition({14.f, 14.f});
            slot->addChild(icon, 1);
        }
    }
    auto* bar = ui::makeInset({size.width - 140.f, 30.f}, 100);
    bar->setPosition({70.f, 22.f});
    root->addChild(bar);
    if (hint) {
        auto* note = label(text("Toca o pulsa Esc para volver", "Tap or press Esc to go back"), "chatFont.fnt", .7f,
            {size.width / 2.f, 37.f}, root, 1);
        note->setColor(ui::palette::muted);
    }
    return root;
}

void addPanel(CCNode* root, CCSize size) {
    root->addChild(CCLayerColor::create({0, 0, 0, 105}, size.width, size.height), 10);
    auto* window = NineSlice::create("GJ_square01.png");
    window->setContentSize({270.f, 160.f});
    window->setAnchorPoint({.5f, .5f});
    window->setPosition(size / 2.f);
    root->addChild(window, 11);
    label("Popup", "goldFont.fnt", .8f, {135.f, 134.f}, window);
    auto* message = label(text("Asi se abren los paneles.", "This is how panels open."), "chatFont.fnt", .7f,
        {135.f, 88.f}, window);
    message->limitLabelWidth(240.f, .7f, .3f);
    if (auto* ok = ui::makeButtonSprite("OK", ui::Btn::Green, 0.f, .8f)) {
        ok->setPosition({135.f, 32.f});
        window->addChild(ok);
    }
}

Ref<CCTexture2D> render(CCNode* node, CCSize design, CCSize target, float quality) {
    node->setScaleX(target.width / design.width);
    node->setScaleY(target.height / design.height);
    return Visual::capture(node, target, quality);
}

class FullscreenTest : public CCLayer {
public:
    static FullscreenTest* create(Config const& config, bool panel, Rect origin) {
        auto* layer = new FullscreenTest();
        if (layer->setup(config, panel, origin)) {
            layer->autorelease();
            return layer;
        }
        delete layer;
        return nullptr;
    }

    void registerWithTouchDispatcher() override {
        auto* dispatcher = CCDirector::get()->getTouchDispatcher();
        dispatcher->addTargetedDelegate(this, dispatcher->getTargetPrio() - 30, true);
    }

    bool ccTouchBegan(CCTouch*, CCEvent*) override {
        if (m_phase == 1) advance();
        return true;
    }

    void keyBackClicked() override {
        removeFromParentAndCleanup(true);
    }

    void keyDown(enumKeyCodes key, double) override {
        if (key == KEY_Escape) keyBackClicked();
    }

    void update(float dt) override {
        auto* director = CCDirector::get();
        if (paimon::isRuntimeShuttingDown() || director->getNextScene() || !director->getWinSize().equals(m_size)) {
            removeFromParentAndCleanup(true);
            return;
        }
        m_time += frameStep(dt, m_first);
        m_first = false;
        if (m_time >= duration()) {
            if (m_phase == 2) {
                removeFromParentAndCleanup(true);
                return;
            }
            advance();
        }
        present();
    }

    void onExit() override {
        unscheduleAllSelectors();
        CCLayer::onExit();
    }

private:
    bool setup(Config const& raw, bool panel, Rect origin) {
        if (!CCLayer::init()) return false;
        auto* director = CCDirector::get();
        auto* scene = director->getRunningScene();
        m_size = director->getWinSize();
        if (!scene || m_size.width < 8.f || m_size.height < 8.f) return false;
        bool instant = false;
        auto config = previewConfig(raw, panel, instant);
        auto before = Visual::capture(scene, m_size, config.quality);
        if (!before) return false;
        Ref<CCTexture2D> after;
        if (panel) {
            auto* base = CCNode::create();
            base->setContentSize(m_size);
            auto* image = CCSprite::createWithTexture(before);
            if (!image) return false;
            image->setFlipY(true);
            image->setAnchorPoint({0.f, 0.f});
            image->setScaleX(m_size.width / image->getContentSize().width);
            image->setScaleY(m_size.height / image->getContentSize().height);
            base->addChild(image);
            addPanel(base, m_size);
            after = Visual::capture(base, m_size, config.quality);
        } else {
            after = Visual::capture(layerScreen(m_size, true), m_size, config.quality);
        }
        if (!after) return false;
        bool morph = config.origin == Origin::Button;
        origin = morph ? normalizeOrigin(origin, m_size.width, m_size.height) :
            fallbackOrigin(m_size.width, m_size.height, config.origin);
        if (instant) {
            m_open = Visual::create(after, nullptr, m_size, config, origin, false, false, panel);
            m_back = Visual::create(before, nullptr, m_size, config, origin, true, false, panel);
        } else {
            m_open = Visual::create(before, after, m_size, config, origin, false, morph, panel);
            m_back = config.animateBack ?
                Visual::create(after, before, m_size, config, origin, true, morph, panel) :
                Visual::create(before, nullptr, m_size, config, origin, true, false, panel);
        }
        if (!m_open || !m_back) return false;
        m_openDuration = instant ? .05f : config.duration;
        m_backDuration = instant || !config.animateBack ? .05f : config.backDuration;
        addChild(m_open);
        addChild(m_back);
        setContentSize(m_size);
        setID("dynamic-transition-fullscreen-test"_spr);
        setTouchEnabled(true);
        setKeypadEnabled(true);
        setKeyboardEnabled(true);
        scheduleUpdate();
        present();
        return true;
    }

    float duration() const {
        return m_phase == 0 ? m_openDuration : m_phase == 1 ? 1.6f : m_backDuration;
    }

    void advance() {
        m_phase = std::min(m_phase + 1, 2);
        m_time = 0.f;
    }

    void present() {
        bool opening = m_phase < 2;
        float progress = m_phase == 1 ? 1.f : std::clamp(m_time / duration(), 0.f, 1.f);
        m_open->setVisible(opening);
        m_back->setVisible(!opening);
        (opening ? m_open : m_back)->setProgress(progress);
    }

    Visual* m_open = nullptr;
    Visual* m_back = nullptr;
    CCSize m_size;
    float m_time = 0.f, m_openDuration = .42f, m_backDuration = .32f;
    int m_phase = 0;
    bool m_first = true;
};

}

ConfigPreview* ConfigPreview::create(float width) {
    auto* node = new ConfigPreview();
    if (node->setup(width)) {
        node->autorelease();
        return node;
    }
    delete node;
    return nullptr;
}

bool ConfigPreview::setup(float width) {
    if (!CCNode::init()) return false;
    auto win = CCDirector::get()->getWinSize();
    float aspect = win.height / std::max(1.f, win.width);
    float innerWidth = width - 8.f, innerHeight = kStageBox - 8.f;
    m_stage = {innerWidth, innerWidth * aspect};
    if (m_stage.height > innerHeight) m_stage = {innerHeight / aspect, innerHeight};
    float stageBottom = kGraphHeight + kCaptionGap;
    setContentSize({width, stageBottom + kStageBox});

    auto* frame = ui::makeInset({width, kStageBox}, 140);
    frame->setPosition({0.f, stageBottom});
    addChild(frame);
    auto* stencil = PaimonDrawNode::create();
    if (!stencil) return false;
    roundedFill(stencil, m_stage, 5.f);
    auto* clip = CCClippingNode::create(stencil);
    if (!clip) return false;
    clip->setContentSize(m_stage);
    clip->setPosition({(width - m_stage.width) / 2.f, stageBottom + (kStageBox - m_stage.height) / 2.f});
    addChild(clip, 1);
    m_clip = clip;

    m_caption = label("", "chatFont.fnt", .5f, {width / 2.f, kGraphHeight + kCaptionGap / 2.f}, this);
    m_caption->setColor(ui::palette::muted);
    addChild(ui::makeInset({width, kGraphHeight}, 120));
    m_curve = PaimonDrawNode::create();
    m_marker = PaimonDrawNode::create();
    if (!m_curve || !m_marker) return false;
    addChild(m_curve, 1);
    addChild(m_marker, 2);
    m_curveName = label("", "chatFont.fnt", .42f, {width - 8.f, 5.f}, this, 3);
    m_curveName->setAnchorPoint({1.f, 0.f});
    m_curveName->setColor(ui::palette::muted);
    scheduleUpdate();
    return true;
}

void ConfigPreview::captureScreens() {
    m_quality = m_config.quality;
    auto win = CCDirector::get()->getWinSize();
    Rect button;
    auto* menu = menuScreen(win, button);
    float sx = m_stage.width / win.width, sy = m_stage.height / win.height;
    m_button = {button.x * sx, button.y * sy, button.width * sx, button.height * sy};
    m_menu = render(menu, win, m_stage, m_quality);
    m_layer = render(layerScreen(win, false), win, m_stage, m_quality);
    Rect unused;
    auto* panel = menuScreen(win, unused);
    addPanel(panel, win);
    m_panel = render(panel, win, m_stage, m_quality);
}

void ConfigPreview::setConfig(Config const& config) {
    m_config = config;
    if (std::abs(m_quality - m_config.quality) > .01f || !m_menu || !m_layer || !m_panel) captureScreens();
    rebuildVisuals();
}

void ConfigPreview::setPanelMode(bool panel) {
    if (m_panelMode == panel) return;
    m_panelMode = panel;
    rebuildVisuals();
}

void ConfigPreview::setLooping(bool looping) {
    m_looping = looping;
    if (looping) m_playing = true;
}

void ConfigPreview::play(bool backwards) {
    m_phase = backwards ? 2 : 0;
    m_time = 0.f;
    m_looping = false;
    m_playing = true;
    m_firstStep = true;
    present();
}

void ConfigPreview::rebuildVisuals() {
    if (m_open) m_open->removeFromParent();
    if (m_back) m_back->removeFromParent();
    m_open = m_back = nullptr;
    m_motion = previewConfig(m_config, m_panelMode, m_instant);
    m_phase = 0;
    m_time = 0.f;
    m_playing = true;
    m_firstStep = true;
    if (m_menu && m_layer && m_panel) {
        auto to = m_panelMode ? m_panel : m_layer;
        bool morph = m_motion.origin == Origin::Button;
        Rect origin = morph ? m_button : fallbackOrigin(m_stage.width, m_stage.height, m_motion.origin);
        if (m_instant) {
            m_open = Visual::create(to, nullptr, m_stage, m_motion, origin, false, false, m_panelMode);
            m_back = Visual::create(m_menu, nullptr, m_stage, m_motion, origin, true, false, m_panelMode);
        } else {
            m_open = Visual::create(m_menu, to, m_stage, m_motion, origin, false, morph, m_panelMode);
            m_back = m_motion.animateBack ?
                Visual::create(to, m_menu, m_stage, m_motion, origin, true, morph, m_panelMode) :
                Visual::create(m_menu, nullptr, m_stage, m_motion, origin, true, false, m_panelMode);
        }
        for (auto* visual : {m_open, m_back}) {
            if (visual) m_clip->addChild(visual);
        }
    }

    std::string caption;
    if (m_panelMode) caption = text("Panel  |  ", "Panel  |  ");
    if (m_instant) {
        caption += text("Movimiento reducido: instantaneo", "Reduced motion: instant");
    } else {
        if (paimon::settings::smoothui::reducedMotion()) caption += text("Movimiento reducido  |  ", "Reduced motion  |  ");
        caption += fmt::format("{}  |  {:.2f}s / {:.2f}s", nameOf(styleNames(), m_motion.style),
            m_motion.duration, m_motion.backDuration);
    }
    m_caption->setString(caption.c_str());
    m_caption->limitLabelWidth(getContentSize().width - 8.f, .5f, .2f);
    m_curveName->setString(nameOf(curveNames(), m_motion.curve).c_str());
    drawCurve();
    present();
}

CCPoint ConfigPreview::graphPoint(float t, float value) const {
    float width = getContentSize().width;
    float range = std::max(.001f, m_curveHigh - m_curveLow);
    return {10.f + t * (width - 20.f), 9.f + (value - m_curveLow) / range * (kGraphHeight - 18.f)};
}

void ConfigPreview::drawCurve() {
    constexpr int kSamples = 64;
    std::array<float, kSamples + 1> values;
    m_curveLow = 0.f;
    m_curveHigh = 1.f;
    for (int i = 0; i <= kSamples; ++i) {
        values[static_cast<size_t>(i)] = ease(static_cast<float>(i) / kSamples, m_motion.curve, m_motion.spring);
        m_curveLow = std::min(m_curveLow, values[static_cast<size_t>(i)]);
        m_curveHigh = std::max(m_curveHigh, values[static_cast<size_t>(i)]);
    }
    m_curve->clear();
    for (float level : {0.f, 1.f})
        m_curve->drawSegment(graphPoint(0.f, level), graphPoint(1.f, level), .4f, {1.f, 1.f, 1.f, .12f});
    for (int i = 1; i <= kSamples; ++i) {
        m_curve->drawSegment(graphPoint(static_cast<float>(i - 1) / kSamples, values[static_cast<size_t>(i - 1)]),
            graphPoint(static_cast<float>(i) / kSamples, values[static_cast<size_t>(i)]), .9f, {.45f, .85f, 1.f, .95f});
    }
}

float ConfigPreview::phaseDuration(int phase) const {
    if (phase == 0) return m_instant ? .05f : m_motion.duration;
    if (phase == 2) return m_instant || !m_motion.animateBack ? .05f : m_motion.backDuration;
    return kHold;
}

void ConfigPreview::present() {
    bool opening = m_phase < 2;
    bool animating = m_phase == 0 || m_phase == 2;
    float progress = animating ? std::clamp(m_time / phaseDuration(m_phase), 0.f, 1.f) : 1.f;
    if (m_open) {
        m_open->setVisible(opening);
        if (opening) m_open->setProgress(progress);
    }
    if (m_back) {
        m_back->setVisible(!opening);
        if (!opening) m_back->setProgress(progress);
    }
    auto dot = graphPoint(progress, ease(progress, m_motion.curve, m_motion.spring));
    m_marker->clear();
    m_marker->drawSegment({dot.x, 9.f}, {dot.x, kGraphHeight - 9.f}, .35f, {1.f, 1.f, 1.f, animating ? .2f : .08f});
    m_marker->drawSolidCircle(dot, 2.4f, {1.f, 1.f, 1.f, animating ? .95f : .45f}, 16);
}

void ConfigPreview::update(float dt) {
    if (!m_playing) return;
    m_time += frameStep(dt, m_firstStep) * (m_slow ? .3f : 1.f);
    m_firstStep = false;
    for (int guard = 0; guard < 4 && m_time >= phaseDuration(m_phase); ++guard) {
        m_time -= phaseDuration(m_phase);
        bool animation = m_phase == 0 || m_phase == 2;
        m_phase = (m_phase + 1) % 4;
        if (animation && !m_looping) {
            m_time = 0.f;
            m_playing = false;
            break;
        }
    }
    present();
}

DynamicTransitionConfigPopup* DynamicTransitionConfigPopup::create() {
    auto* popup = new DynamicTransitionConfigPopup();
    if (popup->init()) { popup->autorelease(); return popup; }
    delete popup;
    return nullptr;
}

bool DynamicTransitionConfigPopup::init() {
    if (!PaimonPopup::init(480.f, 300.f)) return false;
    setTitle("Dynamic Transition");
    addInfoButton(text("Transicion dinamica", "Dynamic Transition"),
        text("Cada layer se abre como una app desde el boton que lo lanzo.\n\n"
            "La <cg>vista previa</c> usa el mismo compositor que la transicion real y se repite sola. "
            "En la pestana <cy>Paneles</c> muestra como se abren los popups. "
            "<cy>Pantalla completa</c> prueba el efecto sobre esta pantalla, desde el boton pulsado.\n\n"
            "Los cambios se guardan al instante.",
            "Open each layer like an app from the button that launched it.\n\n"
            "The <cg>preview</c> uses the same compositor as the real transition and loops on its own. "
            "The <cy>Panels</c> tab shows how popups open. "
            "<cy>Fullscreen</c> tries the effect over this screen, starting from the pressed button.\n\n"
            "Changes save instantly."));
    paimon::markDynamicPopup(this);
    m_config = getConfig();
    m_preview = ConfigPreview::create(196.f);
    if (!m_preview) return false;
    m_preview->setPosition({14.f, 80.f});
    m_mainLayer->addChild(m_preview);
    m_preview->setConfig(m_config);
    buildPreviewControls();
    rebuild();
    return true;
}

void DynamicTransitionConfigPopup::buildPreviewControls() {
    auto add = [this](char const* caption, float x, float y, float width, ui::Btn skin, auto callback) {
        auto* button = ui::makeButton(caption, std::move(callback), skin, width, .5f, "bigFont.fnt");
        button->setPosition({x, y});
        m_buttonMenu->addChild(button);
        return button;
    };
    std::vector<CCMenuItemSpriteExtra*> row;
    row.push_back(add(text("Abrir", "Open"), 37.f, 63.f, 46.f, ui::Btn::Cyan, [this] {
        m_preview->play(false);
        refreshControlSkins();
    }));
    row.push_back(add(text("Volver", "Back"), 87.f, 63.f, 46.f, ui::Btn::Cyan, [this] {
        m_preview->play(true);
        refreshControlSkins();
    }));
    m_loopButton = add(text("Bucle", "Loop"), 137.f, 63.f, 46.f, ui::Btn::Green, [this] {
        m_preview->setLooping(!m_preview->looping());
        refreshControlSkins();
    });
    m_slowButton = add(text("Lento", "Slow"), 187.f, 63.f, 46.f, ui::Btn::Gray, [this] {
        m_preview->setSlow(!m_preview->slow());
        refreshControlSkins();
    });
    row.push_back(m_loopButton);
    row.push_back(m_slowButton);
    ui::matchButtonLabels(row);

    auto* fullscreen = CCMenuItemExt::createSpriteExtra(
        ui::makeButtonSprite(text("Pantalla completa", "Fullscreen"), ui::Btn::Cyan, 196.f, .6f, "bigFont.fnt"),
        [this](CCMenuItemSpriteExtra* button) { startFullscreenTest(button); });
    fullscreen->setPosition({112.f, 24.f});
    m_buttonMenu->addChild(fullscreen);

    add(text("Restaurar", "Reset"), kColumn + kColumnWidth / 2.f, 24.f, 110.f, ui::Btn::Red, [this] {
        WeakRef<DynamicTransitionConfigPopup> self = this;
        auto* alert = geode::createQuickPopup(text("Restaurar", "Reset"),
            text("Volver a los valores predeterminados de Dynamic Transition?", "Restore Dynamic Transition defaults?"),
            text("Cancelar", "Cancel"), text("Restaurar", "Reset"), [self](FLAlertLayer*, bool confirmed) {
                if (!confirmed) return;
                resetConfig();
                auto popup = self.lock();
                if (!popup) return;
                popup->m_config = getConfig();
                popup->m_preview->setConfig(popup->m_config);
                popup->scheduleRebuild();
            }, false);
        if (alert) kit::showAbove(alert, this);
    });
    refreshControlSkins();
}

void DynamicTransitionConfigPopup::refreshControlSkins() {
    if (m_loopButton) ui::setButtonSkin(m_loopButton, m_preview->looping() ? ui::Btn::Green : ui::Btn::Gray);
    if (m_slowButton) ui::setButtonSkin(m_slowButton, m_preview->slow() ? ui::Btn::Green : ui::Btn::Gray);
}

void DynamicTransitionConfigPopup::startFullscreenTest(CCMenuItemSpriteExtra* source) {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || !source) return;
    auto* test = FullscreenTest::create(m_config, m_preview->panelMode(), buttonRect(source));
    if (!test) {
        PaimonNotify::show(text("No se pudo capturar la pantalla.", "Could not capture the screen."),
            NotificationIcon::Warning);
        return;
    }
    scene->addChild(test, std::numeric_limits<int>::max() - 1);
}

void DynamicTransitionConfigPopup::persist() {
    saveConfig(m_config);
    m_config = getConfig();
    m_preview->setConfig(m_config);
    refreshPresetState();
}

void DynamicTransitionConfigPopup::scheduleRebuild() {
    if (m_rebuildQueued) return;
    m_rebuildQueued = true;
    Ref<DynamicTransitionConfigPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        self->m_rebuildQueued = false;
        if (!paimon::isRuntimeShuttingDown() && self->getParent()) self->rebuild();
    });
}

void DynamicTransitionConfigPopup::choosePreset(int preset) {
    m_config = applyPreset(m_config, preset);
    persist();
    scheduleRebuild();
}

void DynamicTransitionConfigPopup::refreshPresetState() {
    int match = matchPreset(m_config);
    for (size_t i = 0; i < m_presetButtons.size(); ++i)
        ui::setButtonSkin(m_presetButtons[i], static_cast<int>(i) == match ? ui::Btn::Green : ui::Btn::Gray);
    ui::matchButtonLabels(m_presetButtons);
    if (!m_presetLabel) return;
    auto names = presetNames();
    auto current = match >= 0 ? names[static_cast<size_t>(match)] : std::string(text("Personalizado", "Custom"));
    m_presetLabel->setString(fmt::format("{} {}", text("Actual:", "Current:"), current).c_str());
}

void DynamicTransitionConfigPopup::rebuild() {
    float keptOffset = 0.f;
    bool keepScroll = m_scroll && m_scrollTab == m_tab;
    if (keepScroll) {
        auto* content = m_scroll->m_contentLayer;
        keptOffset = content->getPositionY() - (m_scroll->getContentSize().height - content->getContentSize().height);
    }
    if (m_scroll) { m_scroll->removeFromParent(); m_scroll = nullptr; }
    if (m_header) { m_header->removeFromParent(); m_header = nullptr; }
    m_presetButtons.clear();
    m_presetLabel = nullptr;

    float width = kColumnWidth, inner = kit::cardInnerWidth(width);
    auto seconds = [](double value) { return fmt::format("{:.2f}s", value); };
    auto percent = [](double value) { return fmt::format("{:.0f}%", value * 100.); };

    m_header = CCNode::create();
    auto* hero = kit::makeHeroToggle(width, "Dynamic Transition", nullptr, m_config.enabled,
        [this](bool value) { m_config.enabled = value; persist(); });
    hero->setPosition({kColumn, 228.f});
    m_header->addChild(hero);
    auto* tabs = kit::makeTabBar(width, {text("Estilo", "Style"), text("Ritmo", "Timing"),
        text("Alcance", "Scope"), text("Paneles", "Panels")}, m_tab,
        [this](int value) { m_tab = value; scheduleRebuild(); });
    tabs->setPosition({kColumn, 198.f});
    m_header->addChild(tabs);
    m_mainLayer->addChild(m_header);

    std::vector<CCNode*> items;
    if (m_tab == 0) {
        constexpr int kColumns = 3;
        constexpr float kGap = 4.f, kRow = 22.f;
        auto names = presetNames();
        int rows = (static_cast<int>(names.size()) + kColumns - 1) / kColumns;
        float gridHeight = rows * kRow + (rows - 1) * kGap + 26.f;
        auto* grid = CCNode::create();
        grid->setContentSize({inner, gridHeight});
        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
        grid->addChild(menu);
        float buttonWidth = (inner - kGap * (kColumns - 1)) / kColumns;
        for (size_t i = 0; i < names.size(); ++i) {
            int column = static_cast<int>(i) % kColumns, row = static_cast<int>(i) / kColumns;
            auto* button = ui::makeButton(names[i].c_str(), [this, i] { choosePreset(static_cast<int>(i)); },
                ui::Btn::Gray, buttonWidth, .55f, "bigFont.fnt");
            button->setPosition({column * (buttonWidth + kGap) + buttonWidth / 2.f,
                gridHeight - row * (kRow + kGap) - kRow / 2.f});
            menu->addChild(button);
            m_presetButtons.push_back(button);
        }
        ui::matchButtonLabels(m_presetButtons);
        m_presetLabel = label("", "chatFont.fnt", .5f, {inner / 2.f, 9.f}, grid);
        m_presetLabel->setColor(ui::palette::muted);
        items.push_back(kit::makeCard(width, text("Presets", "Presets"), {255, 200, 100}, {grid}));
        items.push_back(kit::makeCard(width, text("Forma", "Shape"), {115, 210, 255}, {
            kit::makeSelectRow(inner, text("Estilo", "Style"),
                text("Como entra el nuevo layer. Deslizar y Empujar usan el origen como borde.",
                    "How the new layer enters. Slide and Push use the origin as their edge."),
                styleNames(), static_cast<int>(m_config.style),
                [this](int value) { m_config.style = static_cast<Style>(value); persist(); }),
            kit::makeSelectRow(inner, text("Origen", "Origin"), text("Punto desde el que aparece el nuevo layer.", "Where the new layer starts."),
                originNames(), static_cast<int>(m_config.origin),
                [this](int value) { m_config.origin = static_cast<Origin>(value); persist(); }),
        }));
        items.push_back(kit::makeCard(width, text("Apariencia", "Appearance"), {190, 165, 255}, {
            kit::makeSliderRow(inner, text("Esquinas redondeadas", "Rounded corners"), text("Se enderezan al llenar la pantalla.", "They straighten as the layer fills the screen."),
                m_config.cornerRadius, 0., 48., [](double value) { return fmt::format("{:.0f}", value); },
                [this](double value) { m_config.cornerRadius = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(inner, text("Fusion del boton", "Button blend"), text("Estilo App: cuanto tarda el boton en dar paso al layer.", "App style: how long the button takes to blend into the layer."),
                m_config.buttonBlend, 0., .6, percent, [this](double value) { m_config.buttonBlend = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(inner, text("Sombra", "Shadow"), text("Sombra suave bajo el layer en movimiento.", "Soft shadow under the moving layer."),
                m_config.shadow, 0., 1., percent, [this](double value) { m_config.shadow = static_cast<float>(value); persist(); }),
        }));
        items.push_back(kit::makeCard(width, text("Fondo", "Background"), {130, 235, 175}, {
            kit::makeSliderRow(inner, text("Oscurecimiento", "Dimming"), text("Oscurece la pantalla anterior.", "Dims the previous screen."),
                m_config.dim, 0., .65, percent, [this](double value) { m_config.dim = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(inner, text("Profundidad", "Depth"), text("Escala de la pantalla anterior mientras se aleja.", "Scale of the previous screen as it moves back."),
                m_config.backgroundScale, .85, 1., percent, [this](double value) { m_config.backgroundScale = static_cast<float>(value); persist(); }),
        }));
    } else if (m_tab == 1) {
        items.push_back(kit::makeCard(width, text("Duracion", "Duration"), {130, 235, 175}, {
            kit::makeSliderRow(inner, text("Apertura", "Opening"), text("Tiempo para entrar al nuevo layer.", "Time to enter the new layer."),
                m_config.duration, .12, 1.5, seconds, [this](double value) { m_config.duration = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(inner, text("Regreso", "Return"), text("Tiempo para volver a la pantalla anterior.", "Time to return to the previous screen."),
                m_config.backDuration, .12, 1.5, seconds, [this](double value) { m_config.backDuration = static_cast<float>(value); persist(); }),
        }));
        items.push_back(kit::makeCard(width, text("Curva", "Curve"), {115, 210, 255}, {
            kit::makeSelectRow(inner, text("Aceleracion", "Easing"), text("La grafica de la vista previa muestra la curva elegida.", "The preview graph plots the chosen curve."),
                curveNames(), static_cast<int>(m_config.curve),
                [this](int value) { m_config.curve = static_cast<Curve>(value); persist(); }),
            kit::makeSliderRow(inner, text("Rebote", "Bounce"), text("Curva Resorte: cuanto se pasa antes de asentarse.", "Spring curve: how far it overshoots before settling."),
                m_config.spring, 0., .25, percent, [this](double value) { m_config.spring = static_cast<float>(value); persist(); }),
            kit::makeToggleRow(inner, text("Sincronizar con Smooth UI", "Sync with Smooth UI"), text("Usa su velocidad general y fuerza del movimiento.", "Uses its global speed and motion strength."),
                m_config.syncSmoothUI, [this](bool value) { m_config.syncSmoothUI = value; persist(); }),
        }));
        items.push_back(kit::makeHint(width, text("Lento reproduce la vista previa a un tercio de velocidad para revisar cada fotograma.",
            "Slow plays the preview at a third of the speed to inspect every frame.")));
    } else if (m_tab == 2) {
        items.push_back(kit::makeCard(width, text("Navegacion", "Navigation"), {255, 205, 125}, {
            kit::makeToggleRow(inner, text("Animar regreso", "Animate return"), text("Recoge el layer hacia el boton que lo abrio.", "Folds the layer toward the button that opened it."),
                m_config.animateBack, [this](bool value) { m_config.animateBack = value; persist(); }),
            kit::makeToggleRow(inner, text("Escape y Volver", "Escape and Back"), text("Anima el regreso con Escape o el boton Volver de Android.", "Animates returns with Escape or Android's Back button."),
                m_config.animateKeyboardBack, [this](bool value) { m_config.animateKeyboardBack = value; persist(); }),
            kit::makeToggleRow(inner, text("Navegacion instantanea", "Instant navigation"), text("Anima tambien los layers que abren sin transicion.", "Also animates layers that open without a transition."),
                m_config.includeInstant, [this](bool value) { m_config.includeInstant = value; persist(); }),
            kit::makeToggleRow(inner, text("Solo desde botones", "Only from buttons"), text("Omite aperturas automaticas o por teclado sin origen.", "Skips automatic or keyboard openings without an origin."),
                m_config.onlyFromButton, [this](bool value) { m_config.onlyFromButton = value; persist(); }),
        }));
        items.push_back(kit::makeCard(width, text("Rendimiento y accesibilidad", "Performance and accessibility"), {190, 165, 255}, {
            kit::makeSelectRow(inner, text("Movimiento reducido", "Reduced motion"), text("Cuando esta activo en Smooth UI, reemplaza el efecto.", "Replaces the effect when reduced motion is on in Smooth UI."),
                {text("Fundido corto", "Short fade"), text("Cambio instantaneo", "Instant switch")},
                static_cast<int>(m_config.reducedMotion),
                [this](int value) { m_config.reducedMotion = static_cast<ReducedMotion>(value); persist(); }),
            kit::makeSelectRow(inner, text("Calidad", "Quality"), text("Resolucion de las capturas animadas.", "Resolution of the animated captures."),
                {text("Alta", "High"), text("Equilibrada", "Balanced"), text("Rendimiento", "Performance")},
                m_config.quality > .875f ? 0 : m_config.quality > .625f ? 1 : 2,
                [this](int value) { m_config.quality = value == 0 ? 1.f : value == 1 ? .75f : .5f; persist(); }),
        }));
        items.push_back(kit::makeHint(width, text("Escape, la flecha, la X y los botones de cierre usan el origen recordado. Los cambios de escena de niveles y editor conservan su configuracion.",
            "Escape, arrows, X and close buttons use the remembered origin. Level and editor scene changes keep their settings.")));
    } else {
        items.push_back(kit::makeCard(width, text("Estilo de paneles", "Panel style"), {255, 200, 100}, {
            kit::makeSelectRow(inner, text("Animacion", "Animation"), text("Los paneles pueden usar un estilo distinto al de los layers.", "Panels can use a different style from layers."),
                panelStyleNames(), static_cast<int>(m_config.panelStyle),
                [this](int value) { m_config.panelStyle = static_cast<PanelStyle>(value); persist(); }),
        }));
        items.push_back(kit::makeCard(width, text("Tipos de panel", "Panel types"), {150, 210, 255}, {
            kit::makeToggleRow(inner, text("Popups del juego", "Game popups"), text("Avisos, perfiles, comentarios, opciones y ventanas de edicion.", "Alerts, profiles, comments, options and editing windows."),
                m_config.animatePopups, [this](bool value) { m_config.animatePopups = value; persist(); }),
            kit::makeToggleRow(inner, text("Paneles deslizantes", "Sliding panels"), text("Opciones, resultados y otros paneles desplegables.", "Options, results and other dropdown panels."),
                m_config.animateDropdowns, [this](bool value) { m_config.animateDropdowns = value; persist(); }),
            kit::makeToggleRow(inner, text("Paneles de pausa", "Blocking panels"), text("Pausa y paneles que bloquean los controles del fondo.", "Pause and panels that block the controls behind them."),
                m_config.animateBlockingLayers, [this](bool value) { m_config.animateBlockingLayers = value; persist(); }),
            kit::makeToggleRow(inner, text("Dialogos", "Dialogs"), text("Entrada y salida de conversaciones con personajes.", "Opening and closing character conversations."),
                m_config.animateDialogs, [this](bool value) { m_config.animateDialogs = value; persist(); }),
        }));
        items.push_back(kit::makeCard(width, text("Donde animar", "Where to animate"), {190, 165, 255}, {
            kit::makeToggleRow(inner, text("Dentro del editor", "Inside the editor"), text("Anima sus ventanas de ajustes y triggers.", "Animates its settings and trigger windows."),
                m_config.animateEditorPanels, [this](bool value) { m_config.animateEditorPanels = value; persist(); }),
            kit::makeToggleRow(inner, text("Durante niveles", "During levels"), text("Anima pausa, resultados y avisos sobre el nivel.", "Animates pause, results and alerts over the level."),
                m_config.animateGameplayPanels, [this](bool value) { m_config.animateGameplayPanels = value; persist(); }),
            kit::makeToggleRow(inner, text("Popups de otros mods", "Other mods' popups"), text("Permite el efecto en ventanas creadas con Geode.", "Allows the effect in windows created with Geode."),
                m_config.animateOtherMods, [this](bool value) { m_config.animateOtherMods = value; persist(); }),
        }));
        items.push_back(kit::makeHint(width, text("La vista previa muestra un popup mientras esta pestana esta abierta. Los popups propios de Paimon usan Dynamic Popups.",
            "The preview shows a popup while this tab is open. Paimon's own popups use Dynamic Popups.")));
    }
    m_scroll = kit::makeScrollStack({width, 156.f}, items, 6.f);
    m_scroll->setPosition({kColumn, 38.f});
    if (keepScroll) {
        auto* content = m_scroll->m_contentLayer;
        float top = m_scroll->getContentSize().height - content->getContentSize().height;
        content->setPositionY(std::clamp(top + keptOffset, top, 0.f));
    }
    m_scrollTab = m_tab;
    m_mainLayer->addChild(m_scroll);
    m_preview->setPanelMode(m_tab == 3);
    refreshPresetState();
}

}
