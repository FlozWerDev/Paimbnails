#include "PhysicsWorkspace.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/EffectGameObject.hpp>
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

std::vector<GameObject*> selectionOf(EditorUI* ui) {
    std::vector<GameObject*> objects;
    if (!ui) return objects;
    if (auto* selected = ui->getSelectedObjects()) {
        objects.reserve(selected->count());
        for (auto* item : CCArrayExt<CCObject*>(selected)) {
            auto* object = typeinfo_cast<GameObject*>(item);
            if (!object || typeinfo_cast<EffectGameObject*>(object)) continue;
            objects.push_back(object);
        }
    }
    if (objects.empty() && ui->m_selectedObject &&
        !typeinfo_cast<EffectGameObject*>(ui->m_selectedObject)) {
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
    body.gravityScale = body.motion == Motion::Dynamic ? 1.f : 0.f;
    body.exactGroup = exactGroup(ui->m_editorLayer, objects);
    body.objects.reserve(objects.size());
    for (auto* object : objects) body.objects.emplace_back(object);

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
    bool hasStatic = false;
    for (auto const& captured : m_bodies) {
        ResolvedBody body;
        body.spec.motion = captured.motion;
        bool const driven = captured.motion == Motion::Dynamic && captured.gravityScale > 0.f;
        body.spec.velocity = driven
            ? Vec2{config.velocityX, config.velocityY}
            : Vec2{};
        body.spec.angularVelocity = driven
            ? config.spinDegrees * kDegreesToRadians
            : 0.f;
        body.spec.gravityScale = captured.gravityScale;
        body.spec.restitution = config.restitution;
        body.spec.friction = config.friction;

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        for (auto const& weak : captured.objects) {
            auto object = weak.lock();
            if (!object || !object->getParent()) {
                return Err("Uno de los objetos capturados ya no existe en el nivel.");
            }
            auto const rect = ui->m_editorLayer->getObjectRect(object.data(), true, false);
            minX = std::min(minX, rect.getMinX());
            minY = std::min(minY, rect.getMinY());
            maxX = std::max(maxX, rect.getMaxX());
            maxY = std::max(maxY, rect.getMaxY());
            body.objects.push_back(object.data());
        }
        if (body.objects.empty() || !std::isfinite(minX) || !std::isfinite(minY) ||
            !std::isfinite(maxX) || !std::isfinite(maxY)) {
            return Err("No se pudo medir uno de los cuerpos.");
        }

        body.spec.position = {(minX + maxX) * 0.5f, (minY + maxY) * 0.5f};
        float area = 0.f;
        body.spec.fixtures.reserve(body.objects.size());
        for (auto* object : body.objects) {
            auto const rect = ui->m_editorLayer->getObjectRect(object, true, false);
            float const width = std::max(rect.size.width, 1.f);
            float const height = std::max(rect.size.height, 1.f);
            Vec2 const center{rect.getMidX(), rect.getMidY()};
            body.spec.fixtures.push_back({
                {
                    center.x - body.spec.position.x,
                    center.y - body.spec.position.y,
                },
                {width * 0.5f, height * 0.5f},
            });
            area += width * height;
        }
        body.spec.mass = std::clamp(area / 900.f, 0.1f, 1000.f);
        body.preferredGroup = exactGroup(ui->m_editorLayer, body.objects);
        resolved.push_back(std::move(body));
        hasDynamic |= captured.motion == Motion::Dynamic;
        hasStatic |= captured.motion == Motion::Static;
    }

    if (!hasDynamic) return Err("La simulacion necesita al menos un cuerpo dinamico.");
    if (!hasStatic && resolved.size() == 1) {
        return Err("Captura un cuerpo B fijo o agrega otro cuerpo para que A pueda colisionar.");
    }
    return Ok(std::move(resolved));
}

Result<Motion> PhysicsWorkspace::toggleMotion(std::size_t index) {
    if (!paimon::modules::isEnabled("paimbnails.physics.editor")) {
        return Err("El Simulador de Fisicas esta desactivado.");
    }
    if (index >= m_bodies.size()) return Err("Ese cuerpo aun no fue capturado.");
    auto& motion = m_bodies[index].motion;
    motion = motion == Motion::Dynamic ? Motion::Static : Motion::Dynamic;
    if (motion == Motion::Dynamic && index == 1) m_bodies[index].gravityScale = 0.f;
    return Ok(motion);
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
