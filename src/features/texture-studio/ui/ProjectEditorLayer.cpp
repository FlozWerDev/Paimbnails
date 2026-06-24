#include "ProjectEditorLayer.hpp"

#include "../engine/ColorPresets.hpp"
#include "../engine/PackExporter.hpp"
#include "../engine/SelfTest.hpp"
#include "../engine/SpritePreviewRenderer.hpp"
#include "../engine/AutoTuner.hpp"
#include "../data/SpritesheetReader.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/SlotStore.hpp"
#include "../services/FramePixelCache.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

ProjectEditorLayer* ProjectEditorLayer::create(std::string slotId) {
    auto* ret = new ProjectEditorLayer();
    if (ret->init(std::move(slotId))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ProjectEditorLayer::init(std::string slotId) {
    // Tamaño más compacto: el popup se renderiza sobre el TextureStudio
    // (540x360) y antes (480x280) ocupaba prácticamente todo, lo que daba
    // sensación de "pantalla completa" y no se veía el asset. Con 380x230
    // queda como overlay claro y deja espacio para un preview a la izquierda.
    constexpr float kW = 380.f;
    constexpr float kH = 230.f;
    if (!Popup::init(kW, kH)) return false;
    m_slotId = std::move(slotId);

    auto loaded = SlotStore::get().loadSlot(m_slotId);
    if (!loaded) {
        Notification::create(("Cannot load slot: " + loaded.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.0f)->show();
        return true;
    }
    m_project = loaded.unwrap();
    this->setTitle(("Edit: " + m_project.name).c_str());

    // Info / credits button (top-left)
    if (m_buttonMenu) {
        auto* infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        if (infoSpr) {
            infoSpr->setScale(0.7f);
            auto* infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr,
                [this](CCMenuItemSpriteExtra*) { this->onCredits(nullptr); });
            m_buttonMenu->addChildAtPosition(infoBtn, Anchor::TopLeft, {18.f, -18.f});
        }
        if (auto* testSpr = ButtonSprite::create("Test", "bigFont.fnt", "GJ_button_04.png", 0.38f)) {
            if (auto* testBtn = CCMenuItemExt::createSpriteExtra(testSpr,
                    [this](CCMenuItemSpriteExtra*) { this->onSelfTest(nullptr); })) {
                testBtn->setID("self-test-btn"_spr);
                m_buttonMenu->addChildAtPosition(testBtn, Anchor::TopLeft, {52.f, -18.f});
            }
        }
        if (auto* autoSpr = ButtonSprite::create("Auto", "bigFont.fnt", "GJ_button_02.png", 0.38f)) {
            if (auto* autoBtn = CCMenuItemExt::createSpriteExtra(autoSpr,
                    [this](CCMenuItemSpriteExtra*) { this->onAutoTune(nullptr); })) {
                autoBtn->setID("auto-tune-btn"_spr);
                m_buttonMenu->addChildAtPosition(autoBtn, Anchor::TopLeft, {90.f, -18.f});
            }
        }
    }

    // Layout:
    // kW
    // [PREVIEW]   [Color row]
    // [Sheet info]
    // [Status]
    // [Preset]            [Save][Gen]
    constexpr float kPreviewSlot = 110.f;       // espacio reservado a la izquierda
    constexpr float kPreviewMargin = 14.f;
    const float kRightX  = kPreviewSlot + kPreviewMargin;       // x donde empieza la columna derecha
    const float kRightW  = kW - kRightX - 14.f;                 // ancho disponible columna derecha

    // Preview del asset (izquierda)
    auto* previewHost = CCNode::create();
    previewHost->setContentSize({kPreviewSlot, kPreviewSlot});
    previewHost->setAnchorPoint({0.5f, 0.5f});
    m_mainLayer->addChildAtPosition(previewHost, Anchor::Left,
        {kPreviewMargin + kPreviewSlot / 2.f, 8.f});
    buildPreview(previewHost, {kPreviewSlot, kPreviewSlot});

    // Color picker row (arriba derecha)
    ColorPickerRowState st;
    st.color1     = m_project.color1;
    st.color2     = m_project.color2;
    st.colorGlow  = m_project.colorGlow;
    st.brightness = m_project.brightness;

    auto* pickerRow = ColorPickerRow::create(st, kRightW,
        [this](ColorField field, ColorPickerRowState const& state) {
            this->onColorChange(field, state);
        });
    if (pickerRow) {
        // Anclamos arriba a la izquierda y desplazamos a la columna derecha.
        m_mainLayer->addChildAtPosition(pickerRow, Anchor::TopLeft, {kRightX, -50.f});
        m_pickerRow = pickerRow;
    }

    // Sheet info label
    std::string sheetInfo = "Sheets: " + std::to_string(m_project.sheets.size());
    if (m_project.hasBuiltOnce) sheetInfo += "  ·  built once";
    if (auto* sheetLbl = CCLabelBMFont::create(sheetInfo.c_str(), "bigFont.fnt")) {
        sheetLbl->setScale(0.4f);
        sheetLbl->setAnchorPoint({0.5f, 0.5f});
        m_mainLayer->addChildAtPosition(sheetLbl, Anchor::TopLeft,
            {kRightX + kRightW / 2.f, -90.f});
    }

    // Status label (sobre los botones)
    if (auto* statusLbl = CCLabelBMFont::create("Ready.", "bigFont.fnt")) {
        statusLbl->setScale(0.32f);
        statusLbl->setAnchorPoint({0.5f, 0.5f});
        m_mainLayer->addChildAtPosition(statusLbl, Anchor::TopLeft,
            {kRightX + kRightW / 2.f, -110.f});
        m_statusLbl = statusLbl;
    }

    // Buttons (Presets + Save + Generate)
    if (m_buttonMenu) {
        if (auto* presetSpr = ButtonSprite::create("Preset", "bigFont.fnt", "GJ_button_05.png", 0.55f)) {
            if (auto* presetBtn = CCMenuItemExt::createSpriteExtra(presetSpr,
                    [this](CCMenuItemSpriteExtra*) {
                        auto const& presets = ColorPresets::list();
                        if (presets.empty()) return;
                        m_presetIndex = (m_presetIndex + 1) % static_cast<int>(presets.size());
                        auto const& p = presets[m_presetIndex];
                        m_project.color1     = p.color1;
                        m_project.color2     = p.color2;
                        m_project.colorGlow  = p.colorGlow;
                        m_project.brightness = p.brightness;
                        m_project.modifiedAt = nowUnixMs();
                        if (m_pickerRow) {
                            ColorPickerRowState s;
                            s.color1     = p.color1;
                            s.color2     = p.color2;
                            s.colorGlow  = p.colorGlow;
                            s.brightness = p.brightness;
                            m_pickerRow->setState(s);
                        }
                        refreshPreviewTint();
                        if (m_statusLbl) {
                            m_statusLbl->setString(("Preset: " + p.name).c_str());
                        }
                    })) {
                m_buttonMenu->addChildAtPosition(presetBtn, Anchor::BottomLeft, {38.f, 22.f});
                m_presetBtn = presetBtn;
            }
        }

        if (auto* saveSpr = ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_05.png", 0.55f)) {
            if (auto* saveBtn = CCMenuItemExt::createSpriteExtra(saveSpr,
                    [this](CCMenuItemSpriteExtra*) { this->onSave(nullptr); })) {
                m_buttonMenu->addChildAtPosition(saveBtn, Anchor::BottomRight, {-92.f, 22.f});
                m_saveBtn = saveBtn;
            }
        }

        if (auto* genSpr = ButtonSprite::create("Generate Pack", "goldFont.fnt", "GJ_button_01.png", 0.55f)) {
            if (auto* genBtn = CCMenuItemExt::createSpriteExtra(genSpr,
                    [this](CCMenuItemSpriteExtra*) { this->onGenerate(nullptr); })) {
                m_buttonMenu->addChildAtPosition(genBtn, Anchor::BottomRight, {-38.f, 22.f});
                m_genBtn = genBtn;
            }
        }
    }

    return true;
}

void ProjectEditorLayer::buildPreview(CCNode* parent, CCSize const& slot) {
    if (!parent) return;
    buildAccuratePreview(parent, slot);
}

void ProjectEditorLayer::refreshPreviewTint() {
    m_previewGeneration->fetch_add(1, std::memory_order_acq_rel);
    this->unschedule(schedule_selector(ProjectEditorLayer::renderPreviewAfterDelay));
    if (m_previewPixels && !m_previewPixels->empty()) {
        this->scheduleOnce(schedule_selector(ProjectEditorLayer::renderPreviewAfterDelay), 0.15f);
    }
}

void ProjectEditorLayer::buildAccuratePreview(CCNode* parent, CCSize const& slot) {
    constexpr float boxW = 49.f;
    constexpr float boxH = 82.f;
    auto makeBox = [&](float x, char const* label) -> CCNode* {
        auto* host = CCNode::create();
        host->setContentSize({boxW, boxH});
        host->setAnchorPoint({0.5f, 0.5f});
        host->setPosition({x, slot.height * 0.5f + 3.f});
        parent->addChild(host);
        if (auto* bg = CCScale9Sprite::create("GJ_square01.png")) {
            bg->setContentSize({boxW, boxH});
            bg->setColor({28, 28, 32});
            host->addChildAtPosition(bg, Anchor::Center);
        }
        if (auto* lbl = CCLabelBMFont::create(label, "bigFont.fnt")) {
            lbl->setScale(0.25f);
            host->addChildAtPosition(lbl, Anchor::Top, {0.f, -7.f});
        }
        if (auto* loading = CCLabelBMFont::create("...", "bigFont.fnt")) {
            loading->setScale(0.25f);
            loading->setTag(90);
            host->addChildAtPosition(loading, Anchor::Center, {0.f, -5.f});
        }
        return host;
    };
    m_originalPreviewHost = makeBox(boxW * 0.5f + 3.f, "Original");
    m_resultPreviewHost = makeBox(slot.width - boxW * 0.5f - 3.f, "Result");

    if (auto* coverage = CCLabelBMFont::create("Loading preview...", "chatFont.fnt")) {
        coverage->setScale(0.34f);
        coverage->setColor({185, 190, 200});
        coverage->limitLabelWidth(slot.width - 4.f, 0.34f, 0.18f);
        parent->addChildAtPosition(coverage, Anchor::Bottom, {0.f, 5.f});
        m_coverageLbl = coverage;
    }
    startPreviewLoad();
}

void ProjectEditorLayer::startPreviewLoad() {
    auto project = m_project;
    auto closed = m_closed;
    WeakRef<ProjectEditorLayer> weakSelf(this);
    paimon::ThreadTracker::get().spawn([weakSelf, closed, project = std::move(project)]() mutable {
        if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;
        if (!ensureRepresentativeFrame(project)) {
            Loader::get()->queueInMainThread([weakSelf, closed]() {
                if (closed->load(std::memory_order_acquire)) return;
                auto self = weakSelf.lock();
                if (self && self->getParent() && self->m_coverageLbl) {
                    self->m_coverageLbl->setString("No UI frame found");
                }
            });
            return;
        }
        int sheetIndex = project.representativeSheetIndex;
        if (sheetIndex < 0 || sheetIndex >= static_cast<int>(project.sheets.size())) return;
        auto const& sheet = project.sheets[sheetIndex];
        auto data = FramePixelCache::get().frameData(
            std::filesystem::path(sheet.sourcePlistPath),
            std::filesystem::path(sheet.sourcePngPath), project.representativeFrame);
        if (!data) return;
        auto frame = std::make_shared<FramePixelCache::FrameData>(std::move(data).unwrap());
        auto logical = std::make_shared<ImageBuffer>(
            SpritesheetReader::composeLogicalFrame(frame->pixels, frame->info));
        Loader::get()->queueInMainThread([weakSelf, closed, frame, logical,
                                          representativeFrame = project.representativeFrame,
                                          sheetIndex]() {
            if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;
            self->m_previewPixels = std::make_shared<ImageBuffer>(frame->pixels);
            self->m_previewFrameInfo = frame->info;
            self->m_project.representativeFrame = representativeFrame;
            self->m_project.representativeSheetIndex = sheetIndex;
            if (auto* loading = self->m_originalPreviewHost->getChildByTag(90)) loading->removeFromParent();
            if (auto* sprite = SpritePreviewRenderer::createSprite(*logical)) {
                auto size = sprite->getContentSize();
                if (size.width > 0.f && size.height > 0.f) {
                    sprite->setScale(std::min({40.f / size.width, 55.f / size.height, 3.f}));
                }
                self->m_originalPreviewHost->addChildAtPosition(sprite, Anchor::Center, {0.f, -5.f});
                self->m_originalPreviewSprite = sprite;
            }
            self->refreshPreviewTint();
        });
    });
}

void ProjectEditorLayer::renderPreviewAfterDelay(float) {
    if (!m_previewPixels || m_previewPixels->empty()) return;
    int generation = m_previewGeneration->load(std::memory_order_acquire);
    auto renderGeneration = m_previewGeneration;
    auto closed = m_closed;
    auto pixels = m_previewPixels;
    auto frameInfo = m_previewFrameInfo;
    SpritePreviewOptions options;
    options.colors.color1 = m_project.color1;
    options.colors.color2 = m_project.color2;
    options.colors.glow = m_project.colorGlow;
    options.brightness = m_project.brightness;
    options.alternativeGlowOverlay = m_project.alternativeGlowOverlay;
    WeakRef<ProjectEditorLayer> weakSelf(this);

    paimon::ThreadTracker::get().spawn([weakSelf, renderGeneration, closed, generation,
                                        pixels, frameInfo, options]() {
        if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;
        auto preview = SpritePreviewRenderer::renderTintedWithStats(*pixels, options);
        preview.image = SpritesheetReader::composeLogicalFrame(preview.image, frameInfo);
        auto result = std::make_shared<SpritePreviewResult>(std::move(preview));
        Loader::get()->queueInMainThread(
            [weakSelf, renderGeneration, closed, generation, result]() {
            if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire) ||
                renderGeneration->load(std::memory_order_acquire) != generation) return;
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;
            if (self->m_previewSprite) self->m_previewSprite->removeFromParent();
            if (auto* loading = self->m_resultPreviewHost->getChildByTag(90)) loading->removeFromParent();
            if (auto* sprite = SpritePreviewRenderer::createSprite(result->image)) {
                auto size = sprite->getContentSize();
                if (size.width > 0.f && size.height > 0.f) {
                    sprite->setScale(std::min({40.f / size.width, 55.f / size.height, 3.f}));
                }
                self->m_resultPreviewHost->addChildAtPosition(sprite, Anchor::Center, {0.f, -5.f});
                self->m_previewSprite = sprite;
            }
            if (self->m_coverageLbl) {
                auto const& s = result->stats;
                self->m_coverageLbl->setString(fmt::format(
                    "C1 {:.0f}%  C2 {:.0f}%  Glow {:.0f}%{}",
                    s.color1Coverage * 100.f, s.color2Coverage * 100.f,
                    s.glowCoverage * 100.f, s.needsReview ? "  !" : "").c_str());
            }
        });
    });
}

