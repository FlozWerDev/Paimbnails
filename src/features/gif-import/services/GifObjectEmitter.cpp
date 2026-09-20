#include "GifObjectEmitter.hpp"
#include "GifImportPipeline.hpp"
#include "GifMotionPlanner.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../collab-editor/CollabManager.hpp"

#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/utils/general.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
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
constexpr int kMoveTrigger = 901;
constexpr int kGradientTrigger = 2903;

struct ObjectShape {
    int id = kSolidColorObject;
    float width = 30.f;
    float height = 30.f;
    int zLayer = 0;
};

struct PreparedImport {
    std::string payload;
    std::vector<int> channels;
    std::vector<int> groups;
    std::size_t objects = 0;
};

// WeakRef's pool is main-thread-only, so the worker hands back plain C++ state.
struct AsyncProgress {
    std::atomic<float> value{0.f};
    std::atomic<int> stage{static_cast<int>(BuildStage::Preparing)};
    std::atomic<int> pass{0};
    std::atomic<int> passes{0};
    std::mutex mutex;
    std::optional<BuildResult> result;
};

using ShapeTable = std::array<ObjectShape, kPrimitiveKinds>;

std::size_t shapeIndex(PrimitiveKind kind) {
    return static_cast<std::size_t>(kind);
}

ShapeTable defaultShapes() {
    ShapeTable shapes{};
    shapes[shapeIndex(PrimitiveKind::Block)] = {kSolidColorObject, 30.f, 30.f};
    shapes[shapeIndex(PrimitiveKind::Stroke)] = {kSolidColorObject, 30.f, 30.f};
    shapes[shapeIndex(PrimitiveKind::Circle)] = {kCircleObject, 50.f, 50.f};
    shapes[shapeIndex(PrimitiveKind::Triangle)] = {kTriangleObject, 30.f, 30.f};
    shapes[shapeIndex(PrimitiveKind::WideTriangle)] = {kWideTriangleObject, 60.f, 30.f};
    shapes[shapeIndex(PrimitiveKind::Glow)] = {kSolidColorObject, 30.f, 30.f};
    return shapes;
}

void appendGroups(std::string& save, int group) {
    if (group > 0) save += fmt::format(",57,{}", group);
}

