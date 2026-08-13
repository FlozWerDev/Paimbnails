#include "GifImportPopup.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/GIFDecoder.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/stb_image.h"
#include "../services/GifArtVectorizer.hpp"
#include "../services/GifImportPipeline.hpp"
#include "../services/GifObjectEmitter.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/utils/general.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

using namespace geode::prelude;

namespace paimon::gifimport {

// Workers only keep these C++ mailboxes; tick applies results on the Cocos thread.
struct ProcessingProgress {
    std::atomic<float> value = 0.f;
    std::atomic<int> stage = static_cast<int>(BuildStage::Preparing);
    std::atomic<int> pass = 0;
    std::atomic<int> passes = 0;
    std::mutex mutex;
    std::optional<BuildResult> result;
};

struct LoadedSource {
    std::filesystem::path path;
    std::shared_ptr<SourceAnimation> source;
    std::string error;
};

struct SourceLoadState {
    std::mutex mutex;
    std::optional<LoadedSource> result;
};

namespace {

constexpr float kPopupWidth = 500.f;
constexpr float kPopupHeight = 320.f;
constexpr std::size_t kMaxFileBytes = 128 * 1024 * 1024;
constexpr std::size_t kDecodeMemory = 128 * 1024 * 1024;
constexpr int kMaxImageDimension = 8192;

bool renderEnabled() {
    return paimon::modules::isEnabled("paimbnails.gifrender.editor");
}

char const* buildStageText(BuildStage stage) {
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

CCLabelBMFont* valueLabel(CCNode* parent, CCPoint position) {
    auto* label = CCLabelBMFont::create("-", "goldFont.fnt");
    label->setScale(0.38f);
    label->setPosition(position);
    parent->addChild(label);
    return label;
}

} // namespace

GifImportPopup* GifImportPopup::create() {
    if (!paimon::modules::isEnabled("paimbnails.gifimport.editor")) return nullptr;
    auto* ret = new GifImportPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool GifImportPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;
    setID("gif-import-popup"_spr);
    setTitle("GIF o Imagen a Objetos");
    loadOptions();

    auto* previewPanel = paimon::SpriteHelper::createDarkPanel(214.f, 178.f, 220, 6.f);
    previewPanel->setPosition({18.f, 82.f});
    m_mainLayer->addChild(previewPanel);

    auto* previewHint = CCLabelBMFont::create("Elige un GIF o imagen", "bigFont.fnt");
    previewHint->setID("preview-hint");
    previewHint->setScale(0.34f);
    previewHint->setColor({125, 135, 160});
    previewHint->setPosition({125.f, 171.f});
    m_mainLayer->addChild(previewHint, 2);

    m_fileLabel = CCLabelBMFont::create("Ningun archivo seleccionado", "goldFont.fnt");
    m_fileLabel->setScale(0.4f);
    m_fileLabel->setPosition({125.f, 274.f});
    m_mainLayer->addChild(m_fileLabel);

    m_statsLabel = CCLabelBMFont::create("La vista previa usa objetos reales de GD", "bigFont.fnt");
    m_statsLabel->setScale(0.27f);
    m_statsLabel->setColor({165, 180, 210});
    m_statsLabel->setPosition({125.f, 69.f});
    m_mainLayer->addChild(m_statsLabel);

    m_progressTrack = CCLayerColor::create({0, 0, 0, 150}, 214.f, 4.f);
    m_progressTrack->ignoreAnchorPointForPosition(false);
    m_progressTrack->setAnchorPoint({0.f, 0.f});
    m_progressTrack->setPosition({18.f, 77.f});
    m_progressTrack->setVisible(false);
    m_mainLayer->addChild(m_progressTrack, 2);

    m_progressFill = CCLayerColor::create({90, 225, 150, 255}, 214.f, 4.f);
    m_progressFill->ignoreAnchorPointForPosition(false);
    m_progressFill->setAnchorPoint({0.f, 0.f});
    m_progressFill->setPosition({18.f, 77.f});
    m_progressFill->setScaleX(0.f);
    m_progressFill->setVisible(false);
    m_mainLayer->addChild(m_progressFill, 3);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);
    WeakRef<GifImportPopup> self = this;

