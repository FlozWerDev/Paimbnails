#include "IconBuildService.hpp"

#include "IconApplier.hpp"
#include "IconThumbs.hpp"
#include "MoreIconsBridge.hpp"
#include "../engine/IconCompiler.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"

using namespace geode::prelude;

namespace paimon::icon_maker {

namespace {

void runCompile(IconProject project, bool apply,
                IconBuildService::DoneCallback onDone) {
    paimon::ThreadTracker::get().spawn([project, apply, onDone]() mutable {
        auto result = IconCompiler::compile(project);

        Loader::get()->queueInMainThread(
            [project = std::move(project), apply, onDone,
             result = std::move(result)]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                if (!result) {
                    if (onDone) onDone(Err("{}", result.unwrapErr()));
                    return;
                }
                auto compiled = result.unwrap();

                project.hasBuiltOnce = true;
                project.lastBuiltAt = nowUnixMs();
                project.modifiedAt = nowUnixMs();
                if (auto r = IconProjectStore::get().saveProject(project); !r) {
                    log::warn("[icon-maker] build: no se pudo guardar: {}", r.unwrapErr());
                }
                IconThumbs::get().invalidate(project.id);

                if (!apply) {
                    if (onDone) onDone(Ok<std::string>("Archivos generados."));
                    return;
                }

                if (MoreIconsBridge::available()) {
                    if (auto r = MoreIconsBridge::registerIcon(project, compiled); !r) {
                        if (onDone) onDone(Err("More Icons: {}", r.unwrapErr()));
                        return;
                    }
                    if (MoreIconsBridge::applyIcon(project)) {
                        if (onDone) onDone(Ok<std::string>("Listo! Ya lo llevas puesto."));
                    } else {
                        if (onDone) onDone(Ok<std::string>("Guardado en More Icons."));
                    }
                    return;
                }

                IconApplier::get().invalidate(project.id);
                IconApplier::get().setActive(project.type, project.id);
                if (onDone) onDone(Ok<std::string>("Listo! Se vera al volver al garaje."));
            });
    });
}

}  // anonymous namespace

void IconBuildService::buildAndApply(IconProject project, DoneCallback onDone) {
    runCompile(std::move(project), true, std::move(onDone));
}

void IconBuildService::build(IconProject project, DoneCallback onDone) {
    runCompile(std::move(project), false, std::move(onDone));
}

}  // namespace paimon::icon_maker
