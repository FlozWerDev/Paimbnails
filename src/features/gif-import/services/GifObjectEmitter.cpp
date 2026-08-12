#include "GifObjectEmitter.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../collab-editor/CollabManager.hpp"

#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace paimon::gifimport {

namespace {

constexpr int kSolidColorObject = 211;
constexpr int kCircleObject = 3637;
constexpr int kTriangleObject = 693;
constexpr int kWideTriangleObject = 694;
constexpr int kAlphaTrigger = 1007;
constexpr int kSpawnTrigger = 1268;

struct ObjectShape {
    int id = kSolidColorObject;
    float width = 30.f;
    float height = 30.f;
};

ObjectShape shapeFor(PrimitiveKind kind) {
    switch (kind) {
        case PrimitiveKind::Circle: return {kCircleObject, 50.f, 50.f};
        case PrimitiveKind::Triangle: return {kTriangleObject, 30.f, 30.f};
        case PrimitiveKind::WideTriangle: return {kWideTriangleObject, 60.f, 30.f};
        case PrimitiveKind::Block:
        case PrimitiveKind::Stroke: return {kSolidColorObject, 30.f, 30.f};
    }
    return {};
}

bool bitAt(VisibilityTrack const& track, int frame) {
    return (track.mask[static_cast<std::size_t>(frame / 64)] &
            (std::uint64_t{1} << (frame % 64))) != 0;
}

void appendGroups(std::string& save, int group) {
    if (group > 0) save += fmt::format(",57,{}", group);
}

void appendPrimitive(
    std::string& payload,
    Primitive const& object,
    std::vector<int> const& colors,
    float pixelSize,
    CCPoint origin,
    int imageHeight,
    int group
) {
    auto const shape = shapeFor(object.kind);
    float const x = origin.x + object.x * pixelSize;
    float const y = origin.y + (imageHeight - object.y) * pixelSize;
    float const scaleX = object.width * pixelSize / shape.width;
    float const scaleY = object.height * pixelSize / shape.height;
    int const color = colors[static_cast<std::size_t>(object.color)];
    float const rotation = object.rotation +
        (object.kind == PrimitiveKind::Triangle || object.kind == PrimitiveKind::WideTriangle
            ? 180.f : 0.f);
    payload += fmt::format(
        "1,{},2,{:.3f},3,{:.3f},21,{},64,1,67,1,121,1,134,1,128,{:.4f},129,{:.4f}",
        shape.id, x, y, color, scaleX, scaleY
    );
    if (std::abs(rotation) > 0.001f) {
        payload += fmt::format(",6,{:.3f}", rotation);
    }
    appendGroups(payload, group);
    payload += ';';
}

void appendAlpha(
    std::string& payload,
    float x,
    float y,
    int target,
    bool visible,
    int eventGroup
) {
    payload += fmt::format(
        "1,{},2,{:.3f},3,{:.3f},10,0,35,{},51,{},87,1",
        kAlphaTrigger, x, y, visible ? 1 : 0, target
    );
    if (eventGroup > 0) payload += ",62,1";
    appendGroups(payload, eventGroup);
    payload += ';';
}

void appendSpawn(
    std::string& payload,
    float x,
    float y,
    int target,
    float delay,
    int eventGroup
) {
    payload += fmt::format(
        "1,{},2,{:.3f},3,{:.3f},51,{},63,{:.3f},87,1",
        kSpawnTrigger, x, y, target, delay
    );
    if (eventGroup > 0) payload += ",62,1";
    appendGroups(payload, eventGroup);
    payload += ';';
}

std::vector<int> freeColorChannels(GJEffectManager* effects, std::size_t count) {
    std::vector<int> channels;
    channels.reserve(count);
    for (int id = 1; id <= 999 && channels.size() < count; ++id) {
        if (!effects->colorExists(id)) channels.push_back(id);
    }
    return channels;
}

std::vector<int> freeGroups(LevelEditorLayer* editor, std::size_t count) {
    std::vector<int> groups;
    groups.reserve(count);
    gd::unordered_set<int> excluded;
    for (std::size_t i = 0; i < count; ++i) {
        int const id = editor->getNextFreeGroupID(excluded);
        if (id <= 0 || id > 9999) break;
        groups.push_back(id);
        excluded.insert(id);
    }
    return groups;
}

void removeColors(GJEffectManager* effects, std::vector<int> const& channels) {
    for (int channel : channels) effects->removeColorAction(channel);
}

bool validateObjects(LevelEditorLayer* editor, ImportMode mode) {
    std::array<int, 4> const ids{
        kSolidColorObject, kCircleObject, kTriangleObject, kWideTriangleObject
    };
    std::size_t const count = mode == ImportMode::Art ? ids.size() : 1;
    for (std::size_t i = 0; i < count; ++i) {
        auto* probe = editor->createObject(ids[i], {-10000.f, -10000.f}, true);
        bool const solid = ids[i] == kSolidColorObject ||
            ids[i] == kTriangleObject || ids[i] == kWideTriangleObject;
        bool const valid = probe &&
            (solid ? probe->m_isSolidColorBlock : probe->canChangeMainColor());
        if (probe) editor->removeObject(probe, true);
        if (!valid) return false;
    }
    return true;
}

} // namespace

