#include "PhysicsPopup.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../smooth-scroll/services/SmoothScrollController.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../services/NativePreview.hpp"
#include "../services/PhysicsObjectArt.hpp"
#include "../services/PhysicsTriggerEmitter.hpp"
#include "PhysicsBodyPopup.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJGroundLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <ranges>
#include <utility>

using namespace geode::prelude;

namespace paimon::editorphysics {

namespace {

constexpr float kPopupWidth = 520.f;
constexpr float kPopupHeight = 315.f;
constexpr float kPreviewX = 16.f;
constexpr float kPreviewY = 91.f;
constexpr float kPreviewWidth = 235.f;
constexpr float kPreviewHeight = 137.f;
constexpr float kPreviewClipInset = 3.f;
constexpr float kCamFollow = 5.f;
constexpr float kTwoPi = 6.2831853071795864769f;
constexpr float kRadiansToDegrees = 57.29577951308232f;
constexpr float kMinZoom = 0.2f;
constexpr float kMaxZoom = 12.f;
constexpr float kZoomStep = 1.35f;
constexpr float kScrollZoomIn = 1.14f;
constexpr float kScrollZoomOut = 0.88f;
constexpr float kWorldGroundWidth = 30000.f;
constexpr float kWorldGroundHeight = 6000.f;
constexpr int kCircleSegments = 18;
constexpr float kTapSlack = 6.f;

// A level whose background or ground colour is nearly black leaves the panel
// unreadable, so the darkest tints are lifted just enough to tell them apart.
ccColor3B liftDark(ccColor3B color) {
    unsigned char const peak = std::max({color.r, color.g, color.b});
    if (peak >= 70) return color;
    if (peak < 10) return {70, 72, 90};
    float const gain = 70.f / static_cast<float>(peak);
    return {
        static_cast<GLubyte>(std::min(255.f, color.r * gain)),
        static_cast<GLubyte>(std::min(255.f, color.g * gain)),
        static_cast<GLubyte>(std::min(255.f, color.b * gain)),
    };
}

// Picking runs on the fixture's bounds: at preview scale that is as precise as
// a finger gets, and it still tells two stacked objects apart.
bool containsPoint(Fixture const& fixture, CCPoint point, float slack) {
    float const dx = std::abs(point.x - fixture.offset.x);
    float const dy = std::abs(point.y - fixture.offset.y);
    if (fixture.radius > 0.f) {
        float const reach = fixture.radius + slack;
        return dx * dx + dy * dy <= reach * reach;
    }
    return dx <= fixture.halfSize.x + slack && dy <= fixture.halfSize.y + slack;
}

CCMenuItemSpriteExtra* textButton(
    CCMenu* menu,
    char const* text,
    float x,
    float y,
    int width,
    char const* texture,
    std::function<void()> callback
) {
    auto* sprite = ButtonSprite::create(
        text, width, true, "bigFont.fnt", texture, 24.f, 0.55f
    );
    sprite->setScale(0.72f);
    auto* button = CCMenuItemExt::createSpriteExtra(
        sprite, [callback = std::move(callback)](CCMenuItemSpriteExtra*) {
            if (callback) callback();
        }
    );
    button->setPosition({x, y});
    menu->addChild(button);
    return button;
}

CCLabelBMFont* smallLabel(CCNode* parent, CCPoint position, ccColor3B color) {
    auto* label = CCLabelBMFont::create("-", "bigFont.fnt");
    label->setScale(0.29f);
    label->setColor(color);
    label->setPosition(position);
    parent->addChild(label);
    return label;
}

std::size_t liveObjectCount(CapturedBody const& body) {
    return std::ranges::count_if(body.objects, [](auto const& object) {
        auto locked = object.lock();
        return locked && locked->getParent();
    });
}

std::string objectIDSummary(ResolvedBody const& body) {
    std::vector<int> ids;
    ids.reserve(body.visuals.size());
    for (auto const& visual : body.visuals) {
        if (visual.objectID > 0) ids.push_back(visual.objectID);
    }
    std::ranges::sort(ids);
    auto const unique = std::ranges::unique(ids);
    ids.erase(unique.begin(), unique.end());
    if (ids.empty()) return "sin ID";

    std::string text = ids.size() == 1 ? "ID " : "IDs ";
    std::size_t const shown = std::min<std::size_t>(ids.size(), 3);
    for (std::size_t i = 0; i < shown; ++i) {
        if (i) text += ",";
        text += fmt::format("{}", ids[i]);
    }
    if (shown < ids.size()) text += fmt::format(" +{}", ids.size() - shown);
    return text;
}

} // namespace

PhysicsPopup* PhysicsPopup::create() {
    if (!paimon::modules::isEnabled("paimbnails.physics.editor")) return nullptr;
    auto* popup = new PhysicsPopup();
    if (popup && popup->init()) {
        popup->autorelease();
        return popup;
    }
    CC_SAFE_DELETE(popup);
    return nullptr;
}

bool PhysicsPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;
    setID("physics-lab-popup"_spr);
    setTitle("Simulador de Fisicas");
    m_config = loadConfig();

    auto* previewPanel = paimon::SpriteHelper::createDarkPanel(
        kPreviewWidth, kPreviewHeight, 220, 5.f
    );
    previewPanel->setPosition({kPreviewX, kPreviewY});
    m_mainLayer->addChild(previewPanel);

    float const clipWidth = kPreviewWidth - kPreviewClipInset * 2.f;
    float const clipHeight = kPreviewHeight - kPreviewClipInset * 2.f;
    auto* previewClip = CCClippingNode::create();
    previewClip->setPosition({kPreviewX + kPreviewClipInset, kPreviewY + kPreviewClipInset});
    previewClip->setContentSize({clipWidth, clipHeight});
    m_mainLayer->addChild(previewClip, 2);

    auto* stencil = CCLayerColor::create({255, 255, 255, 255}, clipWidth, clipHeight);
    previewClip->setStencil(stencil);

    buildPreviewScenery(previewClip, clipWidth, clipHeight);