    m_modeSprite = ButtonSprite::create(
        "Modo: Bloques", 112, true, "bigFont.fnt", "GJ_button_05.png", 26.f, 0.5f);
    m_modeSprite->setScale(0.62f);
    auto* modeButton = CCMenuItemExt::createSpriteExtra(
        m_modeSprite, [self](CCMenuItemSpriteExtra*) {
            if (auto* popup = self.lock().data()) popup->toggleMode();
        });
    modeButton->setPosition({375.f, 278.f});
    menu->addChild(modeButton);

    using Adjust = std::function<void(GifImportPopup*, int)>;
    auto addStepper = [&](char const* title, float y, CCLabelBMFont** output, Adjust adjust) {
        auto* label = CCLabelBMFont::create(title, "bigFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->setScale(0.32f);
        label->setPosition({252.f, y});
        m_mainLayer->addChild(label);

        *output = valueLabel(m_mainLayer, {421.f, y});
        for (int direction : {-1, 1}) {
            auto* sprite = ButtonSprite::create(
                direction < 0 ? "-" : "+", "bigFont.fnt", "GJ_button_04.png", 0.8f);
            sprite->setScale(0.43f);
            auto* button = CCMenuItemExt::createSpriteExtra(
                sprite, [self, adjust, direction](CCMenuItemSpriteExtra*) {
                    if (auto* popup = self.lock().data()) adjust(popup, direction);
                });
            button->setPosition({direction < 0 ? 382.f : 462.f, y});
            menu->addChild(button);
        }
    };

    addStepper("Resolucion", 248.f, &m_resolutionValue,
               [](GifImportPopup* popup, int d) { popup->adjustResolution(d); });
    addStepper("Colores", 222.f, &m_colorsValue,
               [](GifImportPopup* popup, int d) { popup->adjustColors(d); });
    addStepper("Presupuesto", 196.f, &m_budgetValue,
               [](GifImportPopup* popup, int d) { popup->adjustBudget(d); });
    addStepper("Frames max.", 170.f, &m_framesValue,
               [](GifImportPopup* popup, int d) { popup->adjustFrames(d); });
    addStepper("Tamano pixel", 144.f, &m_pixelSizeValue,
               [](GifImportPopup* popup, int d) { popup->adjustPixelSize(d); });
    addStepper("Fondo", 118.f, &m_backgroundValue,
               [](GifImportPopup* popup, int) { popup->toggleBackground(); });
    addStepper("Tolerancia", 92.f, &m_toleranceValue,
               [](GifImportPopup* popup, int d) { popup->adjustTolerance(d); });

    m_samplingSprite = ButtonSprite::create("Suave", 74, true, "bigFont.fnt", "GJ_button_04.png", 24.f, 0.5f);
    m_samplingSprite->setScale(0.55f);
    auto* samplingButton = CCMenuItemExt::createSpriteExtra(
        m_samplingSprite, [self](CCMenuItemSpriteExtra*) {
            if (auto* popup = self.lock().data()) popup->toggleSampling();
        });
    samplingButton->setPosition({273.f, 62.f});
    menu->addChild(samplingButton);

    m_ditherSprite = ButtonSprite::create("Dither: no", 80, true, "bigFont.fnt", "GJ_button_04.png", 24.f, 0.5f);
    m_ditherSprite->setScale(0.55f);
    auto* ditherButton = CCMenuItemExt::createSpriteExtra(
        m_ditherSprite, [self](CCMenuItemSpriteExtra*) {
            if (auto* popup = self.lock().data()) popup->toggleDither();
        });
    ditherButton->setPosition({360.f, 62.f});
    menu->addChild(ditherButton);

    m_loopSprite = ButtonSprite::create("Loop: si", 74, true, "bigFont.fnt", "GJ_button_04.png", 24.f, 0.5f);
    m_loopSprite->setScale(0.55f);
    auto* loopButton = CCMenuItemExt::createSpriteExtra(
        m_loopSprite, [self](CCMenuItemSpriteExtra*) {
            if (auto* popup = self.lock().data()) popup->toggleLoop();
        });
    loopButton->setPosition({449.f, 62.f});
    menu->addChild(loopButton);

    bool const hasRender = renderEnabled();
    auto* pickSprite = ButtonSprite::create("Elegir archivo", "goldFont.fnt", "GJ_button_01.png", 0.65f);
    auto* pickButton = CCMenuItemExt::createSpriteExtra(
        pickSprite, [self](CCMenuItemSpriteExtra*) {
            if (auto* popup = self.lock().data()) popup->pickSource();
        });
    pickButton->setPosition({hasRender ? 79.f : 125.f, 31.f});
    menu->addChild(pickButton);

    auto* importSprite = ButtonSprite::create(
        hasRender ? "Importar" : "Importar objetos",
        "goldFont.fnt", "GJ_button_02.png", 0.65f);
    auto* importButton = CCMenuItemExt::createSpriteExtra(
        importSprite, [self](CCMenuItemSpriteExtra*) {
            if (auto* popup = self.lock().data()) popup->importObjects();
        });
    importButton->setPosition({hasRender ? 248.f : 375.f, 31.f});
    menu->addChild(importButton);

    if (hasRender) {
        auto* backgroundSprite = ButtonSprite::create(
            "Run background", 112, true, "goldFont.fnt", "GJ_button_04.png", 30.f, 0.55f);
        backgroundSprite->setScale(0.88f);
        auto* backgroundButton = CCMenuItemExt::createSpriteExtra(
            backgroundSprite, [self](CCMenuItemSpriteExtra*) {
                if (auto* popup = self.lock().data()) popup->runBackground();
            });
        backgroundButton->setPosition({414.f, 31.f});
        menu->addChild(backgroundButton);
    }

    refreshControls();
    schedule(schedule_selector(GifImportPopup::tick));
    return true;
}

void GifImportPopup::loadOptions() {
    auto* mod = Mod::get();
    m_options.maxDimension = static_cast<int>(mod->getSavedValue<int64_t>("gif-import-resolution", 48));
    m_options.maxColors = static_cast<int>(mod->getSavedValue<int64_t>("gif-import-colors", 16));
    m_options.objectBudget = static_cast<int>(mod->getSavedValue<int64_t>("gif-import-budget", 12000));
    m_options.maxFrames = static_cast<int>(mod->getSavedValue<int64_t>("gif-import-frames", 60));
    m_options.pixelSize = static_cast<float>(mod->getSavedValue<double>("gif-import-pixel-size", 6.0));
    m_options.background = mod->getSavedValue<bool>("gif-import-remove-bg", true)
        ? BackgroundMode::AutoBorder : BackgroundMode::Keep;
    m_options.backgroundTolerance = static_cast<int>(
        mod->getSavedValue<int64_t>("gif-import-bg-tolerance", 28));
    m_options.sampling = mod->getSavedValue<bool>("gif-import-smooth", true)
        ? SamplingMode::Smooth : SamplingMode::Pixel;
    int const savedMode = static_cast<int>(mod->getSavedValue<int64_t>(
        "gif-import-mode", mod->getSavedValue<bool>("gif-import-art-mode", false) ? 1 : 0));
    m_options.mode = savedMode == 3
        ? (renderEnabled() ? ImportMode::Render : ImportMode::Paint)
        : savedMode == 2 ? ImportMode::Paint
        : savedMode == 1 ? ImportMode::Art : ImportMode::Blocks;
    m_options.dither = mod->getSavedValue<bool>("gif-import-dither", false);
    m_options.loop = mod->getSavedValue<bool>("gif-import-loop", true);
}

void GifImportPopup::saveOptions() const {
    auto* mod = Mod::get();
    mod->setSavedValue<int64_t>("gif-import-resolution", m_options.maxDimension);
    mod->setSavedValue<int64_t>("gif-import-colors", m_options.maxColors);
    mod->setSavedValue<int64_t>("gif-import-budget", m_options.objectBudget);
    mod->setSavedValue<int64_t>("gif-import-frames", m_options.maxFrames);
    mod->setSavedValue<double>("gif-import-pixel-size", m_options.pixelSize);
    mod->setSavedValue<bool>("gif-import-remove-bg", m_options.background == BackgroundMode::AutoBorder);
    mod->setSavedValue<int64_t>("gif-import-bg-tolerance", m_options.backgroundTolerance);
    mod->setSavedValue<bool>("gif-import-smooth", m_options.sampling == SamplingMode::Smooth);
    mod->setSavedValue<int64_t>("gif-import-mode", static_cast<int64_t>(m_options.mode));
    mod->setSavedValue<bool>("gif-import-dither", m_options.dither);
    mod->setSavedValue<bool>("gif-import-loop", m_options.loop);
}

void GifImportPopup::pickSource() {
    if (m_busyOverlay) return;
    WeakRef<GifImportPopup> self = this;
    pt::pickImage([self](Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup || !popup->getParent()) return;
        if (result.isErr()) {
            PaimonNotify::show("No se pudo abrir el selector de archivos.", NotificationIcon::Error);
            return;
        }
        auto path = result.unwrap();
        if (path) popup->loadSource(*path);
    });
}

void GifImportPopup::loadSource(std::filesystem::path const& path) {
    auto bytesResult = utils::file::readBinary(path);
    if (bytesResult.isErr()) {
        PaimonNotify::show("No se pudo leer el archivo.", NotificationIcon::Error);
        return;
    }
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(bytesResult.unwrap());
    if (bytes->empty() || bytes->size() > kMaxFileBytes) {
        PaimonNotify::show("El archivo esta vacio o es demasiado grande.", NotificationIcon::Warning);
        return;
    }
    if (GIFDecoder::isGIF(bytes->data(), bytes->size())) {
        loadAnimated(path, std::move(bytes));
        return;
    }
    loadStill(path, std::move(bytes));
}

void GifImportPopup::loadAnimated(
    std::filesystem::path const& path,
    std::shared_ptr<std::vector<std::uint8_t>> bytes
) {
    int width = 0;
    int height = 0;
    if (!GIFDecoder::getDimensions(bytes->data(), bytes->size(), width, height)) {
        PaimonNotify::show("No se pudieron leer las dimensiones del GIF.", NotificationIcon::Error);
        return;
    }
    std::size_t const frameBytes = static_cast<std::size_t>(width) * height * 4;
    int const safeFrames = static_cast<int>(std::clamp<std::size_t>(
        kDecodeMemory / std::max<std::size_t>(frameBytes, 1), 1, 120));

    showBusy("Decodificando GIF");
    m_sourceLoad = std::make_shared<SourceLoadState>();
    auto state = m_sourceLoad;
    std::thread([state, bytes, path, safeFrames] {
        auto gif = GIFDecoder::decode(bytes->data(), bytes->size(), safeFrames);
        auto source = std::make_shared<SourceAnimation>();
        source->width = gif.width;
        source->height = gif.height;
        source->frames.reserve(gif.frames.size());
        for (auto& frame : gif.frames) {
            source->frames.push_back({frame.delayMs, std::move(frame.pixels)});
        }
        LoadedSource loaded{path, source, {}};
        if (source->frames.empty()) loaded.error = "No se pudo decodificar ningun frame.";
        std::lock_guard lock(state->mutex);
        state->result = std::move(loaded);
    }).detach();
}

void GifImportPopup::loadStill(
    std::filesystem::path const& path,
    std::shared_ptr<std::vector<std::uint8_t>> bytes
) {
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(
            bytes->data(), static_cast<int>(bytes->size()), &width, &height, &channels) ||
        width <= 0 || height <= 0) {
        PaimonNotify::show("El archivo no es una imagen valida.", NotificationIcon::Warning);
        return;
    }
    if (width > kMaxImageDimension || height > kMaxImageDimension) {
        PaimonNotify::show(
            fmt::format("La imagen es demasiado grande (max {} px).", kMaxImageDimension),
            NotificationIcon::Warning);
        return;
    }

