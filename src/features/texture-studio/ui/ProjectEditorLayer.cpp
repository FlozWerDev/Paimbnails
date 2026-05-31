#include "ProjectEditorLayer.hpp"

#include "../engine/ColorPresets.hpp"
#include "../engine/PackExporter.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/SlotStore.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/cocos/textures/CCTextureCache.h>
#include <Geode/loader/Loader.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>
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

    // ── Info / credits button (top-left) ──────────────────────────────
    if (m_buttonMenu) {
        auto* infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        if (infoSpr) {
            infoSpr->setScale(0.7f);
            auto* infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr,
                [this](CCMenuItemSpriteExtra*) { this->onCredits(nullptr); });
            m_buttonMenu->addChildAtPosition(infoBtn, Anchor::TopLeft, {18.f, -18.f});
        }
    }

    // Layout:
    //   ┌─ kW ───────────────────────────┐
    //   │ [PREVIEW]   [Color row]        │
    //   │             [Sheet info]       │
    //   │             [Status]           │
    //   │ [Preset]            [Save][Gen]│
    //   └────────────────────────────────┘
    constexpr float kPreviewSlot = 110.f;       // espacio reservado a la izquierda
    constexpr float kPreviewMargin = 14.f;
    const float kRightX  = kPreviewSlot + kPreviewMargin;       // x donde empieza la columna derecha
    const float kRightW  = kW - kRightX - 14.f;                 // ancho disponible columna derecha

    // ── Preview del asset (izquierda) ─────────────────────────────────
    auto* previewHost = CCNode::create();
    previewHost->setContentSize({kPreviewSlot, kPreviewSlot});
    previewHost->setAnchorPoint({0.5f, 0.5f});
    m_mainLayer->addChildAtPosition(previewHost, Anchor::Left,
        {kPreviewMargin + kPreviewSlot / 2.f, 8.f});
    buildPreview(previewHost, {kPreviewSlot, kPreviewSlot});

    // ── Color picker row (arriba derecha) ─────────────────────────────
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

    // ── Sheet info label ──────────────────────────────────────────────
    std::string sheetInfo = "Sheets: " + std::to_string(m_project.sheets.size());
    if (m_project.hasBuiltOnce) sheetInfo += "  ·  built once";
    if (auto* sheetLbl = CCLabelBMFont::create(sheetInfo.c_str(), "bigFont.fnt")) {
        sheetLbl->setScale(0.4f);
        sheetLbl->setAnchorPoint({0.5f, 0.5f});
        m_mainLayer->addChildAtPosition(sheetLbl, Anchor::TopLeft,
            {kRightX + kRightW / 2.f, -90.f});
    }

    // ── Status label (sobre los botones) ──────────────────────────────
    if (auto* statusLbl = CCLabelBMFont::create("Ready.", "bigFont.fnt")) {
        statusLbl->setScale(0.32f);
        statusLbl->setAnchorPoint({0.5f, 0.5f});
        m_mainLayer->addChildAtPosition(statusLbl, Anchor::TopLeft,
            {kRightX + kRightW / 2.f, -110.f});
        m_statusLbl = statusLbl;
    }

    // ── Buttons (Presets + Save + Generate) ───────────────────────────
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

    // Fondo (frame) para que se distinga aun cuando no haya sprite cargado.
    if (auto* frame = CCScale9Sprite::create("GJ_square01.png")) {
        frame->setContentSize(slot);
        frame->setColor({28, 28, 28});
        parent->addChildAtPosition(frame, Anchor::Center);
    }

    // Buscamos el primer sheet con png válido.
    std::string pngPath;
    for (auto const& s : m_project.sheets) {
        if (s.sourcePngPath.empty()) continue;
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(s.sourcePngPath), ec)) {
            pngPath = s.sourcePngPath;
            break;
        }
    }

    CCSprite* preview = nullptr;
    if (!pngPath.empty()) {
        // Forzamos recarga: si ya hubo cambios en disco esto evita una textura stale.
        auto* cache = CCTextureCache::sharedTextureCache();
        if (cache) {
            cache->removeTextureForKey(pngPath.c_str());
            if (auto* tex = cache->addImage(pngPath.c_str(), false)) {
                preview = CCSprite::createWithTexture(tex);
            }
        }
    }

    if (!preview) {
        // Fallback: ícono genérico para que el slot no se vea vacío.
        preview = CCSprite::createWithSpriteFrameName("GJ_squareIcon_001.png");
        if (!preview) preview = CCSprite::create("square.png");
    }
    if (!preview) {
        // Estado extremo: solo etiqueta.
        if (auto* lbl = CCLabelBMFont::create("(no preview)", "bigFont.fnt")) {
            lbl->setScale(0.35f);
            parent->addChildAtPosition(lbl, Anchor::Center);
        }
        return;
    }

    // Encajar el sprite dentro del slot manteniendo proporción.
    auto sz = preview->getContentSize();
    if (sz.width > 0 && sz.height > 0) {
        // Margen interior de 6px para que se vea el frame.
        float maxW = std::max(8.f, slot.width  - 12.f);
        float maxH = std::max(8.f, slot.height - 12.f);
        float scale = std::min(maxW / sz.width, maxH / sz.height);
        // Limitamos el upscale para que un sprite 16x16 no se pixele al máximo.
        scale = std::min(scale, 4.f);
        preview->setScale(scale);
    }
    preview->setAnchorPoint({0.5f, 0.5f});
    parent->addChildAtPosition(preview, Anchor::Center);
    m_previewSprite = preview;

    // Aplicamos color1 como tinte inicial para anticipar el resultado.
    refreshPreviewTint();
}

void ProjectEditorLayer::refreshPreviewTint() {
    if (!m_previewSprite) return;
    // Tinte aproximado: usamos color1, que es el color principal del pack.
    // No es el resultado real del PackExporter, pero da una idea visual
    // inmediata sin tener que regenerar el sheet.
    m_previewSprite->setColor(m_project.color1);
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
