#include "../../../utils/FilesystemPath.hpp"
#include "IconBuildService.hpp"

#include "IconApplier.hpp"
#include "IconThumbs.hpp"
#include "MoreIconsBridge.hpp"
#include "../engine/IconCompiler.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../persist/IconPaths.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include <exception>
#include <memory>
#include <utility>

using namespace geode::prelude;

namespace paimon::icon_maker {

namespace {

std::vector<std::weak_ptr<IconBuildService::DoneCallback>> s_pendingCompiles;

void runCompile(IconProject project, bool apply,
                IconBuildService::DoneCallback onDone) {
    auto& store = IconProjectStore::get();
    store.loadIndex();
    if (!store.exists(project.id)) {
        if (onDone) onDone(Err("El proyecto ya no existe."));
        return;
    }
    auto revision = store.revision(project.id);
    // the callback may own Cocos refs. Move it out on main before releasing its shared box.
    auto completion = std::make_shared<IconBuildService::DoneCallback>(std::move(onDone));
    std::erase_if(s_pendingCompiles, [](auto const& item) { return item.expired(); });
    s_pendingCompiles.emplace_back(completion);
    bool started = paimon::ThreadTracker::get().spawn(
        [project = std::move(project), apply, revision, completion]() mutable {
        auto result = [&]() -> decltype(IconCompiler::compile(project)) {
            try {
                return IconCompiler::compile(project);
            } catch (std::exception const& error) {
                return Err("No se pudo compilar: {}", error.what());
            }
        }();

        Loader::get()->queueInMainThread(
            [project = std::move(project), apply, revision, completion,
             result = std::move(result)]() mutable {
                auto onDone = std::exchange(*completion, {});
                if (paimon::isRuntimeShuttingDown()) return;
                if (!result) {
                    if (onDone) onDone(Err("{}", result.unwrapErr()));
                    return;
                }
                auto& store = IconProjectStore::get();
                if (!store.exists(project.id) || store.revision(project.id) != revision) {
                    if (onDone) onDone(Err("El proyecto cambio durante la compilacion. Vuelve a generarlo."));
                    return;
                }
                auto loaded = store.loadProject(project.id);
                if (!loaded) {
                    if (onDone) onDone(Err("{}", loaded.unwrapErr()));
                    return;
                }
                auto compiled = result.unwrap();
                auto outputDir = IconPaths::outputDir(project.id);
                std::error_code publishError;
                if (!compiled.staging) {
                    if (onDone) onDone(Err("El compilador no genero una salida valida."));
                    return;
                }
                if (!compiled.staging->publishTo(outputDir, publishError)) {
                    if (!compiled.staging->backupPath().empty()) {
                        log::warn("[icon-maker] previous output preserved at {} after publication failed",
                            paimon::paths::toUtf8(compiled.staging->backupPath()));
                    }
                    if (onDone) onDone(Err("No se pudo publicar el icono: {}", publishError.message()));
                    return;
                }
                auto current = loaded.unwrap();
                current.hasBuiltOnce = true;
                current.lastBuiltAt = nowUnixMs();
                if (auto saved = store.saveProject(current); !saved) {
                    // a project write may succeed before its index write fails.
                    // restore the previous build flags as well as the output directory.
                    if (auto restored = store.saveProject(loaded.unwrap()); !restored) {
                        log::warn("[icon-maker] build metadata rollback failed: {}", restored.unwrapErr());
                    }
                    if (!compiled.staging->rollback(publishError)) {
                        log::warn("[icon-maker] output rollback failed; previous output kept at {}: {}",
                            paimon::paths::toUtf8(compiled.staging->backupPath()), publishError.message());
                    }
                    if (onDone) onDone(Err("{}", saved.unwrapErr()));
                    return;
                }
                compiled.staging->commit();
                for (auto* quality : {&compiled.uhd, &compiled.hd, &compiled.sd}) {
                    quality->png = outputDir / quality->png.filename();
                    quality->plist = outputDir / quality->plist.filename();
                }
                IconThumbs::get().invalidate(project.id);

                if (!apply) {
                    if (onDone) onDone(Ok<std::string>("Archivos generados."));
                    return;
                }
                if (MoreIconsBridge::available()) {
                    if (auto r = MoreIconsBridge::registerIcon(current, compiled); !r) {
                        if (onDone) onDone(Err("More Icons: {}", r.unwrapErr()));
                        return;
                    }
                    if (MoreIconsBridge::applyIcon(current)) {
                        if (onDone) onDone(Ok<std::string>("Listo! Ya lo llevas puesto."));
                    } else {
                        if (onDone) onDone(Ok<std::string>("Guardado en More Icons."));
                    }
                    return;
                }
                IconApplier::get().invalidate(current.id);
                IconApplier::get().setActive(current.type, current.id);
                if (onDone) onDone(Ok<std::string>("Listo! Se vera al volver al garaje."));
            });
    });
    if (!started) {
        auto callback = std::exchange(*completion, {});
        if (callback) callback(Err("No se pudo iniciar la compilacion."));
    }
}

}  // anonymous namespace

void IconBuildService::shutdown() {
    for (auto const& pending : s_pendingCompiles) {
        if (auto callback = pending.lock()) *callback = nullptr;
    }
    s_pendingCompiles.clear();
}

void IconBuildService::buildAndApply(IconProject project, DoneCallback onDone) {
    runCompile(std::move(project), true, std::move(onDone));
}

void IconBuildService::build(IconProject project, DoneCallback onDone) {
    runCompile(std::move(project), false, std::move(onDone));
}

}  // namespace paimon::icon_maker