    m_previewWorld = CCNode::create();
    previewClip->addChild(m_previewWorld, 3);

    m_focusLabel = CCLabelBMFont::create("Vista: todos | x1.0", "goldFont.fnt");
    m_focusLabel->setScale(0.32f);
    m_focusLabel->setPosition({kPreviewX + kPreviewWidth * 0.5f, kPreviewY + kPreviewHeight - 11.f});
    m_mainLayer->addChild(m_focusLabel, 6);

    m_bodyALabel = smallLabel(m_mainLayer, {128.f, 267.f}, {120, 235, 255});
    m_otherBodiesLabel = smallLabel(m_mainLayer, {111.f, 249.f}, {255, 190, 95});

    auto* hint = CCLabelBMFont::create(
        "Configura cada cuerpo con engranaje: triggers en vivo o keyframes horneados.",
        "bigFont.fnt"
    );
    hint->setScale(0.235f);
    hint->setColor({155, 170, 200});
    hint->setPosition({133.f, 235.f});
    hint->limitLabelWidth(230.f, 0.235f, 0.16f);
    m_mainLayer->addChild(hint);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);
    WeakRef<PhysicsPopup> self = this;

    for (int direction : {-1, 1}) {
        auto* arrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        arrow->setScale(0.26f);
        if (direction < 0) arrow->setFlipX(true);
        auto* focusButton = CCMenuItemExt::createSpriteExtra(
            arrow, [self, direction](CCMenuItemSpriteExtra*) {
                if (auto popup = self.lock()) popup->cycleFocus(direction);
            }
        );
        focusButton->setPosition({
            direction < 0 ? kPreviewX + 13.f : kPreviewX + kPreviewWidth - 13.f,
            kPreviewY + kPreviewHeight - 11.f,
        });
        menu->addChild(focusButton);
    }

    struct ViewControl {
        char const* frame;
        float y;
        float factor;
    };
    // The strip between the preview and the option column, so the controls never
    // cover the simulation.
    for (auto const& control : std::array<ViewControl, 3>{{
        {"GJ_zoomInBtn_001.png", 200.f, kZoomStep},
        {"GJ_zoomOutBtn_001.png", 180.f, 1.f / kZoomStep},
        {"GJ_resetBtn_001.png", 160.f, 0.f},
    }}) {
        auto* sprite = paimon::SpriteHelper::safeCreateWithFrameName(control.frame);
        if (!sprite) continue;
        limitNodeSize(sprite, {15.f, 15.f}, 1.f, 0.05f);
        float const factor = control.factor;
        auto* button = CCMenuItemExt::createSpriteExtra(
            sprite, [self, factor](CCMenuItemSpriteExtra*) {
                auto popup = self.lock();
                if (!popup) return;
                if (factor > 0.f) popup->adjustZoom(factor);
                else popup->resetView();
            }
        );
        button->setPosition({261.f, control.y});
        menu->addChild(button);
    }

    auto* hitboxToggle = CCMenuItemExt::createTogglerWithStandardSprites(
        0.3f, [self](CCMenuItemToggler*) {
            if (auto popup = self.lock()) popup->toggleHitboxes();
        }
    );
    hitboxToggle->setPosition({261.f, 140.f});
    menu->addChild(hitboxToggle);

    if (auto* gravity = paimon::SpriteHelper::safeCreateWithFrameName("GJ_gravityBtn_001.png")) {
        limitNodeSize(gravity, {15.f, 15.f}, 1.f, 0.05f);
        auto* motionButton = CCMenuItemExt::createSpriteExtra(
            gravity, [self](CCMenuItemSpriteExtra*) {
                if (auto popup = self.lock()) popup->toggleFocusedMotion();
            }
        );
        motionButton->setPosition({261.f, 120.f});
        menu->addChild(motionButton);
    }

    if (auto* gear = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png")) {
        limitNodeSize(gear, {15.f, 15.f}, 1.f, 0.05f);
        auto* editButton = CCMenuItemExt::createSpriteExtra(
            gear, [self](CCMenuItemSpriteExtra*) {
                if (auto popup = self.lock()) popup->openBodyEditor();
            }
        );
        editButton->setPosition({261.f, 100.f});
        menu->addChild(editButton);
    }

    textButton(menu, "Elegir A", 47.f, 73.f, 70, "GJ_button_04.png", [self] {
        if (auto popup = self.lock()) popup->beginCapture(CaptureRole::ReplaceA);
    });
    textButton(menu, "Elegir B", 103.f, 73.f, 70, "GJ_button_05.png", [self] {
        if (auto popup = self.lock()) popup->beginCapture(CaptureRole::ReplaceB);
    });
    textButton(menu, "+ Din", 158.f, 73.f, 62, "GJ_button_04.png", [self] {
        if (auto popup = self.lock()) popup->beginCapture(CaptureRole::AddDynamic);
    });
    textButton(menu, "+ Fijo", 211.f, 73.f, 62, "GJ_button_05.png", [self] {
        if (auto popup = self.lock()) popup->beginCapture(CaptureRole::AddStatic);
    });

    m_bodyModeSprite = ButtonSprite::create(
        "B: fijo", 78, true, "bigFont.fnt", "GJ_button_05.png", 22.f, 0.5f
    );
    m_bodyModeSprite->setScale(0.48f);
    auto* bodyModeButton = CCMenuItemExt::createSpriteExtra(
        m_bodyModeSprite, [self](CCMenuItemSpriteExtra*) {
            if (auto popup = self.lock()) popup->toggleBMotion();
        }
    );
    bodyModeButton->setPosition({225.f, 249.f});
    menu->addChild(bodyModeButton);

    char const* optionNames[] = {
        "Gravedad", "Rebote", "Friccion", "Arrastre", "Duracion",
        "Velocidad X", "Velocidad Y", "Giro inicial", "Calidad",
    };
    for (int field = 0; field < 9; ++field) {
        float const y = 259.f - field * 22.f;
        auto* name = CCLabelBMFont::create(optionNames[field], "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->setScale(0.3f);
        name->setPosition({274.f, y});
        m_mainLayer->addChild(name);

        m_valueLabels[static_cast<std::size_t>(field)] = smallLabel(
            m_mainLayer, {429.f, y}, {255, 220, 110}
        );
        for (int direction : {-1, 1}) {
            auto* sprite = ButtonSprite::create(
                direction < 0 ? "-" : "+", "bigFont.fnt", "GJ_button_04.png", 0.8f
            );
            sprite->setScale(0.42f);
            auto* button = CCMenuItemExt::createSpriteExtra(
                sprite, [self, field, direction](CCMenuItemSpriteExtra*) {
                    if (auto popup = self.lock()) popup->adjust(field, direction);
                }
            );
            button->setPosition({direction < 0 ? 397.f : 485.f, y});
            menu->addChild(button);
        }
    }

    textButton(menu, "Limpiar", 61.f, 39.f, 75, "GJ_button_06.png", [self] {
        if (auto popup = self.lock()) popup->clearBodies();
    });
    textButton(menu, "Previsualizar", 154.f, 39.f, 110, "GJ_button_04.png", [self] {
        if (auto popup = self.lock()) popup->preview();
    });
    textButton(menu, "Compilar GD", 269.f, 39.f, 100, "GJ_button_01.png", [self] {
        if (auto popup = self.lock()) popup->bake();
    });
    textButton(menu, "Quitar ultimo", 399.f, 39.f, 115, "GJ_button_06.png", [self] {
        if (auto popup = self.lock()) popup->removeLast();
    });

    m_statusLabel = smallLabel(m_mainLayer, {260.f, 15.f}, {185, 200, 225});
    m_statusLabel->setScale(0.265f);
    m_statusLabel->limitLabelWidth(475.f, 0.265f, 0.16f);

    setMouseEnabled(true);
    refreshBodies();
    refreshValues();
    if (auto captured = PhysicsWorkspace::get().resolve(editorUI(), m_config); captured.isOk()) {
        m_resolved = captured.unwrap();
        rebuildPreview();
        updateFocusLabel();
    }
    if (PhysicsWorkspace::get().empty()) {
        setStatus(
            "Sin cuerpos: pulsa Elegir A, selecciona los objetos en el editor y vuelve a abrir.",
            {255, 205, 95}
        );
    } else {
        setStatus(
            "Listo: toca un objeto para elegirlo y afinalo con el engranaje. "
            "Arrastra la vista, usa la rueda para el zoom y la casilla para las hitboxes.",
            {170, 225, 185}
        );
    }
    schedule(schedule_selector(PhysicsPopup::tick));
    return true;
}