    showBusy("Decodificando imagen");
    m_sourceLoad = std::make_shared<SourceLoadState>();
    auto state = m_sourceLoad;
    std::thread([state, bytes, path] {
        int decodedWidth = 0;
        int decodedHeight = 0;
        int decodedChannels = 0;
        auto* pixels = stbi_load_from_memory(
            bytes->data(), static_cast<int>(bytes->size()),
            &decodedWidth, &decodedHeight, &decodedChannels, 4);
        auto source = std::make_shared<SourceAnimation>();
        if (pixels) {
            source->width = decodedWidth;
            source->height = decodedHeight;
            source->frames.push_back({
                0,
                std::vector<std::uint8_t>(
                    pixels,
                    pixels + static_cast<std::size_t>(decodedWidth) * decodedHeight * 4)
            });
            stbi_image_free(pixels);
        }
        LoadedSource loaded{path, source, {}};
        if (source->frames.empty()) loaded.error = "No se pudo decodificar la imagen.";
        std::lock_guard lock(state->mutex);
        state->result = std::move(loaded);
    }).detach();
}

void GifImportPopup::applySource(
    std::filesystem::path const& path,
    std::shared_ptr<SourceAnimation> source
) {
    m_path = path;
    m_source = std::move(source);
    m_plan.reset();
    m_previewFrame = 0;
    m_previewElapsed = 0.f;
    auto name = utils::string::pathToString(path.filename());
    if (name.size() > 32) name = name.substr(0, 30) + "..";
    m_fileLabel->setString(name.c_str());
    requestProcess();
}

