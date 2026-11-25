#include "../../../utils/FilesystemPath.hpp"
#include "StingerConfigPopup.hpp"
#include "../services/TransitionTimeline.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../ui/PaimonUI.hpp"
using namespace geode::prelude;
using namespace paimon::transitions;

namespace ui = paimon::ui;

StingerConfigPopup* StingerConfigPopup::create(TransitionConfig config, std::function<void(TransitionConfig)> save) {
    auto* popup = new StingerConfigPopup();
    if (popup->init(std::move(config), std::move(save))) { popup->autorelease(); return popup; }
    delete popup; return nullptr;
}
bool StingerConfigPopup::init(TransitionConfig config, std::function<void(TransitionConfig)> save) {
    if (!PaimonPopup::init(400.f, 300.f)) return false;
    setTitle("Stinger / OBS");
    addInfoButton("Stinger / OBS",
        "Imagen, GIF o video convertidos a hojas PNG para cortar la escena, como un stinger de OBS.\n\n"
        "Ajusta la <cy>duracion</c> y el <cy>punto de corte</c>: el cambio de escena debe quedar oculto tras el medio. "
        "Video sin audio ni alpha; GIF/PNG mantienen transparencia.");
    m_config = std::move(config); m_save = std::move(save);
    TransitionManager::sanitizeConfig(m_config);
    WeakRef<StingerConfigPopup> self = this;
    auto ws = m_mainLayer->getContentSize();

    auto label = [this](char const* text, float y, float scale) {
        auto* node = CCLabelBMFont::create(text, "bigFont.fnt");
        node->setPosition({200.f, y}); node->setScale(scale); m_mainLayer->addChild(node); return node;
    };

    auto previewPanel = ui::makePanel({ws.width - 24.f, 136.f}, "Vista previa", 95);
    previewPanel->setPosition({12.f, 118.f});
    m_mainLayer->addChild(previewPanel);

    m_preview = CCLayerColor::create({30, 30, 40, 255}, 320.f, 96.f);
    m_preview->setPosition({ws.width / 2.f - 160.f, 130.f});
    m_mainLayer->addChild(m_preview);
    // idle view: both scenes side by side, so A and B read before playing.
    m_split = CCNode::create();
    m_preview->addChild(m_split, 1);
    auto half = [this](float x, ccColor4B col, char const* name, char const* sub) {
        auto* pane = CCLayerColor::create(col, 160.f, 96.f);
        pane->setPosition({x, 0.f});
        m_split->addChild(pane);
        auto* tag = CCLabelBMFont::create(name, "goldFont.fnt");
        tag->setScale(.42f); tag->setPosition({x + 80.f, 56.f});
        m_split->addChild(tag);
        auto* subTag = CCLabelBMFont::create(sub, "chatFont.fnt");
        subTag->setScale(.3f); subTag->setPosition({x + 80.f, 36.f});
        m_split->addChild(subTag);
    };
    half(0.f, {45, 90, 160, 255}, "ESCENA A", "(la que sale)");
    half(160.f, {45, 160, 90, 255}, "ESCENA B", "(la que entra)");
    // playback view: the current scene fills the box, B takes over at the cut.
    auto fullPane = [this](ccColor4B col, char const* name) {
        auto* pane = CCLayerColor::create(col, 320.f, 96.f);
        pane->setVisible(false);
        auto* tag = CCLabelBMFont::create(name, "goldFont.fnt");
        tag->setScale(.55f); tag->setPosition({160.f, 50.f});
        pane->addChild(tag);
        m_preview->addChild(pane, 2);
        return pane;
    };
    m_fullA = fullPane({45, 90, 160, 255}, "ESCENA A");
    m_fullB = fullPane({45, 160, 90, 255}, "ESCENA B");
    m_hint = CCLabelBMFont::create("Importa una imagen, GIF o video para verla aqui", "chatFont.fnt");
    m_hint->setScale(.3f); m_hint->setPosition({160.f, 14.f});
    m_preview->addChild(m_hint, 15);
    auto* progressTrack = CCLayerColor::create({0, 0, 0, 130}, 320.f, 7.f);
    m_preview->addChild(progressTrack, 18);
    m_progress = CCLayerColor::create({255, 200, 60, 255}, 1.f, 7.f);
    m_progress->setScaleX(0.f);
    m_preview->addChild(m_progress, 19);
    m_cutMark = CCLayerColor::create({255, 255, 255, 255}, 2.f, 12.f);
    m_preview->addChild(m_cutMark, 20);
    m_stateTag = CCLabelBMFont::create("SIN MEDIO", "chatFont.fnt");
    m_stateTag->setScale(.3f); m_stateTag->setAnchorPoint({0.f, 1.f});
    m_stateTag->setPosition({6.f, 92.f});
    m_preview->addChild(m_stateTag, 21);

    m_timing = label("", 108.f, .32f);
    m_timing->setColor(ui::palette::gold);

    auto stepPanel = ui::makePanel({ws.width - 24.f, 56.f}, "Ajustes", 95);
    stepPanel->setPosition({12.f, 56.f});
    m_mainLayer->addChild(stepPanel);

    auto* menu = CCMenu::create(); menu->setPosition({0, 0}); m_mainLayer->addChild(menu);

    // captions collide with the panel heading, so each arrow pair gets a
    // value label between the arrows instead (the timing line names them).
    m_cutVal = CCLabelBMFont::create("", "bigFont.fnt");
    m_cutVal->setScale(.3f); m_cutVal->setPosition({90.f, 74.f});
    m_cutVal->setColor(ui::palette::gold);
    m_mainLayer->addChild(m_cutVal);
    m_durVal = CCLabelBMFont::create("", "bigFont.fnt");
    m_durVal->setScale(.3f); m_durVal->setPosition({250.f, 74.f});
    m_durVal->setColor(ui::palette::gold);
    m_mainLayer->addChild(m_durVal);

    auto arrow = [menu](bool left, CCPoint pos, auto callback) {
        auto* spr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        if (!left) spr->setFlipX(true);
        spr->setScale(0.5f);
        auto* item = CCMenuItemExt::createSpriteExtra(spr, callback);
        item->setPosition(pos); menu->addChild(item);
    };
    arrow(true, {58, 74}, [self](auto*) { if (auto p = self.lock()) { p->m_config.cutPoint = std::max(0.f, p->m_config.cutPoint - .05f); p->refresh(); } });
    arrow(false, {122, 74}, [self](auto*) { if (auto p = self.lock()) { p->m_config.cutPoint = std::min(1.f, p->m_config.cutPoint + .05f); p->refresh(); } });
    arrow(true, {218, 74}, [self](auto*) { if (auto p = self.lock()) { p->m_config.duration = std::max(.1f, p->m_config.duration - .1f); p->refresh(); } });
    arrow(false, {282, 74}, [self](auto*) { if (auto p = self.lock()) { p->m_config.duration = std::min(30.f, p->m_config.duration + .1f); p->refresh(); } });

    auto button = [menu](char const* title, CCPoint pos, ui::Btn skin, auto callback) {
        auto* item = CCMenuItemExt::createSpriteExtra(ui::makeButtonSprite(title, skin, 0.f, .55f, "bigFont.fnt"), callback);
        item->setPosition(pos); menu->addChild(item);
    };
    button("Importar", {80, 32}, ui::Btn::Cyan, [self](auto*) { if (auto p = self.lock()) p->import(); });
    button("Preview", {160, 32}, ui::Btn::Cyan, [self](auto*) { if (auto p = self.lock()) p->preview(); });
    button("Original", {240, 32}, ui::Btn::Gray, [self](auto*) { if (auto p = self.lock(); p && p->m_media) { p->m_config.duration = p->m_media->duration(); p->refresh(); } });
    button("Guardar", {330, 32}, ui::Btn::Green, [self](auto*) {
        if (auto p = self.lock()) {
            if (p->m_busy || !p->m_media) { p->m_status->setString("Espera a que el medio este preparado."); return; }
            p->m_config.type = TransitionType::Stinger;
            p->m_config.mediaPath = p->m_media->manifest;
            if (p->m_save) p->m_save(p->m_config);
            p->onClose(nullptr);
        }
    });

    m_status = label("Elige un medio. El corte debe quedar cubierto.", 12.f, .25f);
    m_status->setColor(ui::palette::muted);

    refresh();
    if (!m_config.mediaPath.empty()) {
        m_busy = true; m_status->setString("Cargando hojas...");
        prepareTransitionMedia(m_config.mediaPath, [self](auto media, auto error) {
            if (auto p = self.lock()) {
                p->m_busy = false; p->m_media = std::move(media);
                p->m_status->setString(p->m_media ? "Listo. Ajusta el corte y prueba." : error.c_str()); p->refresh();
            }
        });
    }
    return true;
}
void StingerConfigPopup::refresh() {
    m_timing->setString(fmt::format("Corte {:.0f}% ({:.0f}ms)  |  Duracion {:.2f}s",
        m_config.cutPoint * 100, m_config.duration * m_config.cutPoint * 1000, m_config.duration).c_str());
    if (m_cutVal) m_cutVal->setString(fmt::format("{:.0f}%", m_config.cutPoint * 100.f).c_str());
    if (m_durVal) m_durVal->setString(fmt::format("{:.2f}s", m_config.duration).c_str());
    if (m_cutMark) m_cutMark->setPosition({m_config.cutPoint * 320.f - 1.f, 0.f});
    if (m_playing) { m_playing = false; unscheduleUpdate(); }
    showIdle();
}
void StingerConfigPopup::showIdle() {
    if (!m_preview) return;
    if (m_split) m_split->setVisible(true);
    if (m_fullA) m_fullA->setVisible(false);
    if (m_fullB) m_fullB->setVisible(false);
    if (m_progress) m_progress->setScaleX(0.f);
    if (m_hint) m_hint->setVisible(!m_media);
    if (m_stateTag) m_stateTag->setString(m_media ? "LISTO - PULSA PREVIEW" : "SIN MEDIO");
    if (m_media) {
        if (!m_overlay) { m_overlay = CCSprite::create(); m_preview->addChild(m_overlay, 10); }
        m_media->apply(m_overlay, 0);
        float s = std::max(320.f / std::max(1, m_media->width), 96.f / std::max(1, m_media->height));
        m_overlay->setPosition({160.f, 48.f});
        m_overlay->setScale(s);
        m_overlay->setVisible(true);
    } else if (m_overlay) {
        m_overlay->setVisible(false);
    }
}
void StingerConfigPopup::import() {
    if (m_busy) return;
    m_busy = true;
    WeakRef<StingerConfigPopup> self = this;
    pt::pickMedia([self](Result<std::optional<std::filesystem::path>> result) {
        auto p = self.lock(); if (!p) return;
        if (!result) { p->m_busy = false; p->m_status->setString("No se pudo abrir el selector."); return; }
        auto path = result.unwrap(); if (!path) { p->m_busy = false; return; }
        p->m_busy = true; p->m_status->setString("Convirtiendo a hojas... Puedes cerrar esta ventana.");
        prepareTransitionMedia(paimon::paths::toUtf8(*path), [self](auto media, auto error) {
            if (auto p = self.lock()) {
                p->m_busy = false;
                if (media) {
                    p->m_media = std::move(media); p->m_config.mediaPath = p->m_media->manifest;
                    p->m_config.duration = p->m_media->duration();
                    p->m_status->setString(fmt::format("{} frames | {} hojas | {:.1f} MB",
                        p->m_media->endsMs.size(), p->m_media->pages.size(), p->m_media->bytes / 1048576.f).c_str());
                    p->refresh(); p->preview();
                } else p->m_status->setString(error.c_str());
                p->m_status->limitLabelWidth(370.f, .25f, .15f);
            }
        });
    });
}
void StingerConfigPopup::preview() {
    if (m_busy) return;
    if (!m_media) { m_status->setString("Primero importa una imagen, GIF o video."); return; }
    if (!m_overlay) { m_overlay = CCSprite::create(); m_preview->addChild(m_overlay, 10); }
    m_media->apply(m_overlay, 0);
    float s = std::max(320.f / std::max(1, m_media->width), 96.f / std::max(1, m_media->height));
    m_overlay->setPosition({160.f, 48.f});
    m_overlay->setScale(s);
    m_overlay->setVisible(true);
    if (m_split) m_split->setVisible(false);
    if (m_fullA) m_fullA->setVisible(true);
    if (m_fullB) m_fullB->setVisible(false);
    if (m_hint) m_hint->setVisible(false);
    m_elapsed = 0; m_playing = true; update(0); scheduleUpdate();
}
void StingerConfigPopup::update(float dt) {
    if (!m_playing || !m_media) return;
    m_elapsed += bounded(dt, 0, 0, 30);
    bool beforeCut = m_elapsed < m_config.duration * m_config.cutPoint;
    if (m_fullA) m_fullA->setVisible(beforeCut);
    if (m_fullB) m_fullB->setVisible(!beforeCut);
    m_media->apply(m_overlay, m_elapsed / m_config.duration * m_media->duration());
    if (m_progress)
        m_progress->setScaleX(320.f * std::clamp(m_elapsed / std::max(.01f, m_config.duration), 0.f, 1.f));
    if (m_stateTag)
        m_stateTag->setString(fmt::format("{} - ESCENA {}", "REPRODUCIENDO", beforeCut ? "A" : "B").c_str());
    if (m_status)
        m_status->setString(fmt::format("Vista previa: {:.2f}s / {:.2f}s - {}",
            std::min(m_elapsed, m_config.duration), m_config.duration,
            beforeCut ? "Escena A (azul) -> el corte cambia a B" : "Escena B (verde)").c_str());
    if (m_elapsed >= m_config.duration) {
        m_playing = false; unscheduleUpdate();
        showIdle();
        if (m_status) m_status->setString("Vista previa terminada. Ajusta el corte y prueba otra vez.");
    }
}
