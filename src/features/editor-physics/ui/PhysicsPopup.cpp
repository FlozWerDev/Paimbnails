#include "PhysicsPopup.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../services/PhysicsTriggerEmitter.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <ranges>

using namespace geode::prelude;

namespace paimon::editorphysics {

namespace {

constexpr float kPopupWidth = 520.f;
constexpr float kPopupHeight = 315.f;
constexpr float kPreviewX = 16.f;
constexpr float kPreviewY = 91.f;
constexpr float kPreviewWidth = 235.f;
constexpr float kPreviewHeight = 137.f;
constexpr float kTwoPi = 6.2831853071795864769f;

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

Vec2 rotatePoint(Vec2 point, float angle) {
    float const cosine = std::cos(angle);
    float const sine = std::sin(angle);
    return {
        point.x * cosine - point.y * sine,
        point.x * sine + point.y * cosine,
    };
}

std::size_t liveObjectCount(CapturedBody const& body) {
    return std::ranges::count_if(body.objects, [](auto const& object) {
        auto locked = object.lock();
        return locked && locked->getParent();
    });
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

    auto* previewTitle = CCLabelBMFont::create("VISTA PREVIA", "goldFont.fnt");
    previewTitle->setScale(0.35f);
    previewTitle->setPosition({kPreviewX + kPreviewWidth * 0.5f, kPreviewY + kPreviewHeight - 11.f});
    m_mainLayer->addChild(previewTitle, 2);

    m_previewDraw = CCDrawNode::create();
    m_mainLayer->addChild(m_previewDraw, 3);

    m_bodyALabel = smallLabel(m_mainLayer, {128.f, 267.f}, {120, 235, 255});
    m_otherBodiesLabel = smallLabel(m_mainLayer, {111.f, 249.f}, {255, 190, 95});

    auto* hint = CCLabelBMFont::create(
        "A cae y responde; B puede ser fijo o dinamico. Los extras colisionan igual.",
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
    textButton(menu, "Hornear", 269.f, 39.f, 90, "GJ_button_01.png", [self] {
        if (auto popup = self.lock()) popup->bake();
    });
    textButton(menu, "Quitar ultimo", 399.f, 39.f, 115, "GJ_button_06.png", [self] {
        if (auto popup = self.lock()) popup->removeLast();
    });

    m_statusLabel = smallLabel(m_mainLayer, {260.f, 15.f}, {185, 200, 225});
    m_statusLabel->setScale(0.265f);
    m_statusLabel->limitLabelWidth(475.f, 0.265f, 0.16f);

    refreshBodies();
    refreshValues();
    if (PhysicsWorkspace::get().empty()) {
        setStatus("Selecciona A, cierra y vuelve a abrir el laboratorio.", {255, 205, 95});
    } else {
        setStatus("Listo: previsualiza antes de crear los triggers.", {170, 225, 185});
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
    if (m_previewDraw) m_previewDraw->clear();
    refreshBodies();
    setStatus(
        result.unwrap() == Motion::Dynamic
            ? "B ahora es reactivo: recibe impactos sin gravedad propia."
            : "B ahora funciona como colisionador fijo.",
        {170, 225, 185}
    );
}

void PhysicsPopup::clearBodies() {
    PhysicsWorkspace::get().clear();
    m_resolved.clear();
    m_trace = {};
    m_playing = false;
    if (m_previewDraw) m_previewDraw->clear();
    refreshBodies();
    setStatus("Cuerpos borrados. Selecciona A y pulsa Elegir A.", {255, 205, 95});
}

void PhysicsPopup::preview() {
    if (!runSimulation()) return;
    m_playing = true;
    m_elapsed = 0.f;
    drawPreview(0.f);
    std::size_t dynamics = std::ranges::count_if(m_resolved, [](auto const& body) {
        return body.spec.motion == Motion::Dynamic;
    });
    std::size_t const estimate = dynamics * (m_trace.frames.size() + 1);
    setStatus(
        fmt::format(
            "{} cuerpos | {} impactos | impulso pico {:.1f} | {} frames | hasta {} objetos",
            m_resolved.size(), m_trace.impacts, m_trace.peakImpulse,
            m_trace.frames.size(), estimate
        ),
        {170, 225, 185}
    );
}

void PhysicsPopup::bake() {
    if (!runSimulation()) return;
    auto result = emitToEditor(editorUI(), m_resolved, m_trace);
    if (result.isErr()) {
        setStatus(result.unwrapErr(), {255, 120, 120});
        return;
    }
    auto const report = result.unwrap();
    setStatus(
        fmt::format(
            "Horneado: {} keyframes + {} trigger(s), {} grupos.",
            report.keyframes, report.triggers, report.groups
        ),
        {135, 255, 150}
    );
    PaimonNotify::show("Fisicas horneadas con keyframes.", NotificationIcon::Success);
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
    specs.reserve(m_resolved.size());
    for (auto const& body : m_resolved) specs.push_back(body.spec);
    m_trace = simulate(specs, simulationOptions(m_config));
    if (m_trace.frames.size() < 2) {
        setStatus("El solver no produjo suficientes frames.", {255, 120, 120});
        return false;
    }
    refreshPreviewBounds();
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
        "A: {} objeto{} | {}",
        liveObjectCount(a), liveObjectCount(a) == 1 ? "" : "s",
        a.exactGroup > 0 ? fmt::format("grupo {}", a.exactGroup) : "grupo automatico"
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
            bodies.size() > 1 && bodies[1].motion == Motion::Dynamic ? "B: reactivo" : "B: fijo"
        );
    }
}

void PhysicsPopup::refreshPreviewBounds() {
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (auto const& frame : m_trace.frames) {
        for (std::size_t i = 0; i < m_resolved.size(); ++i) {
            float radius = 1.f;
            for (auto const& fixture : m_resolved[i].spec.fixtures) {
                float const offset = std::hypot(fixture.offset.x, fixture.offset.y);
                float const extent = std::hypot(fixture.halfSize.x, fixture.halfSize.y);
                radius = std::max(radius, offset + extent);
            }
            auto const& position = frame.poses[i].position;
            minX = std::min(minX, position.x - radius);
            minY = std::min(minY, position.y - radius);
            maxX = std::max(maxX, position.x + radius);
            maxY = std::max(maxY, position.y + radius);
        }
    }
    float const width = std::max(maxX - minX, 1.f);
    float const height = std::max(maxY - minY, 1.f);
    m_previewScale = std::min(
        (kPreviewWidth - 18.f) / width,
        (kPreviewHeight - 28.f) / height
    );
    float const shownWidth = width * m_previewScale;
    float const shownHeight = height * m_previewScale;
    m_previewMinX = minX - (kPreviewWidth - 18.f - shownWidth) * 0.5f / m_previewScale;
    m_previewMinY = minY - (kPreviewHeight - 28.f - shownHeight) * 0.5f / m_previewScale;
}

void PhysicsPopup::drawPreview(float time) {
    if (!m_previewDraw || m_trace.frames.empty()) return;
    m_previewDraw->clear();

    std::size_t next = 1;
    while (next < m_trace.frames.size() && m_trace.frames[next].time < time) ++next;
    next = std::min(next, m_trace.frames.size() - 1);
    std::size_t const previous = next > 0 ? next - 1 : 0;
    float const span = m_trace.frames[next].time - m_trace.frames[previous].time;
    float const alpha = span > 0.0001f
        ? std::clamp((time - m_trace.frames[previous].time) / span, 0.f, 1.f)
        : 0.f;

    auto mapPoint = [&](Vec2 point) {
        return CCPoint{
            kPreviewX + 9.f + (point.x - m_previewMinX) * m_previewScale,
            kPreviewY + 8.f + (point.y - m_previewMinY) * m_previewScale,
        };
    };

    for (std::size_t i = 0; i < m_resolved.size(); ++i) {
        auto const& a = m_trace.frames[previous].poses[i];
        auto const& b = m_trace.frames[next].poses[i];
        Pose pose;
        pose.position = {
            a.position.x + (b.position.x - a.position.x) * alpha,
            a.position.y + (b.position.y - a.position.y) * alpha,
        };
        pose.angle = a.angle + std::remainder(b.angle - a.angle, kTwoPi) * alpha;

        bool const dynamic = m_resolved[i].spec.motion == Motion::Dynamic;
        ccColor4F const fill = dynamic
            ? ccc4f(i == 0 ? 0.12f : 0.35f, i == 0 ? 0.78f : 0.9f, 0.95f, 0.62f)
            : ccc4f(1.f, 0.52f, 0.15f, 0.62f);
        ccColor4F const border = dynamic
            ? ccc4f(0.55f, 0.95f, 1.f, 0.95f)
            : ccc4f(1.f, 0.82f, 0.4f, 0.95f);

        for (auto const& fixture : m_resolved[i].spec.fixtures) {
            CCPoint vertices[4];
            Vec2 const corners[4]{
                {-fixture.halfSize.x, -fixture.halfSize.y},
                { fixture.halfSize.x, -fixture.halfSize.y},
                { fixture.halfSize.x,  fixture.halfSize.y},
                {-fixture.halfSize.x,  fixture.halfSize.y},
            };
            for (int corner = 0; corner < 4; ++corner) {
                Vec2 const local{
                    fixture.offset.x + corners[corner].x,
                    fixture.offset.y + corners[corner].y,
                };
                Vec2 const rotated = rotatePoint(local, pose.angle);
                vertices[corner] = mapPoint({
                    pose.position.x + rotated.x,
                    pose.position.y + rotated.y,
                });
            }
            m_previewDraw->drawPolygon(vertices, 4, fill, 1.f, border);
        }
    }
}

void PhysicsPopup::tick(float dt) {
    if (!m_playing || m_trace.frames.empty()) return;
    m_elapsed += dt;
    float const duration = m_trace.frames.back().time;
    if (m_elapsed > duration + 0.45f) m_elapsed = 0.f;
    drawPreview(std::min(m_elapsed, duration));
}

void PhysicsPopup::setStatus(std::string const& text, ccColor3B color) {
    if (!m_statusLabel) return;
    m_statusLabel->setColor(color);
    m_statusLabel->setString(text.c_str());
    m_statusLabel->limitLabelWidth(475.f, 0.265f, 0.16f);
}

} // namespace paimon::editorphysics