void GifImportPopup::requestProcess() {
    if (m_options.mode == ImportMode::Render && !renderEnabled()) {
        m_options.mode = ImportMode::Paint;
    }
    saveOptions();
    refreshControls();
    if (!m_source) return;
    if (m_processing) {
        m_reprocess = true;
        return;
    }
    startProcess();
}

void GifImportPopup::startProcess() {
    if (!m_source) return;
    m_processing = true;
    m_reprocess = false;
    m_progress = std::make_shared<ProcessingProgress>();
    m_progressTrack->setVisible(true);
    m_progressFill->setScaleX(0.f);
    m_progressFill->setVisible(true);
    m_statsLabel->setColor({255, 205, 105});
    m_statsLabel->setString("Procesando y optimizando...");

    auto source = m_source;
    Options const options = m_options;
    auto progress = m_progress;
    std::thread([source, options, progress] {
        auto result = buildPlan(*source, options, [progress](BuildProgress const& update) {
            progress->value.store(update.value, std::memory_order_relaxed);
            progress->stage.store(static_cast<int>(update.stage), std::memory_order_relaxed);
            progress->pass.store(update.pass, std::memory_order_relaxed);
            progress->passes.store(update.passes, std::memory_order_relaxed);
        });
        std::lock_guard lock(progress->mutex);
        progress->result = std::move(result);
    }).detach();
}