void PhysicsPopup::onClose(CCObject* sender) {
    saveConfig(m_config);
    Popup::onClose(sender);
}

EditorUI* PhysicsPopup::editorUI() const {
    auto* editor = LevelEditorLayer::get();
    return editor ? editor->m_editorUI : nullptr;
}

void PhysicsPopup::beginCapture(CaptureRole role) {
    PhysicsWorkspace::get().beginCapture(role);
    char const* name = role == CaptureRole::ReplaceA ? "A" :
        role == CaptureRole::ReplaceB ? "B" :
        role == CaptureRole::AddDynamic ? "dinamico extra" : "fijo extra";
    PaimonNotify::show(
        fmt::format("Selecciona el cuerpo {} y vuelve a abrir Fisicas.", name),
        NotificationIcon::Info,
        4.f
    );
    onClose(nullptr);
}

void PhysicsPopup::toggleBMotion() {
    auto result = PhysicsWorkspace::get().toggleMotion(1);
    if (result.isErr()) {
        setStatus(result.unwrapErr(), {255, 190, 100});
        return;
    }
    m_playing = false;
    m_trace = {};
    m_resolved.clear();
    m_selectedObject = -1;
    rebuildPreview();
    refreshBodies();
    setStatus(
        result.unwrap() == Motion::Dynamic
            ? "B ahora es dinamico; su salida se elige desde el engranaje."
            : "B ahora funciona como colisionador fijo.",
        {170, 225, 185}
    );
}

void PhysicsPopup::clearBodies() {
    PhysicsWorkspace::get().clear();
    m_resolved.clear();
    m_trace = {};
    m_playing = false;
    m_focusIndex = -1;
    m_selectedObject = -1;
    m_zoom = 1.f;
    m_manualCamera = false;
    rebuildPreview();
    updateFocusLabel();
    refreshBodies();
    setStatus("Cuerpos borrados. Pulsa Elegir A y elige los objetos en el editor.", {255, 205, 95});
}

void PhysicsPopup::preview() {
    if (!runSimulation()) return;
    m_playing = true;
    m_elapsed = 0.f;
    std::size_t bakedDynamics = std::ranges::count_if(m_resolved, [](auto const& body) {
        return body.spec.motion == Motion::Dynamic &&
            body.native.backend == PhysicsBackend::Baked;
    });
    std::size_t reactiveDynamics = std::ranges::count_if(m_resolved, [](auto const& body) {
        return body.spec.motion == Motion::Dynamic &&
            body.native.backend == PhysicsBackend::Reactive;
    });
    std::size_t const bakedEstimate = bakedDynamics * (m_trace.frames.size() + 1);
    std::vector<NativeBodyInput> nativeInputs;
    nativeInputs.reserve(m_resolved.size());
    for (auto const& body : m_resolved) {
        NativeBodyInput input;
        input.spec = body.spec;
        input.settings = body.native;
        input.objectCount = body.objects.size();
        for (auto const& visual : body.visuals) input.objectOffsets.push_back(visual.offset);
        nativeInputs.push_back(std::move(input));
    }
    auto const nativeEstimate = reactiveDynamics > 0
        ? estimateNativeRequirements(nativeInputs, m_config.gravity, m_config.airDrag)
            .estimatedObjects
        : 0;
    ShapeCounts shapes;
    for (auto const& body : m_resolved) {
        shapes.boxes += body.shapes.boxes;
        shapes.ramps += body.shapes.ramps;
        shapes.rounds += body.shapes.rounds;
        shapes.hulls += body.shapes.hulls;
    }
    // A body on the trigger backend is drawn with the graph's own model, and a
    // preset that waits for the player has nothing to move it in the lab.
    std::size_t const waiting = std::ranges::count_if(m_resolved, [&](auto const& body) {
        return body.spec.motion == Motion::Dynamic &&
            body.native.backend == PhysicsBackend::Reactive &&
            nativeNeedsPlayer(body.native, body.spec, m_config.gravity);
    });
    std::string backends;
    if (reactiveDynamics > 0) {
        backends = fmt::format(" | {} con triggers", reactiveDynamics);
        if (waiting > 0) backends += fmt::format(", {} esperan al jugador", waiting);
    }
    setStatus(
        fmt::format(
            "{} cuerpos | {} bloques + {} rampas + {} redondos + {} siluetas | {} impactos | "
            "impulso pico {:.1f} | {} frames{} | hasta {} objetos GD ({} baked + ~{} nativos)",
            m_resolved.size(), shapes.boxes, shapes.ramps, shapes.rounds, shapes.hulls,
            m_trace.impacts, m_trace.peakImpulse, m_trace.frames.size(), backends,
            bakedEstimate + nativeEstimate, bakedEstimate, nativeEstimate
        ),
        {170, 225, 185}
    );
}