Result<EmitReport> emitToEditor(
    EditorUI* ui,
    ImportPlan const& plan,
    Options const& options,
    CCPoint origin
) {
    if (!paimon::modules::isEnabled("paimbnails.gifimport.editor")) {
        return Err("El modulo GIF a Objetos esta desactivado.");
    }
    if (!ui || !ui->m_editorLayer) return Err("El editor ya no esta disponible.");
    if (plan.palette.empty() || plan.totalObjects == 0) return Err("El plan no contiene objetos.");

    auto* editor = ui->m_editorLayer;
    auto* settings = editor->m_levelSettings;
    auto* effects = settings ? settings->m_effectManager : nullptr;
    if (!effects) return Err("No se pudo acceder a los colores del nivel.");

    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected() && !collab.canEditObjects()) {
        return Err("La sesion Collab esta en modo solo lectura.");
    }
    if (collab.connected() && !collab.clientCanEditColors()) {
        return Err("El host no permite editar los canales de color.");
    }
    if (!validateObjects(editor, plan.mode)) {
        return Err("Esta version de GD no expone las figuras de color esperadas.");
    }

    auto channels = freeColorChannels(effects, plan.palette.size());
    if (channels.size() != plan.palette.size()) {
        return Err("No hay suficientes canales de color libres (1-999).");
    }

    bool const hasAnimation = plan.animated() && !plan.tracks.empty();
    std::size_t const eventGroupCount = hasAnimation
        ? animationEventGroupCount(plan.frames.size(), options.loop)
        : 0;
    std::size_t const groupCount = hasAnimation ? plan.tracks.size() + eventGroupCount : 0;
    auto groups = freeGroups(editor, groupCount);
    if (groups.size() != groupCount) {
        return Err("No hay suficientes grupos libres para animar el GIF.");
    }

    std::vector<int> stateGroups;
    std::vector<int> eventGroups;
    if (hasAnimation) {
        stateGroups.assign(groups.begin(), groups.begin() + static_cast<std::ptrdiff_t>(plan.tracks.size()));
        eventGroups.assign(groups.begin() + static_cast<std::ptrdiff_t>(plan.tracks.size()), groups.end());
    }

    std::string payload;
    payload.reserve(plan.totalObjects * 112);
    for (auto const& object : plan.staticObjects) {
        appendPrimitive(payload, object, channels, options.pixelSize, origin, plan.height, 0);
    }
    for (std::size_t i = 0; i < plan.tracks.size(); ++i) {
        for (auto const& object : plan.tracks[i].objects) {
            appendPrimitive(
                payload, object, channels, options.pixelSize, origin, plan.height, stateGroups[i]);
        }
    }

    if (hasAnimation) {
        float const triggerY = origin.y - 45.f;
        float const startX = std::max(15.f, origin.x - 90.f);
        std::size_t triggerIndex = 0;
        auto triggerPosition = [&] {
            CCPoint const position{
                origin.x + static_cast<float>(triggerIndex % 24) * 8.f,
                triggerY - static_cast<float>(triggerIndex / 24) * 8.f
            };
            ++triggerIndex;
            return position;
        };

        for (std::size_t i = 0; i < plan.tracks.size(); ++i) {
            if (bitAt(plan.tracks[i], 0)) continue;
            auto const position = triggerPosition();
            appendAlpha(payload, 15.f, position.y, stateGroups[i], false, 0);
        }

        auto eventGroupForFrame = [&](std::size_t frame) {
            return options.loop ? eventGroups[frame] : eventGroups[frame - 1];
        };

        appendSpawn(
            payload, startX, triggerY, eventGroupForFrame(1),
            std::max(plan.frames.front().delayMs, 10) / 1000.f, 0
        );
        std::size_t const firstTransition = options.loop ? 0 : 1;
        for (std::size_t frame = firstTransition; frame < plan.frames.size(); ++frame) {
            int const eventGroup = eventGroupForFrame(frame);
            std::size_t const previous = frame == 0 ? plan.frames.size() - 1 : frame - 1;
            for (std::size_t track = 0; track < plan.tracks.size(); ++track) {
                bool const changed =
                    bitAt(plan.tracks[track], static_cast<int>(frame)) !=
                    bitAt(plan.tracks[track], static_cast<int>(previous));
                if (!changed) continue;
                auto const position = triggerPosition();
                appendAlpha(
                    payload, position.x, position.y, stateGroups[track],
                    bitAt(plan.tracks[track], static_cast<int>(frame)), eventGroup
                );
            }

            bool const hasNext = frame + 1 < plan.frames.size();
            if (hasNext || options.loop) {
                std::size_t const next = hasNext ? frame + 1 : 0;
                auto const position = triggerPosition();
                appendSpawn(
                    payload, position.x, position.y, eventGroupForFrame(next),
                    std::max(plan.frames[frame].delayMs, 10) / 1000.f, eventGroup
                );
            }
        }
    }

    if (static_cast<std::size_t>(std::count(payload.begin(), payload.end(), ';')) !=
        plan.totalObjects) {
        return Err("El plan genero una cantidad de objetos inconsistente.");
    }

    for (std::size_t i = 0; i < plan.palette.size(); ++i) {
        auto const& color = plan.palette[i];
        ccColor3B const gdColor{color.r, color.g, color.b};
        auto* action = ColorAction::create(gdColor, false, 0);
        if (!action) {
            removeColors(effects, std::vector<int>(
                channels.begin(), channels.begin() + static_cast<std::ptrdiff_t>(i)));
            return Err("No se pudo crear la paleta del GIF.");
        }
        action->m_colorID = channels[i];
        action->m_color = gdColor;
        action->m_fromColor = gdColor;
        action->m_toColor = gdColor;
        action->m_fromOpacity = 1.f;
        action->m_toOpacity = 1.f;
        effects->setColorAction(action, channels[i]);
    }

    CCArray* created = editor->createObjectsFromString(payload, false, true);
    if (!created || created->count() != plan.totalObjects) {
        if (created) {
            for (auto* item : CCArrayExt<CCObject*>(created)) {
                if (auto* object = typeinfo_cast<GameObject*>(item)) editor->removeObject(object, true);
            }
        }
        removeColors(effects, channels);
        return Err("GD no pudo crear todos los objetos; no se aplico la importacion.");
    }

    editor->updateObjectColors(created);
    editor->dirtifyTriggers();
    ui->deselectAll();
    ui->selectObjects(created, false);

    if (collab.connected()) {
        collab.sendCreatedObjects(created);
        collab.sendLevelSettings(false);
    }

    return Ok(EmitReport{
        plan.totalObjects,
        static_cast<int>(channels.size()),
        static_cast<int>(groups.size())
    });
}

} // namespace paimon::gifimport