void GifImportPopup::applyProcessed(BuildResult result) {
    m_processing = false;
    if (m_reprocess) {
        startProcess();
        return;
    }
    m_progress.reset();
    m_progressTrack->setVisible(false);
    m_progressFill->setVisible(false);
    if (!result) {
        m_plan.reset();
        m_statsLabel->setColor({255, 120, 120});
        m_statsLabel->setString(result.error.c_str());
        return;
    }
    m_plan = std::make_shared<ImportPlan>(std::move(result.plan));
    m_previewFrame = 0;
    m_previewElapsed = 0.f;
    refreshPreview();
    refreshControls();
}

void GifImportPopup::refreshControls() {
    m_resolutionValue->setString(fmt::format("{} px", m_options.maxDimension).c_str());
    m_colorsValue->setString(std::to_string(m_options.maxColors).c_str());
    m_budgetValue->setString(fmt::format("{}k", m_options.objectBudget / 1000.f).c_str());
    m_framesValue->setString(std::to_string(m_options.maxFrames).c_str());
    m_pixelSizeValue->setString(fmt::format("{:.0f} u", m_options.pixelSize).c_str());
    m_backgroundValue->setString(
        m_options.background == BackgroundMode::AutoBorder ? "Auto" : "Conservar");
    m_toleranceValue->setString(std::to_string(m_options.backgroundTolerance).c_str());
    bool const vector = m_options.mode != ImportMode::Blocks;
    m_modeSprite->setString(
        m_options.mode == ImportMode::Render ? "Modo: Render"
        : m_options.mode == ImportMode::Paint ? "Modo: Pintura"
        : m_options.mode == ImportMode::Art ? "Modo: Art"
        : "Modo: Bloques");
    m_samplingSprite->setString(vector
        ? "Suave: fijo"
        : (m_options.sampling == SamplingMode::Smooth ? "Suave" : "Pixel"));
    m_ditherSprite->setString(vector
        ? "Dither: no"
        : (m_options.dither ? "Dither: si" : "Dither: no"));
    m_loopSprite->setString(m_options.loop ? "Loop: si" : "Loop: no");

    if (!m_plan || m_processing) return;
    m_statsLabel->setColor({135, 230, 170});
    std::string review;
    if (m_plan->mode == ImportMode::Render) {
        review = fmt::format(
            "\nfid {:.1f}% | detalle {:.1f}% | {} pasadas",
            m_plan->similarity, m_plan->detailSimilarity, m_plan->renderPasses);
    } else if (usesPaintGeometry(m_plan->mode)) {
        review = fmt::format(" | fidelidad {:.1f}%", m_plan->similarity);
    }
    m_statsLabel->setString(fmt::format(
        "{}x{} | {} frames | {} colores | {}{}\n"
        "{} formas ({} blq, {} traz, {} circ, {} tri) + {} triggers = {}{}",
        m_plan->width, m_plan->height, m_plan->frames.size(), m_plan->palette.size(),
        m_plan->strategy, review,
        m_plan->visualObjects, m_plan->blockObjects, m_plan->strokeObjects,
        m_plan->circleObjects, m_plan->triangleObjects,
        m_plan->triggerObjects, m_plan->totalObjects,
        m_plan->actualDimension < m_plan->requestedDimension ? " (ajustado)" : ""
    ).c_str());
}