void appendPrimitive(
    std::string& payload,
    Primitive const& object,
    ShapeTable const& shapes,
    std::vector<PlanStamp> const& stamps,
    std::vector<int> const& colors,
    float pixelSize,
    CCPoint origin,
    int imageHeight,
    int group,
    bool layered,
    int zLayer
) {
    auto const& shape = shapes[shapeIndex(object.kind)];
    bool const stamped =
        object.kind == PrimitiveKind::Stamp && object.stamp < stamps.size();
    auto const& stamp = stamped ? stamps[object.stamp] : PlanStamp{};
    float const boxWidth = object.width * pixelSize;
    float const boxHeight = object.height * pixelSize;
    // El molde va recortado a lo que pinta, asi que el objeto entero se corre
    // para que ese recorte caiga donde el plan lo dejo.
    float x = origin.x + (object.x + stamp.offsetX * object.width + 0.5f) * pixelSize;
    float y = origin.y +
        (imageHeight - object.y - stamp.offsetY * object.height - 0.5f) * pixelSize;
    float scaleX = boxWidth / shape.width;
    float scaleY = boxHeight / shape.height;
    if (stamped) {
        // GD escala antes de girar, asi que a un cuarto de vuelta los dos ejes
        // se cruzan: lo ancho de la caja lo da el alto del arte.
        bool const quarter =
            std::abs(std::fmod(std::abs(stamp.rotation), 180.f) - 90.f) < 0.5f;
        scaleX = (quarter ? boxHeight : boxWidth) /
            (quarter ? stamp.baseHeight : stamp.baseWidth);
        scaleY = (quarter ? boxWidth : boxHeight) /
            (quarter ? stamp.baseWidth : stamp.baseHeight);
    }
    // Skipping a stray colour index would desync the object count the import
    // checks against, so the last channel takes it instead.
    int const color = colors[std::min<std::size_t>(object.color, colors.size() - 1)];
    float const rotation = stamped ? stamp.rotation : object.rotation +
        (object.kind == PrimitiveKind::Triangle || object.kind == PrimitiveKind::WideTriangle
            ? 180.f : 0.f);
    payload += fmt::format(
        "1,{},2,{:.3f},3,{:.3f},21,{},64,1,67,1,121,1,134,1,128,{:.4f},129,{:.4f}",
        stamped ? stamp.objectId : shape.id, x, y, color, scaleX, scaleY
    );
    if (std::abs(rotation) > 0.001f) {
        payload += fmt::format(",6,{:.3f}", rotation);
    }
    if (stamped && stamp.flipX) payload += ",4,1";
    if (layered) {
        payload += fmt::format(",25,{}", std::clamp<int>(object.layer, -999, 999));
        if (zLayer != 0) payload += fmt::format(",24,{}", zLayer);
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

void appendMove(
    std::string& payload,
    float x,
    float y,
    int target,
    float offsetX,
    float offsetY,
    float duration,
    int eventGroup
) {
    payload += fmt::format(
        "1,{},2,{:.3f},3,{:.3f},10,{:.3f},28,{:.3f},29,{:.3f},30,0,51,{},87,1",
        kMoveTrigger, x, y, duration, offsetX, offsetY, target
    );
    if (eventGroup > 0) payload += ",62,1";
    appendGroups(payload, eventGroup);
    payload += ';';
}

// Wash a pantalla completa del VertX en modo normal (207 ausente): las
// esquinas van duplicadas arriba/abajo y la rampa sale vertical sin depender
// de como interprete el juego el vertexMode.
void appendGradient(
    std::string& payload,
    float x,
    float y,
    int gradientID,
    int topChannel,
    int bottomChannel
) {
    payload += fmt::format(
        "1,{},2,{:.3f},3,{:.3f},203,{},204,{},205,{},206,{},209,{};",
        kGradientTrigger, x, y, bottomChannel, bottomChannel,
        topChannel, topChannel, gradientID);
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

bool measureShape(LevelEditorLayer* editor, ObjectShape& shape) {
    auto* probe = editor->createObject(shape.id, {-10000.f, -10000.f}, true);
    if (!probe) return false;
    bool const valid = probe->m_isSolidColorBlock || probe->canChangeMainColor();
    auto const size = probe->getContentSize();
    shape.zLayer = static_cast<int>(probe->m_defaultZLayer);
    editor->removeObject(probe, true);
    if (!valid) return false;
    if (size.width > 4.f && size.width < 240.f && size.height > 4.f && size.height < 240.f) {
        shape.width = size.width;
        shape.height = size.height;
    }
    return true;
}

// El orden Z (25) solo ordena dentro de una misma capa Z, asi que las figuras
// que traen otra capa por defecto se dibujarian encima de los cuadrados pase lo
// que pase. Si alguna no coincide, las mandamos todas a la capa del cuadrado.
int sharedZLayer(ShapeTable const& shapes, bool stamped) {
    int const block = shapes[shapeIndex(PrimitiveKind::Block)].zLayer;
    if (stamped) return block != 0 ? block : static_cast<int>(ZLayer::B1);
    for (auto kind : {PrimitiveKind::Stroke, PrimitiveKind::Circle,
                      PrimitiveKind::Triangle, PrimitiveKind::WideTriangle}) {
        if (shapes[shapeIndex(kind)].zLayer == block) continue;
        return block != 0 ? block : static_cast<int>(ZLayer::B1);
    }
    return 0;
}

// El save de GD guarda el Move en unidades suyas, no en pixeles. En vez de dar
// por buena la constante se crea un trigger de prueba con un valor conocido y se
// le pregunta al propio juego en cuanto lo tradujo.
float moveUnitScale(LevelEditorLayer* editor) {
    float scale = 3.f;
    auto* created = editor->createObjectsFromString(
        fmt::format("1,{},2,-10000,3,-10000,28,100,29,0;", kMoveTrigger), true, true);
    if (!created) return scale;
    for (auto* item : CCArrayExt<CCObject*>(created)) {
        if (auto* trigger = typeinfo_cast<EffectGameObject*>(item)) {
            float const offset = trigger->m_moveOffset.x;
            if (offset > 1.f && offset < 100000.f) scale = offset / 100.f;
        }
        if (auto* object = typeinfo_cast<GameObject*>(item)) {
            editor->removeObject(object, true);
        }
    }
    return scale;
}

bool resolveShapes(LevelEditorLayer* editor, ImportMode mode, ShapeTable& shapes) {
    if (!measureShape(editor, shapes[shapeIndex(PrimitiveKind::Block)])) return false;
    shapes[shapeIndex(PrimitiveKind::Stroke)] = shapes[shapeIndex(PrimitiveKind::Block)];
    shapes[shapeIndex(PrimitiveKind::Glow)] = shapes[shapeIndex(PrimitiveKind::Block)];
    if (mode == ImportMode::Blocks || usesSoftGeometry(mode)) return true;

    for (auto kind : {PrimitiveKind::Circle, PrimitiveKind::Triangle,
                      PrimitiveKind::WideTriangle}) {
        auto& shape = shapes[shapeIndex(kind)];
        if (measureShape(editor, shape)) continue;
        return false;
    }
    return true;
}

// La pasada relajada de la biblioteca acepta decoracion sin exigir tinte, asi
// que se verifica aqui: el blending sale del canal de color (toda la paleta
// suave es glow con glowPaletteStart=0) y vale igual para nativos y repuesto,
// pero un objeto que no acepta tinte pintaria su color original.
void verifySoftStamps(LevelEditorLayer* editor, ImportPlan const& plan) {
    if (!usesSoftGeometry(plan.mode) || plan.stamps.empty()) return;
    if (plan.glowPaletteStart != 0) {
        log::warn("[GifImport] Soft mode without glow palette start; blending may be off.");
    }
    std::unordered_set<int> seen;
    for (auto const& stamp : plan.stamps) {
        if (stamp.objectId <= 0 || !seen.insert(stamp.objectId).second) continue;
        auto* probe = editor->createObject(stamp.objectId, {-10000.f, -10000.f}, true);
        if (!probe) {
            log::warn("[GifImport] Soft stamp {} no longer spawns; import may show gaps.",
                stamp.objectId);
            continue;
        }
        bool const tintable =
            probe->m_isSolidColorBlock || probe->canChangeMainColor();
        if (!tintable) {
            log::warn("[GifImport] Soft stamp {} ignores tint; its cells keep the editor color.",
                stamp.objectId);
        }
        if (stamp.analyticFallback) {
            log::info("[GifImport] Fallback stamp {} emits with blending at glowOpacity {}.",
                stamp.objectId, plan.glowOpacity);
        }
        editor->removeObject(probe, true);
    }
}

Result<PreparedImport> prepareImport(
    EditorUI* ui,
    ImportPlan const& plan,
    Options const& options,
    CCPoint origin
) {
    if (!paimon::modules::isEnabled("paimbnails.gifimport.editor")) {
        return Err("El modulo GIF a Objetos esta desactivado.");
    }
    if (plan.mode == ImportMode::Render &&
        !paimon::modules::isEnabled("paimbnails.gifrender.editor")) {
        return Err("El submodulo Render esta desactivado.");
    }
    if (!ui || !ui->m_editorLayer) return Err("El editor ya no esta disponible.");
    if (plan.palette.empty() || plan.totalObjects == 0) {
        return Err("El plan no contiene objetos.");
    }

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

    auto shapes = defaultShapes();
    if (!resolveShapes(editor, plan.mode, shapes)) {
        return Err("Esta version de GD no expone las figuras de color esperadas.");
    }
    verifySoftStamps(editor, plan);

    auto channels = freeColorChannels(effects, plan.palette.size());
    if (channels.size() != plan.palette.size()) {
        return Err("No hay suficientes canales de color libres (1-999).");
    }

    bool const hasAnimation =
        plan.animated() && (!plan.tracks.empty() || !plan.motionTracks.empty());
    std::size_t const eventGroupCount = hasAnimation
        ? animationEventGroupCount(plan.frames.size(), options.loop)
        : 0;
    std::size_t const groupCount = hasAnimation
        ? plan.tracks.size() + plan.motionTracks.size() + eventGroupCount
        : 0;
    auto groups = freeGroups(editor, groupCount);
    if (groups.size() != groupCount) {
        return Err("No hay suficientes grupos libres para animar el GIF.");
    }

    std::vector<int> stateGroups;
    std::vector<int> motionGroups;
    std::vector<int> eventGroups;
    if (hasAnimation) {
        auto const states = static_cast<std::ptrdiff_t>(plan.tracks.size());
        auto const motions = static_cast<std::ptrdiff_t>(plan.motionTracks.size());
        stateGroups.assign(groups.begin(), groups.begin() + states);
        motionGroups.assign(groups.begin() + states, groups.begin() + states + motions);
        eventGroups.assign(groups.begin() + states + motions, groups.end());
    }

    float const moveScale = plan.motionTracks.empty() ? 3.f : moveUnitScale(editor);

    bool const layered = usesPaintGeometry(plan.mode) || usesSoftGeometry(plan.mode);
    int const zLayer = layered ? sharedZLayer(shapes, !plan.stamps.empty()) : 0;
    std::string payload;
    payload.reserve(plan.totalObjects * 112);
    for (auto const& object : plan.staticObjects) {
        appendPrimitive(
            payload, object, shapes, plan.stamps, channels, options.pixelSize, origin,
            plan.height, 0, layered, zLayer);
    }
    for (std::size_t i = 0; i < plan.tracks.size(); ++i) {
        for (auto const& object : plan.tracks[i].objects) {
            appendPrimitive(
                payload, object, shapes, plan.stamps, channels, options.pixelSize, origin,
                plan.height, stateGroups[i], layered, zLayer);
        }
    }
    for (std::size_t i = 0; i < plan.motionTracks.size(); ++i) {
        for (auto const& object : plan.motionTracks[i].objects) {
            appendPrimitive(
                payload, object, shapes, plan.stamps, channels, options.pixelSize, origin,
                plan.height, motionGroups[i], layered, zLayer);
        }
    }

    if (plan.gradientWash) {
        gd::unordered_set<int> exclude;
        int const gradientID = editor->getNextFreeGradientID(exclude);
        appendGradient(payload, origin.x, origin.y - 60.f, gradientID,
            channels[static_cast<std::size_t>(plan.washTop)],
            channels[static_cast<std::size_t>(plan.washBottom)]);
        log::info("[GifImport] VertX wash: gradientID={} top={} bottom={}.",
            gradientID, plan.washTop, plan.washBottom);
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
            if (visibleAt(plan.tracks[i].mask, 0)) continue;
            auto const position = triggerPosition();
            appendAlpha(payload, 15.f, position.y, stateGroups[i], false, 0);
        }
        for (std::size_t i = 0; i < plan.motionTracks.size(); ++i) {
            if (visibleAt(plan.motionTracks[i].mask, 0)) continue;
            auto const position = triggerPosition();
            appendAlpha(payload, 15.f, position.y, motionGroups[i], false, 0);
        }

        auto eventGroupForFrame = [&](std::size_t frame) {
            return options.loop ? eventGroups[frame] : eventGroups[frame - 1];
        };

        // Mover la figura a la pose del frame siguiente durante lo que dura el
        // actual la deja donde toca justo cuando ese frame entra, y de paso el
        // salto entre poses se ve como un desplazamiento y no como un parpadeo.
        auto appendMotionStep = [&](std::size_t frame, std::size_t next, int eventGroup) {
            for (std::size_t i = 0; i < plan.motionTracks.size(); ++i) {
                auto const& track = plan.motionTracks[i];
                auto const* from = keyAt(track, static_cast<int>(frame));
                auto const* to = keyAt(track, static_cast<int>(next));
                int stepX = 0;
                int stepY = 0;
                if (from && to) {
                    stepX = to->x - from->x;
                    stepY = to->y - from->y;
                } else if (from && options.loop) {
                    stepX = -from->x;
                    stepY = -from->y;
                } else {
                    continue;
                }
                if (stepX == 0 && stepY == 0) continue;
                auto const position = triggerPosition();
                float const duration = to && std::abs(stepX) + std::abs(stepY) <= 4
                    ? std::max(plan.frames[frame].delayMs, 10) / 1000.f
                    : 0.f;
                appendMove(
                    payload, position.x, position.y, motionGroups[i],
                    stepX * options.pixelSize / moveScale,
                    -stepY * options.pixelSize / moveScale,
                    duration, eventGroup);
            }
        };

        appendSpawn(
            payload, startX, triggerY, eventGroupForFrame(1),
            std::max(plan.frames.front().delayMs, 10) / 1000.f, 0);
        // Sin loop el frame cero no tiene grupo de evento propio: su movimiento
        // sale suelto y arranca con el nivel, igual que los apagados iniciales.
        if (!options.loop) appendMotionStep(0, 1, 0);
        std::size_t const firstTransition = options.loop ? 0 : 1;
        for (std::size_t frame = firstTransition; frame < plan.frames.size(); ++frame) {
            int const eventGroup = eventGroupForFrame(frame);
            std::size_t const previous = frame == 0 ? plan.frames.size() - 1 : frame - 1;
            for (std::size_t track = 0; track < plan.tracks.size(); ++track) {
                bool const changed =
                    visibleAt(plan.tracks[track].mask, static_cast<int>(frame)) !=
                    visibleAt(plan.tracks[track].mask, static_cast<int>(previous));
                if (!changed) continue;
                auto const position = triggerPosition();
                appendAlpha(
                    payload, position.x, position.y, stateGroups[track],
                    visibleAt(plan.tracks[track].mask, static_cast<int>(frame)), eventGroup);
            }
            for (std::size_t track = 0; track < plan.motionTracks.size(); ++track) {
                bool const changed =
                    visibleAt(plan.motionTracks[track].mask, static_cast<int>(frame)) !=
                    visibleAt(plan.motionTracks[track].mask, static_cast<int>(previous));
                if (!changed) continue;
                auto const position = triggerPosition();
                appendAlpha(
                    payload, position.x, position.y, motionGroups[track],
                    visibleAt(plan.motionTracks[track].mask, static_cast<int>(frame)),
                    eventGroup);
            }

            bool const hasNext = frame + 1 < plan.frames.size();
            if (hasNext || options.loop) {
                std::size_t const next = hasNext ? frame + 1 : 0;
                appendMotionStep(frame, next, eventGroup);
                auto const position = triggerPosition();
                appendSpawn(
                    payload, position.x, position.y, eventGroupForFrame(next),
                    std::max(plan.frames[frame].delayMs, 10) / 1000.f, eventGroup);
            }
        }
    }

    if (static_cast<std::size_t>(std::count(payload.begin(), payload.end(), ';')) !=
        plan.totalObjects) {
        return Err("El plan genero una cantidad de objetos inconsistente.");
    }
    return Ok(PreparedImport{
        std::move(payload), std::move(channels), std::move(groups), plan.totalObjects});
}

Result<> installPalette(
    GJEffectManager* effects,
    ImportPlan const& plan,
    std::vector<int> const& channels
) {
    for (std::size_t i = 0; i < plan.palette.size(); ++i) {
        auto const& color = plan.palette[i];
        bool const glow = i >= plan.glowPaletteStart &&
            static_cast<int>(i) != plan.softBackdropColor;
        float const opacity = glow ? plan.glowOpacity : 1.f;
        ccColor3B const gdColor{color.r, color.g, color.b};
        auto* action = ColorAction::create(gdColor, glow, 0);
        if (!action) {
            removeColors(effects, std::vector<int>(
                channels.begin(), channels.begin() + static_cast<std::ptrdiff_t>(i)));
            return Err("No se pudo crear la paleta del GIF.");
        }
        action->m_colorID = channels[i];
        action->m_color = gdColor;
        action->m_fromColor = gdColor;
        action->m_toColor = gdColor;
        action->m_blending = glow;
        action->m_currentOpacity = opacity;
        action->m_fromOpacity = opacity;
        action->m_toOpacity = opacity;
        effects->setColorAction(action, channels[i]);
    }
    return Ok();
}

void removeCreated(LevelEditorLayer* editor, CCArray* created) {
    if (!created) return;
    for (auto* item : CCArrayExt<CCObject*>(created)) {
        if (auto* object = typeinfo_cast<GameObject*>(item)) {
            editor->removeObject(object, true);
        }
    }
}

char const* stageText(BuildStage stage) {
    switch (stage) {
        case BuildStage::Preparing: return "Preparando";
        case BuildStage::Resizing: return "Revisando imagen";
        case BuildStage::Palette: return "Ajustando colores";
        case BuildStage::Geometry: return "Trazando curvas";
        case BuildStage::Reviewing: return "Comparando resultado";
        case BuildStage::Refining: return "Refinando";
        case BuildStage::Done: return "Listo";
    }
    return "Procesando";
}

class BackgroundImportJob : public CCNode {
public:
    static BackgroundImportJob* create(
        EditorUI* ui,
        std::shared_ptr<SourceAnimation> source,
        Options const& options,
        CCPoint center
    ) {
        auto* ret = new BackgroundImportJob();
        if (ret && ret->init(ui, std::move(source), options, center)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

private:
    enum class Phase {
        Analyzing,
        Emitting,
    };

    bool init(
        EditorUI* ui,
        std::shared_ptr<SourceAnimation> source,
        Options const& options,
        CCPoint center
    ) {
        if (!CCNode::init()) return false;
        m_ui = ui;
        m_source = std::move(source);
        m_options = options;
        m_center = center;
        m_progress = std::make_shared<AsyncProgress>();
        setID("gif-import-background-job"_spr);
        setContentSize({230.f, 36.f});

        auto* panel = paimon::SpriteHelper::createDarkPanel(230.f, 36.f, 220, 5.f);
        panel->setAnchorPoint({0.f, 0.f});
        addChild(panel);

        m_label = CCLabelBMFont::create("Preparando dibujo...", "bigFont.fnt");
        m_label->setScale(0.34f);
        m_label->setPosition({115.f, 22.f});
        addChild(m_label, 2);

        auto* track = CCLayerColor::create({0, 0, 0, 150}, 210.f, 4.f);
        track->ignoreAnchorPointForPosition(false);
        track->setAnchorPoint({0.f, 0.f});
        track->setPosition({10.f, 6.f});
        addChild(track, 1);

        m_fill = CCLayerColor::create({90, 225, 150, 255}, 210.f, 4.f);
        m_fill->ignoreAnchorPointForPosition(false);
        m_fill->setAnchorPoint({0.f, 0.f});
        m_fill->setPosition({10.f, 6.f});
        m_fill->setScaleX(0.f);
        addChild(m_fill, 2);

        auto const win = CCDirector::get()->getWinSize();
        setPosition({win.width * 0.5f - 115.f, win.height - 43.f});
        if (!startBuild()) return false;
        schedule(schedule_selector(BackgroundImportJob::tick));
        return true;
    }

    bool startBuild() {
        auto source = m_source;
        auto const options = m_options;
        auto progress = m_progress;
        return paimon::ThreadTracker::get().spawn([source, options, progress] {
            geode::utils::thread::setName("Paimon GIF Background Plan");
            auto result = buildPlan(
                *source, options, [progress](BuildProgress const& update) {
                    progress->value.store(update.value, std::memory_order_relaxed);
                    progress->stage.store(
                        static_cast<int>(update.stage), std::memory_order_relaxed);
                    progress->pass.store(update.pass, std::memory_order_relaxed);
                    progress->passes.store(update.passes, std::memory_order_relaxed);
                });
            std::lock_guard lock(progress->mutex);
            progress->result = std::move(result);
        });
    }

    void beginEmission(BuildResult result) {
        if (!result) {
            fail(result.error);
            return;
        }
        auto ui = m_ui.lock();
        auto* editor = LevelEditorLayer::get();
        if (!ui || !editor || ui->m_editorLayer != editor) {
            fail("El editor ya no esta disponible.");
            return;
        }

        float const margin = m_options.pixelSize;
        CCPoint const origin{
            m_center.x - (result.plan.width * m_options.pixelSize + margin) * 0.5f,
            m_center.y - (result.plan.height * m_options.pixelSize + margin) * 0.5f
        };
        auto preparedResult = prepareImport(ui.data(), result.plan, m_options, origin);
        if (preparedResult.isErr()) {
            fail(preparedResult.unwrapErr());
            return;
        }

        auto* settings = editor->m_levelSettings;
        auto* effects = settings ? settings->m_effectManager : nullptr;
        auto prepared = std::move(preparedResult.unwrap());
        auto paletteResult = installPalette(effects, result.plan, prepared.channels);
        if (paletteResult.isErr()) {
            fail(paletteResult.unwrapErr());
            return;
        }

        m_total = prepared.objects;
        m_created.reserve(m_total);
        m_prepared = std::move(prepared);
        m_source.reset();
        m_phase = Phase::Emitting;
        auto& collab = paimon::collab::CollabManager::get();
        if (collab.connected()) collab.sendLevelSettings(false);
    }

    void tick(float) {
        if (!paimon::modules::isEnabled("paimbnails.gifimport.editor") ||
            !paimon::modules::isEnabled("paimbnails.gifrender.editor")) {
            fail("La construccion en segundo plano fue desactivada.",
                 m_phase == Phase::Emitting);
            return;
        }
        if (m_phase == Phase::Analyzing) {
            std::optional<BuildResult> result;
            {
                std::lock_guard lock(m_progress->mutex);
                if (m_progress->result) result = std::move(m_progress->result);
            }
            if (result) {
                beginEmission(std::move(*result));
                return;
            }
            float const value = m_progress->value.load(std::memory_order_relaxed);
            auto const stage = static_cast<BuildStage>(
                m_progress->stage.load(std::memory_order_relaxed));
            int const pass = m_progress->pass.load(std::memory_order_relaxed);
            int const passes = m_progress->passes.load(std::memory_order_relaxed);
            m_fill->setScaleX(value * 0.7f);
            std::string text = passes > 1
                ? fmt::format("Render {}/{} | {} | {:.0f}%", pass, passes,
                              stageText(stage), value * 100.f)
                : fmt::format("{} | {:.0f}%", stageText(stage), value * 100.f);
            m_label->setString(text.c_str());
            return;
        }
        emitBatch();
    }

    void emitBatch() {
        if (!m_prepared) return;
        auto ui = m_ui.lock();
        auto* editor = LevelEditorLayer::get();
        if (!ui || !editor || ui->m_editorLayer != editor) {
            fail("El editor ya no esta disponible.", true);
            return;
        }

        constexpr std::size_t kBatchSize = 16;
        auto const& payload = m_prepared->payload;
        std::size_t end = m_cursor;
        std::size_t count = 0;
        while (count < kBatchSize && end < payload.size()) {
            auto const separator = payload.find(';', end);
            if (separator == std::string::npos) break;
            end = separator + 1;
            ++count;
        }
        if (count == 0) {
            fail("El dibujo incremental quedo incompleto.", true);
            return;
        }

        auto const batch = payload.substr(m_cursor, end - m_cursor);
        CCArray* created = editor->createObjectsFromString(batch, false, true);
        if (!created || created->count() != count) {
            removeCreated(editor, created);
            fail("GD no pudo crear un lote del dibujo.", true);
            return;
        }

        editor->updateObjectColors(created);
        for (auto* item : CCArrayExt<CCObject*>(created)) {
            if (auto* object = typeinfo_cast<GameObject*>(item)) {
                m_created.emplace_back(object);
            }
        }
        auto& collab = paimon::collab::CollabManager::get();
        if (collab.connected()) collab.sendCreatedObjects(created);

        m_cursor = end;
        m_createdCount += count;
        float const progress = m_total > 0
            ? static_cast<float>(m_createdCount) / m_total : 1.f;
        m_fill->setScaleX(0.7f + progress * 0.3f);
        m_label->setString(fmt::format(
            "Construyendo | {} / {} objetos", m_createdCount, m_total).c_str());
        if (m_cursor < payload.size()) return;

        editor->dirtifyTriggers();
        if (collab.connected()) collab.sendLevelSettings(false);
        PaimonNotify::show(
            fmt::format("Dibujo terminado: {} objetos", m_createdCount),
            NotificationIcon::Success);
        removeFromParent();
    }

    void fail(std::string const& message, bool rollback = false) {
        if (rollback) {
            auto ui = m_ui.lock();
            if (ui && ui->m_editorLayer) {
                auto* editor = ui->m_editorLayer;
                for (auto const& weak : m_created) {
                    if (auto object = weak.lock()) editor->removeObject(object.data(), true);
                }
                auto* settings = editor->m_levelSettings;
                auto* effects = settings ? settings->m_effectManager : nullptr;
                if (effects && m_prepared) removeColors(effects, m_prepared->channels);
                auto& collab = paimon::collab::CollabManager::get();
                if (collab.connected()) collab.sendLevelSettings(false);
            }
        }
        PaimonNotify::show(message, NotificationIcon::Error);
        removeFromParent();
    }

    Phase m_phase = Phase::Analyzing;
    WeakRef<EditorUI> m_ui;
    std::shared_ptr<SourceAnimation> m_source;
    Options m_options;
    CCPoint m_center;
    std::shared_ptr<AsyncProgress> m_progress;
    std::optional<PreparedImport> m_prepared;
    std::vector<WeakRef<GameObject>> m_created;
    std::size_t m_cursor = 0;
    std::size_t m_createdCount = 0;
    std::size_t m_total = 0;
    Ref<CCLabelBMFont> m_label;
    Ref<CCLayerColor> m_fill;
};

} // namespace

Result<EmitReport> emitToEditor(
    EditorUI* ui,
    ImportPlan const& plan,
    Options const& options,
    CCPoint origin
) {
    auto preparedResult = prepareImport(ui, plan, options, origin);
    if (preparedResult.isErr()) return Err(preparedResult.unwrapErr());
    auto prepared = std::move(preparedResult.unwrap());
    auto* editor = ui->m_editorLayer;
    auto* settings = editor->m_levelSettings;
    auto* effects = settings ? settings->m_effectManager : nullptr;
    auto paletteResult = installPalette(effects, plan, prepared.channels);
    if (paletteResult.isErr()) return Err(paletteResult.unwrapErr());

    CCArray* created = editor->createObjectsFromString(prepared.payload, false, true);
    if (!created || created->count() != plan.totalObjects) {
        removeCreated(editor, created);
        removeColors(effects, prepared.channels);
        return Err("GD no pudo crear todos los objetos; no se aplico la importacion.");
    }

    editor->updateObjectColors(created);
    editor->dirtifyTriggers();
    ui->deselectAll();
    ui->selectObjects(created, false);

    auto& collab = paimon::collab::CollabManager::get();
    if (collab.connected()) {
        collab.sendCreatedObjects(created);
        collab.sendLevelSettings(false);
    }

    return Ok(EmitReport{
        plan.totalObjects,
        static_cast<int>(prepared.channels.size()),
        static_cast<int>(prepared.groups.size())
    });
}

Result<> startBackgroundImport(
    EditorUI* ui,
    std::shared_ptr<SourceAnimation> source,
    Options const& options,
    CCPoint center
) {
    if (!paimon::modules::isEnabled("paimbnails.gifrender.editor")) {
        return Err("El submodulo Render esta desactivado.");
    }
    if (!ui || !ui->m_editorLayer || !source) {
        return Err("El editor o la imagen ya no estan disponibles.");
    }
    if (ui->getChildByID("gif-import-background-job"_spr)) {
        return Err("Ya hay un dibujo construyendose en segundo plano.");
    }

    auto* job = BackgroundImportJob::create(ui, std::move(source), options, center);
    if (!job) return Err("No se pudo iniciar la construccion en segundo plano.");
    ui->addChild(job, 9999);
    return Ok();
}

} // namespace paimon::gifimport
