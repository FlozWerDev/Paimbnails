#include "PhysicsWorkspace.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>

using namespace geode::prelude;

namespace paimon::editorphysics {

namespace {

constexpr float kDegreesToRadians = 0.01745329251994329577f;

// orbs, rings, pads and portals are effectgameobject subclasses too, so class
// filtering threw them out with the triggers; only m_istrigger is excluded.
bool isPhysicalObject(GameObject* object) {
    return object && !object->m_isTrigger;
}

std::vector<GameObject*> selectionOf(EditorUI* ui) {
    std::vector<GameObject*> objects;
    if (!ui) return objects;
    if (auto* selected = ui->getSelectedObjects()) {
        objects.reserve(selected->count());
        for (auto* item : CCArrayExt<CCObject*>(selected)) {
            auto* object = typeinfo_cast<GameObject*>(item);
            if (!isPhysicalObject(object)) continue;
            objects.push_back(object);
        }
    }
    if (objects.empty() && isPhysicalObject(ui->m_selectedObject)) {
        objects.push_back(ui->m_selectedObject);
    }
    std::sort(objects.begin(), objects.end());
    objects.erase(std::unique(objects.begin(), objects.end()), objects.end());
    return objects;
}

bool sameObjects(CCArray* group, std::vector<GameObject*> const& selected) {
    if (!group || group->count() != selected.size()) return false;
    std::unordered_set<GameObject*> expected(selected.begin(), selected.end());
    for (auto* item : CCArrayExt<CCObject*>(group)) {
        auto* object = typeinfo_cast<GameObject*>(item);
        if (!object || !expected.erase(object)) return false;
    }
    return expected.empty();
}

int exactGroup(LevelEditorLayer* editor, std::vector<GameObject*> const& objects) {
    if (!editor || objects.empty()) return 0;
    std::set<int> candidates;
    for (int i = 0; i < objects.front()->m_groupCount; ++i) {
        int const group = objects.front()->getGroupID(i);
        if (group > 0) candidates.insert(group);
    }
    for (auto* object : objects) {
        std::set<int> current;
        for (int i = 0; i < object->m_groupCount; ++i) {
            int const group = object->getGroupID(i);
            if (group > 0) current.insert(group);
        }
        std::erase_if(candidates, [&](int group) { return !current.contains(group); });
    }
    for (int group : candidates) {
        if (sameObjects(editor->getGroup(group), objects)) return group;
    }
    return 0;
}

bool containsObject(CapturedBody const& body, GameObject* object) {
    for (auto const& weak : body.objects) {
        if (auto locked = weak.lock(); locked && locked.data() == object) return true;
    }
    return false;
}

std::size_t replaceIndex(CaptureRole role, std::size_t size) {
    if (role == CaptureRole::ReplaceA) return 0;
    if (role == CaptureRole::ReplaceB) return std::min<std::size_t>(1, size);
    return size;
}

Motion motionFor(CaptureRole role) {
    return role == CaptureRole::ReplaceA || role == CaptureRole::AddDynamic
        ? Motion::Dynamic
        : Motion::Static;
}

} // namespace

PhysicsWorkspace& PhysicsWorkspace::get() {
    static PhysicsWorkspace workspace;
    return workspace;
}

void PhysicsWorkspace::bind(EditorUI* ui) {
    auto current = m_ui.lock();
    if (current && current.data() == ui) return;
    m_ui = ui;
    m_bodies.clear();
    m_pending.reset();
}

Result<CaptureReport> PhysicsWorkspace::capture(EditorUI* ui, CaptureRole role) {
    if (!paimon::modules::isEnabled("paimbnails.physics.editor")) {
        return Err("El Simulador de Fisicas esta desactivado.");
    }
    if (!ui || !ui->m_editorLayer) return Err("El editor ya no esta disponible.");
    bind(ui);
    if (role == CaptureRole::ReplaceB && m_bodies.empty()) {
        return Err("Captura el cuerpo A antes de elegir B.");
    }

    auto objects = selectionOf(ui);
    if (objects.empty()) {
        return Err("Selecciona uno o varios objetos visibles; los triggers no cuentan como cuerpo.");
    }
    if (objects.size() > 1000) return Err("Un cuerpo puede contener como maximo 1000 objetos.");

    std::size_t const target = replaceIndex(role, m_bodies.size());
    for (std::size_t i = 0; i < m_bodies.size(); ++i) {
        if (i == target) continue;
        for (auto* object : objects) {
            if (containsObject(m_bodies[i], object)) {
                return Err("Un objeto no puede pertenecer a dos cuerpos de la misma simulacion.");
            }
        }
    }

    CapturedBody body;
    body.motion = motionFor(role);
    body.material.gravityScale = body.motion == Motion::Dynamic ? 1.f : 0.f;
    body.exactGroup = exactGroup(ui->m_editorLayer, objects);
    body.objects.reserve(objects.size());
    for (auto* object : objects) body.objects.emplace_back(object);
    body.materials.resize(objects.size());

    if (target < m_bodies.size()) {
        m_bodies[target] = std::move(body);
    } else {
        if (m_bodies.size() >= 16) return Err("La simulacion admite hasta 16 cuerpos.");
        m_bodies.push_back(std::move(body));
    }

    return Ok(CaptureReport{objects.size(), m_bodies[target].exactGroup});
}

Result<CaptureReport> PhysicsWorkspace::consumePending(EditorUI* ui) {
    if (!m_pending) return Err("No hay una captura pendiente.");
    auto const role = *m_pending;
    m_pending.reset();
    return capture(ui, role);
}

Result<std::vector<ResolvedBody>> PhysicsWorkspace::resolve(
    EditorUI* ui,
    LabConfig const& config
) const {
    if (!paimon::modules::isEnabled("paimbnails.physics.editor")) {
        return Err("El Simulador de Fisicas esta desactivado.");
    }
    if (!ui || !ui->m_editorLayer) return Err("El editor ya no esta disponible.");
    auto current = m_ui.lock();
    if (!current || current.data() != ui) return Err("La seleccion pertenece a otro editor.");
    if (m_bodies.empty()) return Err("Primero captura el cuerpo A.");

    std::vector<ResolvedBody> resolved;
    resolved.reserve(m_bodies.size());
    bool hasDynamic = false;
    for (auto const& captured : m_bodies) {
        ResolvedBody body;
        auto const& material = captured.material;
        body.spec.motion = captured.motion;
        body.native = captured.native;
        // a body with its own launch is launched whatever its gravity does; the
        // lab velocity still only reaches the ones gravity is pulling on.
        bool const driven = captured.motion == Motion::Dynamic &&
            std::abs(material.gravityScale) > 0.0001f;
        body.spec.velocity = material.customLaunch
            ? material.launch
            : driven ? Vec2{config.velocityX, config.velocityY} : Vec2{};
        body.spec.angularVelocity = (material.customLaunch
            ? material.spinDegrees
            : driven ? config.spinDegrees : 0.f) * kDegreesToRadians;
        body.spec.gravityScale = material.gravityScale;
        body.spec.restitution = material.restitution >= 0.f
            ? material.restitution
            : config.restitution;
        body.spec.friction = material.friction >= 0.f ? material.friction : config.friction;

        std::vector<ObjectShape> shapes;
        shapes.reserve(captured.objects.size());
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        for (auto const& weak : captured.objects) {
            auto object = weak.lock();
            if (!object || !object->getParent()) {
                return Err("Uno de los objetos capturados ya no existe en el nivel.");
            }
            auto const shape = shapeOf(ui->m_editorLayer, object.data());
            minX = std::min(minX, shape.center.x - shape.halfSize.x);
            minY = std::min(minY, shape.center.y - shape.halfSize.y);
            maxX = std::max(maxX, shape.center.x + shape.halfSize.x);
            maxY = std::max(maxY, shape.center.y + shape.halfSize.y);
            shapes.push_back(shape);
            body.objects.push_back(object.data());
        }
        if (body.objects.empty() || !std::isfinite(minX) || !std::isfinite(minY) ||
            !std::isfinite(maxX) || !std::isfinite(maxY)) {
            return Err("No se pudo medir uno de los cuerpos.");
        }

        // the origin is the area weighted centroid: the box middle put the pivot
        // outside an l shape and made a slope spin like the block it fills.
        float area = 0.f;
        Vec2 weighted{};
        for (auto const& shape : shapes) {
            float const part = std::max(shapeArea(shape), 1.f);
            auto const centroid = shapeCentroid(shape);
            weighted.x += centroid.x * part;
            weighted.y += centroid.y * part;
            area += part;
        }
        body.spec.position = {weighted.x / area, weighted.y / area};
        body.spec.fixtures.reserve(body.objects.size());
        body.visuals.reserve(body.objects.size());
        for (std::size_t index = 0; index < body.objects.size(); ++index) {
            auto* object = body.objects[index];
            auto const& shape = shapes[index];
            auto fixture = fixtureFrom(shape, body.spec.position);
            if (index < captured.materials.size()) {
                fixture.friction = captured.materials[index].friction;
                fixture.restitution = captured.materials[index].restitution;
            }
            body.spec.fixtures.push_back(fixture);

            BodyVisual visual;
            visual.object = object;
            visual.objectID = object->m_objectID;
            // the art hangs off the object's own position, not off its hitbox
            // centre, which are different things for slopes and extended blocks.
            visual.offset = {
                object->getPositionX() - body.spec.position.x,
                object->getPositionY() - body.spec.position.y,
            };
            visual.size = {shape.halfSize.x * 2.f, shape.halfSize.y * 2.f};
            visual.rotation = object->getRotation();
            visual.scaleX = object->m_scaleX;
            visual.scaleY = object->m_scaleY;
            visual.flipX = object->isFlipX();
            visual.flipY = object->isFlipY();
            visual.zOrder = object->getZOrder();
            visual.baseColor = object->getColor();
            visual.baseOpacity = object->getOpacity();
            if (auto* detail = object->m_colorSprite) {
                visual.detailColor = detail->getColor();
                visual.detailOpacity = detail->getOpacity();
            } else {
                visual.detailColor = visual.baseColor;
                visual.detailOpacity = visual.baseOpacity;
            }
            visual.kind = shape.kind;
            body.visuals.push_back(visual);

            switch (shape.kind) {
                case ShapeKind::Ramp: ++body.shapes.ramps; break;
                case ShapeKind::Round: ++body.shapes.rounds; break;
                case ShapeKind::Hull: ++body.shapes.hulls; break;
                case ShapeKind::Box: ++body.shapes.boxes; break;
            }
        }
        body.spec.mass = material.mass > 0.f
            ? material.mass
            : std::clamp(area / 900.f, 0.1f, 1000.f);
        body.preferredGroup = exactGroup(ui->m_editorLayer, body.objects);
        resolved.push_back(std::move(body));
        hasDynamic |= captured.motion == Motion::Dynamic;
    }

    if (!hasDynamic) return Err("La simulacion necesita al menos un cuerpo dinamico.");
    return Ok(std::move(resolved));
}

Result<Motion> PhysicsWorkspace::toggleMotion(std::size_t index) {
    if (!paimon::modules::isEnabled("paimbnails.physics.editor")) {
        return Err("El Simulador de Fisicas esta desactivado.");
    }
    if (index >= m_bodies.size()) return Err("Ese cuerpo aun no fue capturado.");
    auto& motion = m_bodies[index].motion;
    motion = motion == Motion::Dynamic ? Motion::Static : Motion::Dynamic;
    if (motion == Motion::Dynamic && index == 1) m_bodies[index].material.gravityScale = 0.f;
    return Ok(motion);
}

BodyMaterial* PhysicsWorkspace::material(std::size_t index) {
    return index < m_bodies.size() ? &m_bodies[index].material : nullptr;
}

NativeBodySettings* PhysicsWorkspace::nativeSettings(std::size_t index) {
    return index < m_bodies.size() ? &m_bodies[index].native : nullptr;
}

ObjectMaterial* PhysicsWorkspace::objectMaterial(std::size_t index, std::size_t object) {
    if (index >= m_bodies.size() || object >= m_bodies[index].materials.size()) return nullptr;
    return &m_bodies[index].materials[object];
}

void PhysicsWorkspace::beginCapture(CaptureRole role) {
    m_pending = role;
}

void PhysicsWorkspace::clear() {
    m_bodies.clear();
    m_pending.reset();
}

bool PhysicsWorkspace::hasPendingCapture() const {
    return m_pending.has_value();
}

bool PhysicsWorkspace::empty() const {
    return m_bodies.empty();
}

std::vector<CapturedBody> const& PhysicsWorkspace::bodies() const {
    return m_bodies;
}

} // namespace paimon::editorphysics