void PhysicsPopup::bake() {
    if (!runSimulation()) return;
    auto result = emitToEditor(editorUI(), m_resolved, m_trace, m_config);
    if (result.isErr()) {
        setStatus(result.unwrapErr(), {255, 120, 120});
        return;
    }
    auto const report = result.unwrap();
    setStatus(
        fmt::format(
            "Compilado GD: {} keyframes + {} triggers ({} fisica, {} colision) + "
            "{} bloques, {} grupos.",
            report.keyframes, report.triggers, report.physicsTriggers,
            report.collisionTriggers, report.collisionBlocks, report.groups
        ),
        {135, 255, 150}
    );
    PaimonNotify::show("Fisicas compiladas a objetos nativos de GD.", NotificationIcon::Success);
}

void PhysicsPopup::removeLast() {
    auto result = removeLastEmission(editorUI());
    if (result.isErr()) {
        setStatus(result.unwrapErr(), {255, 190, 100});
        return;
    }
    setStatus(
        fmt::format("Se quitaron {} objetos de la ultima salida.", result.unwrap()),
        {170, 225, 185}
    );
}

void PhysicsPopup::adjust(int field, int direction) {
    switch (field) {
        case 0: m_config.gravity = std::clamp(m_config.gravity + direction * 100.f, -2000.f, 2000.f); break;
        case 1: m_config.restitution = std::clamp(m_config.restitution + direction * 0.05f, 0.f, 1.f); break;
        case 2: m_config.friction = std::clamp(m_config.friction + direction * 0.05f, 0.f, 1.f); break;
        case 3: m_config.airDrag = std::clamp(m_config.airDrag + direction * 0.02f, 0.f, 1.f); break;
        case 4: m_config.duration = std::clamp(m_config.duration + direction * 0.5f, 0.5f, 10.f); break;
        case 5: m_config.velocityX = std::clamp(m_config.velocityX + direction * 50.f, -1500.f, 1500.f); break;
        case 6: m_config.velocityY = std::clamp(m_config.velocityY + direction * 50.f, -1500.f, 1500.f); break;
        case 7: m_config.spinDegrees = std::clamp(m_config.spinDegrees + direction * 15.f, -720.f, 720.f); break;
        case 8: m_config.sampleRate = std::clamp(m_config.sampleRate + direction * 10, 10, 40); break;
        default: return;
    }
    saveConfig(m_config);
    refreshValues();
    m_playing = false;
}

bool PhysicsPopup::runSimulation() {
    auto* ui = editorUI();
    auto result = PhysicsWorkspace::get().resolve(ui, m_config);
    if (result.isErr()) {
        setStatus(result.unwrapErr(), {255, 120, 120});
        return false;
    }
    m_resolved = result.unwrap();
    std::vector<BodySpec> specs;
    std::vector<NativeBodySettings> settings;
    specs.reserve(m_resolved.size());
    settings.reserve(m_resolved.size());
    for (auto const& body : m_resolved) {
        specs.push_back(body.spec);
        settings.push_back(body.native);
    }
    m_trace = simulateWorkspace(specs, settings, simulationOptions(m_config));
    if (m_trace.exhausted) {
        m_trace = {};
        m_playing = false;
        setStatus(
            "La simulacion pesa demasiado para resolverla sin congelar el editor. "
            "Baja la duracion o la calidad, o captura menos objetos.",
            {255, 120, 120}
        );
        return false;
    }
    if (m_trace.frames.size() < 2) {
        setStatus("El solver no produjo suficientes frames.", {255, 120, 120});
        return false;
    }
    if (m_focusIndex >= static_cast<int>(m_resolved.size())) m_focusIndex = -1;
    rebuildPreview();
    updateFocusLabel();
    return true;
}

void PhysicsPopup::refreshValues() {
    std::array<std::string, 9> const values{
        fmt::format("{:.0f}", m_config.gravity),
        fmt::format("{:.2f}", m_config.restitution),
        fmt::format("{:.2f}", m_config.friction),
        fmt::format("{:.2f}", m_config.airDrag),
        fmt::format("{:.1f} s", m_config.duration),
        fmt::format("{:.0f}", m_config.velocityX),
        fmt::format("{:.0f}", m_config.velocityY),
        fmt::format("{:.0f} deg/s", m_config.spinDegrees),
        fmt::format("{} Hz", m_config.sampleRate),
    };
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (m_valueLabels[i]) m_valueLabels[i]->setString(values[i].c_str());
    }
}

