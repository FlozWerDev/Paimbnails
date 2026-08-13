#include "GifObjectEmitter.hpp"
#include "GifImportPipeline.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../collab-editor/CollabManager.hpp"

#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

using ShapeTable = std::array<ObjectShape, 5>;

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
    return shapes;
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
    ShapeTable const& shapes,
    std::vector<int> const& colors,
    float pixelSize,
    CCPoint origin,
    int imageHeight,
    int group,
    bool layered,
    int zLayer
) {
    auto const& shape = shapes[shapeIndex(object.kind)];
    float const x = origin.x + (object.x + 0.5f) * pixelSize;
    float const y = origin.y + (imageHeight - object.y - 0.5f) * pixelSize;
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
int sharedZLayer(ShapeTable const& shapes) {
    int const block = shapes[shapeIndex(PrimitiveKind::Block)].zLayer;
    for (auto kind : {PrimitiveKind::Stroke, PrimitiveKind::Circle,
                      PrimitiveKind::Triangle, PrimitiveKind::WideTriangle}) {
        if (shapes[shapeIndex(kind)].zLayer == block) continue;
        return block != 0 ? block : static_cast<int>(ZLayer::B1);
    }
    return 0;
}

bool resolveShapes(LevelEditorLayer* editor, ImportMode mode, ShapeTable& shapes) {
    if (!measureShape(editor, shapes[shapeIndex(PrimitiveKind::Block)])) return false;
    shapes[shapeIndex(PrimitiveKind::Stroke)] = shapes[shapeIndex(PrimitiveKind::Block)];
    if (mode == ImportMode::Blocks) return true;

    for (auto kind : {PrimitiveKind::Circle, PrimitiveKind::Triangle,
                      PrimitiveKind::WideTriangle}) {
        auto& shape = shapes[shapeIndex(kind)];
        if (measureShape(editor, shape)) continue;
        return false;
    }
    return true;
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
        stateGroups.assign(
            groups.begin(),
            groups.begin() + static_cast<std::ptrdiff_t>(plan.tracks.size()));
        eventGroups.assign(
            groups.begin() + static_cast<std::ptrdiff_t>(plan.tracks.size()),
            groups.end());
    }

    bool const layered = usesPaintGeometry(plan.mode);
    int const zLayer = layered ? sharedZLayer(shapes) : 0;
    std::string payload;
    payload.reserve(plan.totalObjects * 112);
    for (auto const& object : plan.staticObjects) {
        appendPrimitive(
            payload, object, shapes, channels, options.pixelSize, origin, plan.height, 0,
            layered, zLayer);
    }
    for (std::size_t i = 0; i < plan.tracks.size(); ++i) {
        for (auto const& object : plan.tracks[i].objects) {
            appendPrimitive(
                payload, object, shapes, channels, options.pixelSize, origin, plan.height,
                stateGroups[i], layered, zLayer);
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
            std::max(plan.frames.front().delayMs, 10) / 1000.f, 0);
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
                    bitAt(plan.tracks[track], static_cast<int>(frame)), eventGroup);
            }

            bool const hasNext = frame + 1 < plan.frames.size();
            if (hasNext || options.loop) {
                std::size_t const next = hasNext ? frame + 1 : 0;
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
        schedule(schedule_selector(BackgroundImportJob::tick));
        startBuild();
        return true;
    }

    void startBuild() {
        auto source = m_source;
        auto const options = m_options;
        auto progress = m_progress;
        std::thread([source, options, progress] {
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
        }).detach();
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