void GifImportPopup::pollSourceLoad() {
    if (!m_sourceLoad) return;
    std::optional<LoadedSource> loaded;
    {
        std::lock_guard lock(m_sourceLoad->mutex);
        if (!m_sourceLoad->result) return;
        loaded = std::move(m_sourceLoad->result);
    }
    m_sourceLoad.reset();
    hideBusy();
    if (!loaded->error.empty()) {
        PaimonNotify::show(loaded->error, NotificationIcon::Error);
        return;
    }
    applySource(loaded->path, std::move(loaded->source));
}

void GifImportPopup::pollProcessing() {
    if (!m_progress) return;
    std::optional<BuildResult> result;
    {
        std::lock_guard lock(m_progress->mutex);
        if (!m_progress->result) return;
        result = std::move(m_progress->result);
    }
    applyProcessed(std::move(*result));
}

void GifImportPopup::refreshProgress() {
    if (!m_progress) return;
    float const value = std::clamp(
        m_progress->value.load(std::memory_order_relaxed), 0.f, 1.f);
    auto const stage = static_cast<BuildStage>(
        m_progress->stage.load(std::memory_order_relaxed));
    int const pass = m_progress->pass.load(std::memory_order_relaxed);
    int const passes = m_progress->passes.load(std::memory_order_relaxed);
    m_progressFill->setScaleX(value);
    auto const text = passes > 1
        ? fmt::format("Render {}/{} | {} | {:.0f}%", pass, passes,
                      buildStageText(stage), value * 100.f)
        : fmt::format("{} | {:.0f}%", buildStageText(stage), value * 100.f);
    m_statsLabel->setString(text.c_str());
}