void PhysicsPopup::refreshBodies() {
    auto const& bodies = PhysicsWorkspace::get().bodies();
    if (bodies.empty()) {
        m_bodyALabel->setString("A: sin capturar");
        m_otherBodiesLabel->setString("B / multiples: sin capturar");
        if (m_bodyModeSprite) m_bodyModeSprite->setString("B: fijo");
        return;
    }

    auto const& a = bodies.front();
    m_bodyALabel->setString(fmt::format(
        "A: {} objeto{} | {} | {}",
        liveObjectCount(a), liveObjectCount(a) == 1 ? "" : "s",
        a.exactGroup > 0 ? fmt::format("grupo {}", a.exactGroup) : "grupo automatico",
        a.motion == Motion::Static
            ? std::string("fijo")
            : fmt::format("{}:{}", backendName(a.native.backend), presetName(a.native.preset))
    ).c_str());

    std::size_t dynamicCount = 0;
    std::size_t staticCount = 0;
    std::size_t objectCount = 0;
    for (std::size_t i = 1; i < bodies.size(); ++i) {
        objectCount += liveObjectCount(bodies[i]);
        if (bodies[i].motion == Motion::Dynamic) ++dynamicCount;
        else ++staticCount;
    }
    m_otherBodiesLabel->setString(fmt::format(
        "B+: {} fijos + {} dinamicos | {} objetos",
        staticCount, dynamicCount, objectCount
    ).c_str());
    if (m_bodyModeSprite) {
        m_bodyModeSprite->setString(
            bodies.size() > 1 && bodies[1].motion == Motion::Dynamic ? "B: dinamico" : "B: fijo"
        );
    }
}

// GD keeps backgrounds and grounds as loose image files, never as sheet frames,
// so the previous createWithSpriteFrameName call always came back empty and the
// preview stayed black. Colours are read off the editor's own nodes, which is
// what makes the panel match the level being built.
void PhysicsPopup::buildPreviewScenery(CCNode* clip, float width, float height) {
    auto* editor = LevelEditorLayer::get();
    auto* gameManager = GameManager::sharedState();
    // GD numbers its backgrounds from one; index zero asks for a file that does
    // not exist and the panel stayed black waiting for it.
    int const backgroundIndex = std::max(
        1, editor && editor->m_levelSettings ? editor->m_levelSettings->m_backgroundIndex : 1
    );

    auto* background = paimon::SpriteHelper::safeCreate(gameManager->getBGTexture(backgroundIndex));
    if (!background) {
        log::warn("[PhysicsLab] sin textura de fondo para el indice {}", backgroundIndex);
        background = paimon::SpriteHelper::safeCreate("game_bg_01_001.png");
    }
    if (background) {
        auto const size = background->getContentSize();
        if (size.width > 0.f && size.height > 0.f) {
            background->setScale(std::max(width / size.width, height / size.height));
            background->setPosition({width * 0.5f, height * 0.5f});
            if (editor && editor->m_background) {
                background->setColor(liftDark(editor->m_background->getColor()));
            }
            clip->addChild(background, 0);
        }
    }
    if (editor && editor->m_groundLayer && editor->m_groundLayer->m_ground1Sprite) {
        m_groundColor = liftDark(editor->m_groundLayer->m_ground1Sprite->getColor());
    }

    clip->addChild(CCLayerColor::create({0, 0, 0, 45}, width, height), 2);
}

// The ground belongs at y = 0 in level coordinates, inside the world node, so it
// scrolls with the camera. Glued to the bottom of the panel it only pretended to
// be a floor that bodies then fell straight through.
void PhysicsPopup::addWorldGround() {
    auto* fill = CCLayerColor::create(
        {m_groundColor.r, m_groundColor.g, m_groundColor.b, 235},
        kWorldGroundWidth, kWorldGroundHeight
    );
    fill->setPosition({-kWorldGroundWidth * 0.5f, -kWorldGroundHeight});
    m_previewWorld->addChild(fill, -3);

    // The level's own ground tile repeated across the world. It is a loose
    // texture rather than a sheet frame, so GL_REPEAT tiles it directly.
    auto* editor = LevelEditorLayer::get();
    auto* source = editor && editor->m_groundLayer
        ? editor->m_groundLayer->m_ground1Sprite
        : nullptr;
    if (auto* texture = source ? source->getTexture() : nullptr) {
        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT};
        texture->setTexParameters(&params);
        auto* tile = CCSprite::createWithTexture(texture);
        tile->setTextureRect({0.f, 0.f, kWorldGroundWidth, texture->getContentSize().height});
        tile->setAnchorPoint({0.5f, 1.f});
        tile->setColor(m_groundColor);
        m_previewWorld->addChild(tile, -2);
    }

    m_groundLine = CCLayerColor::create({255, 255, 255, 120}, kWorldGroundWidth, 1.f);
    m_previewWorld->addChild(m_groundLine, -1);
}

void PhysicsPopup::rebuildPreview() {
    if (!m_previewWorld) return;
    m_previewWorld->removeAllChildren();
    m_bodyContainers.clear();
    m_outlineNodes.clear();
    m_pathDraw = nullptr;
    m_groundLine = nullptr;
    if (m_resolved.empty()) return;

    refreshCameraScale();
    addWorldGround();

    m_pathDraw = CCDrawNode::create();
    m_previewWorld->addChild(m_pathDraw, 0);

    for (std::size_t i = 0; i < m_resolved.size(); ++i) {
        auto const& body = m_resolved[i];
        auto* container = CCNode::create();
        m_previewWorld->addChild(container, body.spec.motion == Motion::Dynamic ? 2 : 1);

        for (auto const& visual : body.visuals) {
            auto* art = buildObjectArt(visual);
            if (!art) continue;
            art->setPosition({visual.offset.x, visual.offset.y});
            container->addChild(art, visual.zOrder);
        }

        auto* outline = CCDrawNode::create();
        container->addChild(outline, 1000);
        m_outlineNodes.push_back(outline);
        m_bodyContainers.push_back(container);
    }

    refreshOverlays();
    drawPreview(playbackTime(), 0.f);
}