void ProjectEditorLayer::onSelfTest(CCObject*) {
    bool passed = engineSelfTest();
    Notification::create(passed ? "Texture engine self-test passed."
                                : "Texture engine self-test failed; check the log.",
        passed ? NotificationIcon::Success : NotificationIcon::Error, 3.f)->show();
}

void ProjectEditorLayer::onAutoTune(CCObject*) {
    if (!m_previewPixels || m_previewPixels->empty()) {
        Notification::create("Preview still loading; try again in a moment.",
            NotificationIcon::Warning, 2.0f)->show();
        return;
    }
    if (m_statusLbl) m_statusLbl->setString("Auto-tuning...");

    SpritePreviewOptions base;
    base.colors.color1 = m_project.color1;
    base.colors.color2 = m_project.color2;
    base.colors.glow   = m_project.colorGlow;
    base.brightness    = m_project.brightness;
    base.alternativeGlowOverlay = m_project.alternativeGlowOverlay;

    auto pixels = m_previewPixels;
    auto closed = m_closed;
    WeakRef<ProjectEditorLayer> weakSelf(this);

    paimon::ThreadTracker::get().spawn([weakSelf, closed, pixels, base]() {
        if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;
        auto suggestion = std::make_shared<AutoTuner::Suggestion>(
            AutoTuner::tuneForSprite(*pixels, base));
        Loader::get()->queueInMainThread([weakSelf, closed, suggestion]() {
            if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;
            if (!suggestion->changed) {
                if (self->m_statusLbl) {
                    self->m_statusLbl->setString(fmt::format(
                        "Auto: brightness {} already optimal.",
                        suggestion->suggestedBrightness).c_str());
                }
                return;
            }
            self->m_project.brightness = suggestion->suggestedBrightness;
            self->m_project.modifiedAt = nowUnixMs();
            if (self->m_pickerRow) {
                ColorPickerRowState s;
                s.color1     = self->m_project.color1;
                s.color2     = self->m_project.color2;
                s.colorGlow  = self->m_project.colorGlow;
                s.brightness = self->m_project.brightness;
                self->m_pickerRow->setState(s);
            }
            self->refreshPreviewTint();
            if (self->m_statusLbl) {
                self->m_statusLbl->setString(fmt::format(
                    "Auto: brightness -> {} (unsaved).",
                    suggestion->suggestedBrightness).c_str());
            }
        });
    });
}