void GifImportPopup::refreshPreview() {
    if (!m_plan || m_plan->frames.empty()) return;
    m_previewFrame = std::clamp(m_previewFrame, 0, static_cast<int>(m_plan->frames.size()) - 1);
    int const previewScale = m_plan->mode == ImportMode::Blocks ? 1 : 4;
    int const previewWidth = m_plan->width * previewScale;
    int const previewHeight = m_plan->height * previewScale;
    auto pixels = renderPlanFrame(*m_plan, m_previewFrame, previewScale);

    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }
    auto* texture = new CCTexture2D();
    if (texture->initWithData(
        pixels.data(), kCCTexture2DPixelFormat_RGBA8888,
        previewWidth, previewHeight,
        {static_cast<float>(previewWidth), static_cast<float>(previewHeight)}
    )) {
        if (m_plan->mode == ImportMode::Blocks) texture->setAliasTexParameters();
        m_previewSprite = CCSprite::createWithTexture(texture);
        float const scale = std::min(198.f / previewWidth, 162.f / previewHeight);
        m_previewSprite->setScale(scale);
        m_previewSprite->setPosition({125.f, 171.f});
        m_mainLayer->addChild(m_previewSprite, 3);
        if (auto* hint = m_mainLayer->getChildByID("preview-hint")) hint->setVisible(false);
    }
    texture->release();
}

void GifImportPopup::tick(float dt) {
    pollSourceLoad();
    if (m_processing) {
        refreshProgress();
        pollProcessing();
        return;
    }
    if (!m_plan || m_plan->frames.size() < 2) return;
    m_previewElapsed += dt * 1000.f;
    int guard = 0;
    while (guard++ < static_cast<int>(m_plan->frames.size())) {
        int const delay = std::max(m_plan->frames[static_cast<std::size_t>(m_previewFrame)].delayMs, 10);
        if (m_previewElapsed < delay) break;
        m_previewElapsed -= delay;
        ++m_previewFrame;
        if (m_previewFrame >= static_cast<int>(m_plan->frames.size())) {
            if (!m_options.loop) {
                m_previewFrame = static_cast<int>(m_plan->frames.size()) - 1;
                m_previewElapsed = 0.f;
                break;
            }
            m_previewFrame = 0;
        }
        refreshPreview();
    }
}

void GifImportPopup::runBackground() {
    if (!m_source) {
        PaimonNotify::show("Primero elige un GIF o una imagen.", NotificationIcon::Warning);
        return;
    }
    auto* editor = LevelEditorLayer::get();
    auto* ui = editor ? editor->m_editorUI : nullptr;
    if (!ui || !editor->m_objectLayer) {
        PaimonNotify::show("El editor ya no esta disponible.", NotificationIcon::Error);
        return;
    }

    saveOptions();
    auto const winSize = CCDirector::get()->getWinSize();
    CCPoint const workspaceCenter{winSize.width * 0.5f, winSize.height * 0.4f};
    auto const center = editor->m_objectLayer->convertToNodeSpace(workspaceCenter);
    auto result = startBackgroundImport(ui, m_source, m_options, center);
    if (result.isErr()) {
        PaimonNotify::show(result.unwrapErr(), NotificationIcon::Error);
        return;
    }
    PaimonNotify::show(
        "Dibujo ejecutandose en segundo plano.", NotificationIcon::Info);
    onClose(nullptr);
}

