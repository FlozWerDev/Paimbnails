#include "LevelImport.hpp"

#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <fmt/format.h>

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../auto-preview/services/LevelDataProvider.hpp"
#include "TemplateStore.hpp"

using namespace geode::prelude;

namespace paimon::autobuild {

namespace {

bool g_busy = false;
bool g_taxonomyLoaded = false;

constexpr size_t kMaxLevelBytes = 220u * 1024u * 1024u;

// A downloaded level arrives gzipped and base64'd; one already in the editor is
// plain text. The semicolon tells them apart without trying to unpack noise.
std::string unpackLevelString(gd::string const& raw) {
    if (raw.empty()) return {};
    std::string_view const view{raw.c_str(), raw.size()};
    if (view.find(';') != std::string_view::npos) return std::string(view);
    auto unpacked = cocos2d::ZipUtils::decompressString(raw, false, 0);
    return std::string(unpacked.c_str(), unpacked.size());
}

void runAnalysis(std::string text, int levelId, std::string name, AnalysisCallback callback) {
    if (text.empty()) {
        g_busy = false;
        callback(Err("El nivel no trae datos que analizar."));
        return;
    }
    if (text.size() > kMaxLevelBytes) {
        g_busy = false;
        callback(Err("El nivel es demasiado grande para analizarlo."));
        return;
    }

    loadTaxonomyFile();
    bool const started = ThreadTracker::get().spawn(
        [text = std::move(text), levelId, name = std::move(name), callback]() mutable {
            geode::utils::thread::setName("Paimon Autobuild Analysis");
            // A ban marks shutdown with the game still running; free the slot.
            if (paimon::isRuntimeShuttingDown()) { g_busy = false; return; }

            auto data = std::make_shared<LevelData>(parseLevelString(text));
            auto report = analyzeLevel(*data);
            report.levelId = levelId;
            report.name = std::move(name);

            queueInMainThread([data = std::move(data), report = std::move(report),
                               callback]() mutable {
                g_busy = false;
                if (data->objects.empty()) {
                    callback(Err("El nivel no tiene objetos que analizar."));
                    return;
                }
                log::info("[Autobuild] nivel {} analizado: {}", report.levelId,
                          report.summary());
                callback(Ok(AnalysisResult{std::move(data), std::move(report)}));
            });
        });
    if (!started) {
        g_busy = false;
        callback(Err("No se pudo iniciar el analisis."));
    }
}

} // namespace

void loadTaxonomyFile() {
    if (g_taxonomyLoaded) return;
    g_taxonomyLoaded = true;

    auto const path = TemplateStore::directory() / "objects.txt";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;
    auto text = utils::file::readString(path);
    if (text.isErr()) return;
    int const loaded = loadTaxonomyOverrides(text.unwrap());
    log::info("[Autobuild] {} reglas de objetos cargadas de objects.txt", loaded);
}

void analyzeLevelId(int levelId, AnalysisCallback callback) {
    if (g_busy) {
        callback(Err("Ya hay un analisis en marcha."));
        return;
    }
    if (levelId <= 0) {
        callback(Err("Escribe un id de nivel valido."));
        return;
    }

    g_busy = true;
    autopreview::LevelDataProvider::get().request(levelId, [levelId, callback](GJGameLevel* level) {
        if (!level || level->m_levelString.empty()) {
            g_busy = false;
            callback(Err(fmt::format("No se pudo descargar el nivel {}.", levelId)));
            return;
        }
        runAnalysis(unpackLevelString(level->m_levelString), levelId,
                    std::string(level->m_levelName), callback);
    });
}

void analyzeOpenLevel(AnalysisCallback callback) {
    if (g_busy) {
        callback(Err("Ya hay un analisis en marcha."));
        return;
    }
    auto* lel = LevelEditorLayer::get();
    if (!lel || !lel->m_level) {
        callback(Err("Abre un nivel en el editor primero."));
        return;
    }

    std::string text(lel->getLevelString());
    if (text.empty()) {
        callback(Err("El editor no devolvio ningun objeto."));
        return;
    }
    // getLevelString gives objects only, so the palette comes from the settings.
    if (auto* settings = lel->m_levelSettings) {
        text = std::string(settings->getSaveString()) + ";" + text;
    }

    g_busy = true;
    runAnalysis(std::move(text), lel->m_level->m_levelID.value(),
                std::string(lel->m_level->m_levelName), std::move(callback));
}

} // namespace paimon::autobuild