void PhysicsPopup::refreshCameraScale() {
    if (m_resolved.empty()) {
        m_camScale = 1.f;
        return;
    }
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (std::size_t i = 0; i < m_resolved.size(); ++i) {
        if (m_focusIndex >= 0 && static_cast<int>(i) != m_focusIndex) continue;
        auto const& spec = m_resolved[i].spec;
        for (auto const& fixture : spec.fixtures) {
            minX = std::min(minX, spec.position.x + fixture.offset.x - fixture.halfSize.x);
            minY = std::min(minY, spec.position.y + fixture.offset.y - fixture.halfSize.y);
            maxX = std::max(maxX, spec.position.x + fixture.offset.x + fixture.halfSize.x);
            maxY = std::max(maxY, spec.position.y + fixture.offset.y + fixture.halfSize.y);
        }
    }
    // The fit covers the bodies' own size only; the trajectory length (and thus
    // the duration) must never shrink the view.
    float const width = std::max(maxX - minX, 1.f);
    float const height = std::max(maxY - minY, 1.f);
    float const usableHeight = kPreviewHeight - kPreviewClipInset * 2.f - 18.f;
    m_camScale = std::clamp(
        std::min((kPreviewWidth - 26.f) / width, usableHeight / height),
        0.004f, 6.f
    );
}

float PhysicsPopup::previewScale() const {
    return std::clamp(m_camScale * m_zoom, 0.002f, 24.f);
}

void PhysicsPopup::adjustZoom(float factor) {
    float const next = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
    if (std::abs(next - m_zoom) < 0.0001f) return;
    m_zoom = next;
    refreshOverlays();
    updateFocusLabel();
    drawPreview(playbackTime(), 0.f);
}

void PhysicsPopup::resetView() {
    m_zoom = 1.f;
    m_manualCamera = false;
    refreshCameraScale();
    refreshOverlays();
    updateFocusLabel();
    drawPreview(playbackTime(), 0.f);
}

// Outlines, the trajectory and the ground line live inside the world node, so
// their widths are divided by the view scale to stay a constant thickness on
// screen at any zoom.
void PhysicsPopup::refreshOverlays() {
    for (std::size_t i = 0; i < m_outlineNodes.size(); ++i) drawBodyOutline(i);
    drawTrajectory();
    if (m_groundLine) {
        float const height = 2.f / std::max(previewScale(), 0.001f);
        m_groundLine->setContentSize({kWorldGroundWidth, height});
        m_groundLine->setPosition({-kWorldGroundWidth * 0.5f, -height});
    }
}

void PhysicsPopup::toggleHitboxes() {
    m_showHitboxes = !m_showHitboxes;
    refreshOverlays();
    setStatus(
        m_showHitboxes
            ? "Hitboxes a la vista: cada cuerpo muestra la forma con la que choca."
            : "Hitboxes ocultas: la vista queda con los objetos del juego solamente.",
        {170, 225, 185}
    );
}

// The gear reaches the same switch, but flipping a body while watching it fall
// is the thing you want one tap away.
void PhysicsPopup::toggleFocusedMotion() {
    if (m_focusIndex < 0 || static_cast<std::size_t>(m_focusIndex) >= m_resolved.size()) {
        setStatus(
            "Toca un cuerpo en la vista para poder cambiarlo entre dinamico y fijo.",
            {255, 205, 95}
        );
        return;
    }
    auto& workspace = PhysicsWorkspace::get();
    std::size_t const index = static_cast<std::size_t>(m_focusIndex);
    auto result = workspace.toggleMotion(index);
    if (result.isErr()) {
        setStatus(result.unwrapErr(), {255, 190, 100});
        return;
    }
    bool const dynamic = result.unwrap() == Motion::Dynamic;
    if (auto* material = workspace.material(index);
        material && dynamic && material->gravityScale <= 0.f) {
        material->gravityScale = 1.f;
    }
    reloadBodies();
    setStatus(
        fmt::format(
            "Cuerpo {} ahora es {}.",
            bodyName(index), dynamic ? "dinamico" : "fijo"
        ),
        {170, 225, 185}
    );
}

void PhysicsPopup::openBodyEditor() {
    if (m_focusIndex < 0 || static_cast<std::size_t>(m_focusIndex) >= m_resolved.size()) {
        setStatus(
            "Toca un objeto en la vista o usa las flechas para elegir un cuerpo.",
            {255, 205, 95}
        );
        return;
    }
    WeakRef<PhysicsPopup> self = this;
    auto* popup = PhysicsBodyPopup::create(
        static_cast<std::size_t>(m_focusIndex), m_selectedObject,
        [self] {
            if (auto popup = self.lock()) popup->reloadBodies();
        }
    );
    if (popup) popup->show();
}

// Bodies are drawn where the simulation left them, so the pick runs against the
// containers rather than the captured positions.
void PhysicsPopup::selectAt(CCPoint const& location) {
    if (m_bodyContainers.size() != m_resolved.size()) return;
    float const slack = kTapSlack / std::max(previewScale(), 0.001f);
    for (std::size_t index = m_bodyContainers.size(); index-- > 0;) {
        auto const local = m_bodyContainers[index]->convertToNodeSpace(location);
        auto const& fixtures = m_resolved[index].spec.fixtures;
        for (std::size_t slot = 0; slot < fixtures.size(); ++slot) {
            if (!containsPoint(fixtures[slot], local, slack)) continue;
            m_focusIndex = static_cast<int>(index);
            m_selectedObject = static_cast<int>(slot);
            updateFocusLabel();
            refreshOverlays();
            auto const& body = m_resolved[index];
            int const objectID = slot < body.visuals.size() ? body.visuals[slot].objectID : 0;
            setStatus(
                fmt::format(
                    "Cuerpo {} | objeto {} de {} (ID {}) | friccion {:.2f} | rebote {:.2f}. "
                    "Usa el engranaje para cambiarlos.",
                    bodyName(index), slot + 1, fixtures.size(), objectID,
                    fixtures[slot].friction >= 0.f ? fixtures[slot].friction : body.spec.friction,
                    fixtures[slot].restitution >= 0.f
                        ? fixtures[slot].restitution
                        : body.spec.restitution
                ),
                {170, 225, 185}
            );
            return;
        }
    }
    m_selectedObject = -1;
    updateFocusLabel();
    refreshOverlays();
}