void ProjectEditorLayer::onClose(CCObject* sender) {
    m_closed->store(true, std::memory_order_release);
    m_previewGeneration->fetch_add(1, std::memory_order_acq_rel);
    this->unschedule(schedule_selector(ProjectEditorLayer::renderPreviewAfterDelay));
    Popup::onClose(sender);
}

void ProjectEditorLayer::onCredits(CCObject*) {
    // Recoloring algorithm + asset pack: PackGen (by Asterveila).
    // We acknowledge the original work and offer a quick link out to it.
    std::string body =
        "Texture Studio uses the recoloring approach pioneered by\n"
        "<cy>PackGen</c> by <cl>Asterveila</c>:\n"
        "  packgenweb.pages.dev\n\n"
        "Algorithm: per-pixel <cj>luminance tinting</c> using\n"
        "<cy>0.30R + 0.59G + 0.11B</c> as the base luminance, then\n"
        "tinting by user color * (luminance / brightness).\n\n"
        "Open the PackGen website in your browser?";

    geode::createQuickPopup(
        "Credits",
        body,
        "Close", "Open Site",
        [](FLAlertLayer*, bool yes) {
            if (yes) {
                geode::utils::web::openLinkInBrowser(
                    "https://packgenweb.pages.dev/");
            }
        });
}