void GifImportPopup::importObjects() {
    if (m_processing) {
        PaimonNotify::show("Espera a que termine la optimizacion.", NotificationIcon::Info);
        return;
    }
    if (!m_plan) {
        PaimonNotify::show("Primero elige y procesa un archivo.", NotificationIcon::Warning);
        return;
    }
    auto* editor = LevelEditorLayer::get();
    auto* ui = editor ? editor->m_editorUI : nullptr;
    if (!ui || !editor->m_objectLayer) {
        PaimonNotify::show("El editor ya no esta disponible.", NotificationIcon::Error);
        return;
    }

    auto const winSize = CCDirector::get()->getWinSize();
    CCPoint const workspaceCenter{winSize.width * 0.5f, winSize.height * 0.4f};
    auto const center = editor->m_objectLayer->convertToNodeSpace(workspaceCenter);
    float const margin = m_options.pixelSize;
    CCPoint const origin{
        center.x - (m_plan->width * m_options.pixelSize + margin) * 0.5f,
        center.y - (m_plan->height * m_options.pixelSize + margin) * 0.5f
    };
    showBusy("Creando objetos");
    WeakRef<GifImportPopup> self = this;
    WeakRef<EditorUI> targetUI = ui;
    Loader::get()->queueInMainThread([self, targetUI, origin] {
        auto popupRef = self.lock();
        auto targetRef = targetUI.lock();
        auto* popup = popupRef.data();
        auto* target = targetRef.data();
        auto* currentEditor = LevelEditorLayer::get();
        if (!popup) return;
        if (!target || !currentEditor || currentEditor->m_editorUI != target) {
            popup->hideBusy();
            PaimonNotify::show("El editor ya no esta disponible.", NotificationIcon::Error);
            return;
        }
        if (!popup->m_plan) {
            popup->hideBusy();
            return;
        }
        auto result = emitToEditor(target, *popup->m_plan, popup->m_options, origin);
        popup->hideBusy();
        if (result.isErr()) {
            PaimonNotify::show(result.unwrapErr(), NotificationIcon::Error);
            return;
        }
        auto const report = result.unwrap();
        PaimonNotify::show(
            fmt::format("Importado: {} objetos, {} colores, {} grupos",
                        report.objects, report.colors, report.groups),
            NotificationIcon::Success
        );
        popup->onClose(nullptr);
    });
}

void GifImportPopup::adjustResolution(int direction) {
    int const from = direction < 0 ? m_options.maxDimension - 1 : m_options.maxDimension;
    int const step = from >= 160 ? 16 : from >= 64 ? 8 : 4;
    m_options.maxDimension = std::clamp(m_options.maxDimension + direction * step, 4, 320);
    requestProcess();
}

void GifImportPopup::adjustColors(int direction) {
    m_options.maxColors = std::clamp(m_options.maxColors + direction * 2, 1, 64);
    requestProcess();
}

void GifImportPopup::adjustBudget(int direction) {
    m_options.objectBudget = std::clamp(m_options.objectBudget + direction * 1000, 1000, 50000);
    requestProcess();
}

void GifImportPopup::adjustFrames(int direction) {
    m_options.maxFrames = std::clamp(m_options.maxFrames + direction * 5, 1, 120);
    requestProcess();
}

void GifImportPopup::adjustPixelSize(int direction) {
    m_options.pixelSize = std::clamp(m_options.pixelSize + direction, 1.f, 30.f);
    requestProcess();
}

void GifImportPopup::adjustTolerance(int direction) {
    m_options.backgroundTolerance = std::clamp(
        m_options.backgroundTolerance + direction * 4, 0, 120);
    requestProcess();
}

void GifImportPopup::toggleMode() {
    m_options.mode = m_options.mode == ImportMode::Blocks ? ImportMode::Art
        : m_options.mode == ImportMode::Art ? ImportMode::Paint
        : m_options.mode == ImportMode::Paint && renderEnabled() ? ImportMode::Render
        : ImportMode::Blocks;
    requestProcess();
}

void GifImportPopup::toggleBackground() {
    m_options.background = m_options.background == BackgroundMode::AutoBorder
        ? BackgroundMode::Keep : BackgroundMode::AutoBorder;
    requestProcess();
}

void GifImportPopup::toggleSampling() {
    if (m_options.mode != ImportMode::Blocks) return;
    m_options.sampling = m_options.sampling == SamplingMode::Smooth
        ? SamplingMode::Pixel : SamplingMode::Smooth;
    requestProcess();
}

void GifImportPopup::toggleDither() {
    if (m_options.mode != ImportMode::Blocks) return;
    m_options.dither = !m_options.dither;
    requestProcess();
}

void GifImportPopup::toggleLoop() {
    m_options.loop = !m_options.loop;
    requestProcess();
}

void GifImportPopup::showBusy(std::string const& text) {
    if (m_busyOverlay) return;
    m_busyOverlay = PaimonLoadingOverlay::create(text, 30.f);
    if (m_busyOverlay) m_busyOverlay->showLocal(m_mainLayer, 300);
}

void GifImportPopup::hideBusy() {
    if (!m_busyOverlay) return;
    m_busyOverlay->dismiss();
    m_busyOverlay = nullptr;
}

} // namespace paimon::gifimport