void PhysicsPopup::reloadBodies() {
    auto result = PhysicsWorkspace::get().resolve(editorUI(), m_config);
    if (result.isErr()) {
        setStatus(result.unwrapErr(), {255, 190, 100});
        return;
    }
    m_playing = false;
    m_trace = {};
    m_resolved = result.unwrap();
    rebuildPreview();
    refreshBodies();
    updateFocusLabel();
    setStatus("Cuerpo actualizado: previsualiza para ver el cambio.", {170, 225, 185});
}

void PhysicsPopup::drawBodyOutline(std::size_t index) {
    if (index >= m_outlineNodes.size() || index >= m_resolved.size()) return;
    auto* draw = m_outlineNodes[index];
    draw->clear();
    bool const dynamic = m_resolved[index].spec.motion == Motion::Dynamic;
    bool const focused = m_focusIndex == static_cast<int>(index);
    // With the hitboxes hidden the preview is nothing but the game's own art,
    // so the only outline left is the one marking what the user picked.
    if (!m_showHitboxes && !focused) return;

    ccColor4F const border = dynamic
        ? ccc4f(0.55f, 0.95f, 1.f, focused ? 0.95f : 0.35f)
        : ccc4f(1.f, 0.82f, 0.4f, focused ? 0.95f : 0.35f);
    ccColor4F const fill = ccc4f(1.f, 1.f, 1.f, focused ? 0.06f : 0.f);
    ccColor4F const pickedBorder = ccc4f(1.f, 1.f, 1.f, 0.95f);
    float const borderWidth = (focused ? 1.4f : 0.9f) / std::max(previewScale(), 0.001f);
    auto const& fixtures = m_resolved[index].spec.fixtures;
    for (std::size_t slot = 0; slot < fixtures.size(); ++slot) {
        auto const& fixture = fixtures[slot];
        bool const picked = focused && static_cast<int>(slot) == m_selectedObject;
        if (!m_showHitboxes && !picked) continue;
        if (fixture.radius > 0.f) {
            std::array<CCPoint, kCircleSegments> circle{};
            for (int i = 0; i < kCircleSegments; ++i) {
                float const angle = kTwoPi * static_cast<float>(i) / kCircleSegments;
                circle[static_cast<std::size_t>(i)] = ccp(
                    fixture.offset.x + std::cos(angle) * fixture.radius,
                    fixture.offset.y + std::sin(angle) * fixture.radius
                );
            }
            draw->drawPolygon(
                circle.data(), kCircleSegments, fill, borderWidth,
                picked ? pickedBorder : border
            );
            continue;
        }
        if (fixture.vertexCount >= 3) {
            CCPoint vertices[kMaxVertices]{};
            int const count = std::min(fixture.vertexCount, kMaxVertices);
            for (int i = 0; i < count; ++i) {
                vertices[i] = ccp(
                    fixture.offset.x + fixture.vertices[i].x,
                    fixture.offset.y + fixture.vertices[i].y
                );
            }
            draw->drawPolygon(
                vertices, static_cast<unsigned int>(count), fill, borderWidth,
                picked ? pickedBorder : border
            );
            continue;
        }
        CCPoint vertices[4]{
            {fixture.offset.x - fixture.halfSize.x, fixture.offset.y - fixture.halfSize.y},
            {fixture.offset.x + fixture.halfSize.x, fixture.offset.y - fixture.halfSize.y},
            {fixture.offset.x + fixture.halfSize.x, fixture.offset.y + fixture.halfSize.y},
            {fixture.offset.x - fixture.halfSize.x, fixture.offset.y + fixture.halfSize.y},
        };
        draw->drawPolygon(vertices, 4, fill, borderWidth, picked ? pickedBorder : border);
    }
}

void PhysicsPopup::drawTrajectory() {
    if (!m_pathDraw) return;
    m_pathDraw->clear();
    if (m_trace.frames.size() < 2 || m_resolved.empty()) return;
    std::size_t index = 0;
    if (m_focusIndex >= 0) {
        index = static_cast<std::size_t>(m_focusIndex);
    } else {
        for (std::size_t i = 0; i < m_resolved.size(); ++i) {
            if (m_resolved[i].spec.motion == Motion::Dynamic) {
                index = i;
                break;
            }
        }
    }
    if (index >= m_trace.frames.front().poses.size()) return;
    float const radius = 0.8f / std::max(previewScale(), 0.001f);
    ccColor4F const color = m_focusIndex >= 0
        ? ccc4f(1.f, 1.f, 1.f, 0.35f)
        : ccc4f(0.6f, 0.9f, 1.f, 0.22f);
    CCPoint last{
        m_trace.frames.front().poses[index].position.x,
        m_trace.frames.front().poses[index].position.y,
    };
    for (std::size_t f = 1; f < m_trace.frames.size(); ++f) {
        auto const& position = m_trace.frames[f].poses[index].position;
        CCPoint const point{position.x, position.y};
        m_pathDraw->drawSegment(last, point, radius, color);
        last = point;
    }
}

void PhysicsPopup::cycleFocus(int direction) {
    int const count = static_cast<int>(m_resolved.size());
    int next = m_focusIndex + direction;
    if (next < -1) next = count - 1;
    if (next >= count) next = -1;
    m_focusIndex = count > 0 ? next : -1;
    m_selectedObject = -1;
    m_manualCamera = false;
    updateFocusLabel();
    if (m_resolved.empty()) return;
    refreshCameraScale();
    refreshOverlays();
    drawPreview(playbackTime(), 0.f);
}