void ProjectEditorLayer::onColorChange(ColorField, ColorPickerRowState const& state) {
    m_project.color1     = state.color1;
    m_project.color2     = state.color2;
    m_project.colorGlow  = state.colorGlow;
    m_project.brightness = state.brightness;
    m_project.modifiedAt = nowUnixMs();
    refreshPreviewTint();
    if (m_statusLbl) m_statusLbl->setString("Edited (unsaved).");
}

void ProjectEditorLayer::onSave(CCObject*) {
    auto r = SlotStore::get().saveSlot(m_project);
    if (!r) {
        Notification::create(("Save failed: " + r.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.0f)->show();
        return;
    }
    if (m_statusLbl) m_statusLbl->setString("Saved.");
    Notification::create("Slot saved.", NotificationIcon::Success, 1.5f)->show();
}

void ProjectEditorLayer::setBusy(bool busy) {
    auto disableBtn = [busy](CCMenuItemSpriteExtra* btn) {
        if (!btn) return;
        btn->setEnabled(!busy);
        // Visual feedback: bajamos opacidad cuando esta inactivo.
        if (auto* spr = typeinfo_cast<CCSprite*>(btn->getNormalImage())) {
            spr->setOpacity(busy ? 120 : 255);
        }
    };
    disableBtn(m_genBtn);
    disableBtn(m_saveBtn);
    disableBtn(m_presetBtn);
}

void ProjectEditorLayer::onGenerate(CCObject*) {
    // Reentrancia: si ya hay una generacion en curso, ignorar el click.
    // (setBusy desactiva el boton, pero un toque rapido puede colarse.)
    if (m_generating->load(std::memory_order_acquire)) {
        return;
    }

    // Save first so the disk reflects any pending edits. Esto es sincrono
    // y barato (un par de KB de JSON), no congela el hilo principal.
    onSave(nullptr);

    auto cfg = m_project.toExportConfig();
    if (cfg.sheets.empty()) {
        Notification::create("No sheets in this slot.",
            NotificationIcon::Warning, 2.0f)->show();
        return;
    }

    auto outPath = SlotPaths::outputZipFile(m_project.id);
    if (m_statusLbl) m_statusLbl->setString("Generating...");

    // Bloqueamos UI mientras corre el thread.
    m_generating->store(true, std::memory_order_release);
    setBusy(true);

    // Capturamos por valor todo lo que el thread necesita; nada del thread
    // toca `this` directamente. Para volver a la UI usamos un WeakRef +
    // queueInMainThread. Si el popup se cierra mientras corre el export,
    // simplemente perdemos el callback (el zip se sigue escribiendo).
    WeakRef<ProjectEditorLayer> weakSelf(this);
    auto generating = m_generating;  // shared_ptr, copiado para el thread
    std::string projectId = m_project.id;
    PackExportConfig cfgCopy = cfg;
    std::filesystem::path outPathCopy = outPath;

    paimon::ThreadTracker::get().spawn([weakSelf, generating, projectId, cfgCopy, outPathCopy]() {
        if (paimon::isRuntimeShuttingDown()) {
            generating->store(false, std::memory_order_release);
            return;
        }

        // === Trabajo CPU-bound: corre fuera del main thread ===
        // SheetTinter / RectPacker / PNG encode son todos puro RAM, sin
        // OpenGL ni cocos2d, asi que es seguro hacerlo aqui. Sin esto, en
        // packs con varios sheets el main thread se queda colgado durante
        // segundos y Windows mata el proceso por "no responder".
        geode::Result<PackExportResult> result = Err("not started");
        try {
            result = PackExporter::exportPack(cfgCopy, outPathCopy);
        } catch (std::exception const& e) {
            result = Err(std::string("exception: ") + e.what());
        } catch (...) {
            result = Err("unknown exception during export");
        }

        if (paimon::isRuntimeShuttingDown()) {
            generating->store(false, std::memory_order_release);
            return;
        }

        // Movemos el resultado a un shared_ptr para poder pasarlo al main
        // thread (los lambdas de queueInMainThread requieren copy-able).
        auto resultPtr = std::make_shared<geode::Result<PackExportResult>>(std::move(result));

        Loader::get()->queueInMainThread([weakSelf, generating, projectId, resultPtr]() mutable {
            // Marcamos no-busy *antes* de tocar el popup para que aunque
            // el popup ya no exista, el flag refleje el estado real.
            generating->store(false, std::memory_order_release);

            if (paimon::isRuntimeShuttingDown()) return;

            auto self = weakSelf.lock();
            if (!self || !self->getParent()) {
                // Popup cerrado mientras se generaba: el zip ya se
                // escribio, pero no podemos refrescar la UI. Persistir
                // el estado de "built" igual via SlotStore.
                if (resultPtr && *resultPtr) {
                    auto loaded = SlotStore::get().loadSlot(projectId);
                    if (loaded) {
                        auto p = loaded.unwrap();
                        p.hasBuiltOnce  = true;
                        p.lastBuiltAt   = nowUnixMs();
                        p.lastZipRelPath = "output/pack.zip";
                        (void)SlotStore::get().saveSlot(p);
                    }
                }
                return;
            }

            self->setBusy(false);

            if (!resultPtr || !*resultPtr) {
                if (self->m_statusLbl) self->m_statusLbl->setString("Generate failed.");
                std::string err = resultPtr ? resultPtr->unwrapErr() : std::string("internal error");
                Notification::create(("Failed: " + err).c_str(),
                    NotificationIcon::Error, 4.0f)->show();
                return;
            }
            auto exportRes = resultPtr->unwrap();

            // Persist the build state.
            self->m_project.hasBuiltOnce  = true;
            self->m_project.lastBuiltAt   = nowUnixMs();
            self->m_project.lastZipRelPath = "output/pack.zip";
            (void)SlotStore::get().saveSlot(self->m_project);

            if (self->m_statusLbl) {
                self->m_statusLbl->setString(
                    ("Generated " + std::to_string(exportRes.outputZipSizeBytes / 1024) + " KB").c_str());
            }
            Notification::create("Pack generated! Open the slot folder to see it.",
                NotificationIcon::Success, 3.0f)->show();
        });
    });
}

}  // namespace paimon::texture_studio
