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

    m_preview = CCLayerColor::create({45, 90, 160, 255}, 200.f, 90.f);
    m_preview->setPosition({ws.width / 2.f - 100.f, 124.f});
    m_mainLayer->addChild(m_preview);
    auto* a = CCLabelBMFont::create("A  ->  B", "goldFont.fnt");
    a->setPosition({100.f, 45.f}); a->setScale(.65f); m_preview->addChild(a);

    m_timing = label("", 108.f, .32f);
    m_timing->setColor(ui::palette::gold);

    auto stepPanel = ui::makePanel({ws.width - 24.f, 56.f}, "Ajustes", 95);
    stepPanel->setPosition({12.f, 56.f});
    m_mainLayer->addChild(stepPanel);

    auto* menu = CCMenu::create(); menu->setPosition({0, 0}); m_mainLayer->addChild(menu);

    auto stepLabel = [this](char const* text, float x, float y) {
        auto* node = ui::makeLabel(text, 80.f, 0.34f, ui::palette::muted);
        node->setPosition({x, y}); m_mainLayer->addChild(node);
    };
    stepLabel("Corte", 90.f, 92.f);
    stepLabel("Duracion", 250.f, 92.f);

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
    button("Original", {240, 32}, ui::Btn::Gray, [self](auto*) { if (auto p = self.lock()) { if (p->m_media) p->m_config.duration = p->m_media->duration(); p->refresh(); } });
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
    m_timing->setString(fmt::format("Duracion {:.2f}s  |  Corte {:.0f}% ({:.0f}ms)",
        m_config.duration, m_config.cutPoint * 100, m_config.duration * m_config.cutPoint * 1000).c_str());
    if (m_playing) { m_playing = false; unscheduleUpdate(); if (m_overlay) m_overlay->setVisible(false); }
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
        prepareTransitionMedia(utils::string::pathToString(*path), [self](auto media, auto error) {
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
    if (!m_media || m_busy) return;
    if (!m_overlay) { m_overlay = CCSprite::create(); m_preview->addChild(m_overlay, 10); }
    m_media->apply(m_overlay, 0);
    m_overlay->setPosition({100, 45});
    m_overlay->setScaleX(200.f / m_media->width); m_overlay->setScaleY(90.f / m_media->height);
    m_overlay->setVisible(true); m_elapsed = 0; m_playing = true; update(0); scheduleUpdate();
}
void StingerConfigPopup::update(float dt) {
    if (!m_playing || !m_media) return;
    m_elapsed += bounded(dt, 0, 0, 30);
    m_preview->setColor(m_elapsed < m_config.duration * m_config.cutPoint ? ccColor3B{45, 90, 160} : ccColor3B{45, 160, 90});
    m_media->apply(m_overlay, m_elapsed / m_config.duration * m_media->duration());
    if (m_elapsed >= m_config.duration) { m_playing = false; m_overlay->setVisible(false); unscheduleUpdate(); }
}