void PhysicsPopup::updateFocusLabel() {
    if (!m_focusLabel) return;
    std::string const zoom = fmt::format("x{:.1f}", m_zoom);
    std::string text;
    if (m_focusIndex < 0 || static_cast<std::size_t>(m_focusIndex) >= m_resolved.size()) {
        text = fmt::format("Vista: todos | {}", zoom);
    } else {
        std::size_t const index = static_cast<std::size_t>(m_focusIndex);
        auto const& visuals = m_resolved[index].visuals;
        bool const picked = m_selectedObject >= 0 &&
            static_cast<std::size_t>(m_selectedObject) < visuals.size();
        text = fmt::format(
            "Vista: {} | {} | {}",
            bodyName(index),
            picked
                ? fmt::format("ID {}", visuals[static_cast<std::size_t>(m_selectedObject)].objectID)
                : objectIDSummary(m_resolved[index]),
            zoom
        );
    }
    m_focusLabel->setString(text.c_str());
    m_focusLabel->limitLabelWidth(190.f, 0.32f, 0.2f);
}

float PhysicsPopup::playbackTime() const {
    if (m_trace.frames.empty()) return 0.f;
    return std::min(m_elapsed, m_trace.frames.back().time);
}

void PhysicsPopup::drawPreview(float time, float dt) {
    if (!m_previewWorld) return;
    if (m_bodyContainers.size() != m_resolved.size() || m_resolved.empty()) return;

    // Without a trace the bodies are drawn where the editor has them, so opening
    // the lab already shows what was captured instead of an empty panel.
    bool const animated = !m_trace.frames.empty();
    std::size_t next = 1;
    std::size_t previous = 0;
    float alpha = 0.f;
    if (animated) {
        while (next < m_trace.frames.size() && m_trace.frames[next].time < time) ++next;
        next = std::min(next, m_trace.frames.size() - 1);
        previous = next > 0 ? next - 1 : 0;
        float const span = m_trace.frames[next].time - m_trace.frames[previous].time;
        alpha = span > 0.0001f
            ? std::clamp((time - m_trace.frames[previous].time) / span, 0.f, 1.f)
            : 0.f;
    }

    Vec2 target{};
    int tracked = 0;
    for (std::size_t i = 0; i < m_resolved.size(); ++i) {
        Vec2 position = m_resolved[i].spec.position;
        float angle = m_resolved[i].spec.angle;
        if (animated) {
            auto const& a = m_trace.frames[previous].poses[i];
            auto const& b = m_trace.frames[next].poses[i];
            position = {
                a.position.x + (b.position.x - a.position.x) * alpha,
                a.position.y + (b.position.y - a.position.y) * alpha,
            };
            angle = a.angle + (b.angle - a.angle) * alpha;
        }

        auto* container = m_bodyContainers[i];
        container->setPosition({position.x, position.y});
        container->setRotation(-angle * kRadiansToDegrees);

        bool const track = m_focusIndex >= 0
            ? static_cast<int>(i) == m_focusIndex
            : m_resolved[i].spec.motion == Motion::Dynamic;
        if (track) {
            target.x += position.x;
            target.y += position.y;
            ++tracked;
        }
    }
    // Dragging the preview takes the camera off the bodies until the reset button
    // hands it back.
    if (tracked && !m_manualCamera) {
        float const blend = dt > 0.f ? std::min(1.f, dt * kCamFollow) : 1.f;
        m_camera.x += (target.x / tracked - m_camera.x) * blend;
        m_camera.y += (target.y / tracked - m_camera.y) * blend;
    }

    float const clipWidth = kPreviewWidth - kPreviewClipInset * 2.f;
    float const clipHeight = kPreviewHeight - kPreviewClipInset * 2.f;
    float const scale = previewScale();
    m_previewWorld->setScale(scale);
    m_previewWorld->setPosition({
        clipWidth * 0.5f - m_camera.x * scale,
        clipHeight * 0.5f - m_camera.y * scale,
    });
}

bool PhysicsPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    CCRect const preview{kPreviewX, kPreviewY, kPreviewWidth, kPreviewHeight};
    if (m_mainLayer && preview.containsPoint(m_mainLayer->convertToNodeSpace(touch->getLocation()))) {
        m_panning = true;
        return true;
    }
    return FLAlertLayer::ccTouchBegan(touch, event);
}

void PhysicsPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_panning) {
        FLAlertLayer::ccTouchMoved(touch, event);
        return;
    }
    auto const delta = touch->getDelta();
    if (std::abs(delta.x) < 0.01f && std::abs(delta.y) < 0.01f) return;
    float const scale = std::max(previewScale(), 0.001f);
    m_manualCamera = true;
    m_camera.x -= delta.x / scale;
    m_camera.y -= delta.y / scale;
    drawPreview(playbackTime(), 0.f);
}

void PhysicsPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (m_panning) {
        m_panning = false;
        // A finger that never left where it started is picking an object, not
        // dragging the view.
        if (ccpDistance(touch->getLocation(), touch->getStartLocation()) <= kTapSlack) {
            selectAt(touch->getLocation());
        }
        return;
    }
    FLAlertLayer::ccTouchEnded(touch, event);
}

void PhysicsPopup::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_panning = false;
    FLAlertLayer::ccTouchCancelled(touch, event);
}

void PhysicsPopup::scrollWheel(float vertical, float horizontal) {
    float const steps = paimon::smoothscroll::SmoothScrollController::get()
        .filteredZoomSteps(vertical, horizontal);
    if (std::abs(steps) < 0.000001f) return;

    float const baseFactor = steps > 0.f ? kScrollZoomIn : kScrollZoomOut;
    adjustZoom(std::pow(baseFactor, std::abs(steps)));
}

void PhysicsPopup::tick(float dt) {
    if (!m_playing || m_trace.frames.empty()) return;
    m_elapsed += dt;
    float const duration = m_trace.frames.back().time;
    if (m_elapsed > duration + 0.45f) m_elapsed = 0.f;
    drawPreview(playbackTime(), dt);
}

void PhysicsPopup::setStatus(std::string const& text, ccColor3B color) {
    if (!m_statusLabel) return;
    m_statusLabel->setColor(color);
    m_statusLabel->setString(text.c_str());
    m_statusLabel->limitLabelWidth(475.f, 0.265f, 0.16f);
}

} // namespace paimon::editorphysics
