#include "../../ui/PaimonPopup.hpp"
#include "CustomHover.hpp"
#include "../../ui/PaiConfigKit.hpp"
#include "../../ui/PaimonUI.hpp"
#include "../../utils/DynamicPopupRegistry.hpp"
#include "../../utils/SafeNodeHandle.hpp"
#include "../../core/modules/ModuleRegistry.hpp"
#include "../../core/Settings.hpp"
#include "../icon-copy/ui/IconSetNamePopup.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::hover {
namespace {

namespace kit = paimon::configkit;

class HoverPopup;
void clearActivePopup(HoverPopup* popup);
void setActivePopup(HoverPopup* popup);

class HoverPopup final : public PaimonPopup {
    std::string key;
    int tab = 0;
    ScrollLayer* scroll = nullptr;
    paimon::SafeNodeHandle<CCNode> preview;
    paimon::SafeNodeHandle<CCMenuItemSpriteExtra> previewButton;
    paimon::SafeNodeHandle<CCLabelBMFont> previewStatus;
    float elapsed = 0.f;
    float flushTimer = -1.f;
    bool pending = false;

    Config config() const {
        return Manager::get().resolve(key);
    }

    void changed(Config value, bool persistNow = true) {
        Manager::get().set(key, value, persistNow);
        elapsed = 0.f;
        if (!persistNow) {
            flushTimer = 0.35f;
        } else {
            flushTimer = -1.f;
        }
    }

    void rebuildLater() {
        if (pending) return;
        pending = true;
        paimon::SafeNodeHandle<HoverPopup> self(this);
        Loader::get()->queueInMainThread([self] {
            auto* owner = self.get();
            if (!owner) return;
            owner->pending = false;
            if (owner->getParent()) owner->rebuild();
        });
    }

    CCNode* slider(char const* name, float Config::*field, float lo, float hi,
                   char const* unit = "") {
        paimon::SafeNodeHandle<HoverPopup> self(this);
        return kit::makeSliderRow(376.f, name, nullptr, config().*field, lo, hi,
            [unit](double value) { return fmt::format("{:.2f}{}", value, unit); },
            [self, field](double value) {
                auto* owner = self.get();
                if (!owner) return;
                auto current = owner->config();
                current.*field = static_cast<float>(value);
                owner->changed(current, false);
            });
    }

    void rebuild() {
        if (scroll) {
            scroll->removeFromParent();
            scroll = nullptr;
        }

        auto& manager = Manager::get();
        auto current = config();
        paimon::SafeNodeHandle<HoverPopup> self(this);

        std::vector<CCNode*> rows;
        rows.push_back(kit::makeHint(396.f, key.empty()
            ? "Configuracion global. Ctrl+clic en un boton para personalizarlo."
            : "Boton seleccionado. Los cambios se ven al instante."));
        rows.push_back(kit::makeToggleRow(396.f, "Custom Hover",
            "Cursor en PC; deslizar el dedo en movil.",
            paimon::modules::isEnabled("paimbnails.customhover.global"),
            [](bool enabled) {
                paimon::modules::setEnabled("paimbnails.customhover.global", enabled);
            }));
        rows.push_back(kit::makeTabBar(396.f, {"Presets", "Ajustes", "Guardados"}, tab,
            [self](int selected) {
                auto* owner = self.get();
                if (!owner) return;
                owner->tab = selected;
                owner->rebuildLater();
            }));

        if (tab == 0) {
            std::vector<CCNode*> scopeRows;
            scopeRows.push_back(kit::makeToggleRow(376.f, "Todos vinculados",
                "Ctrl+D: usa este estilo en todos. Desactiva para recuperar los individuales.",
                manager.linked, [self](bool enabled) {
                    auto* owner = self.get();
                    if (!owner) return;
                    auto& mgr = Manager::get();
                    if (enabled) {
                        mgr.group(owner->config());
                    } else {
                        mgr.setLinked(false);
                    }
                    owner->rebuildLater();
                }));
            scopeRows.push_back(kit::makeButtonRow(376.f, "Elegir boton",
                "Cierra esta ventana y toca el boton que quieras editar.", "Elegir", [self] {
                    auto* owner = self.get();
                    if (!owner) return;
                    Manager::get().picking = true;
                    owner->onClose(nullptr);
                    Notification::create("Toca un boton para editar su hover (Esc cancela)",
                        NotificationIcon::Info)->show();
                }));
            if (manager.picking) {
                scopeRows.push_back(kit::makeButtonRow(376.f, "Cancelar seleccion",
                    "Desactiva el modo Elegir boton.", "Cancelar", [self] {
                        Manager::get().picking = false;
                        if (auto* owner = self.get()) owner->rebuildLater();
                    }, paimon::ui::Btn::Gray));
            }
            rows.push_back(kit::makeCard(396.f, "Aplicar a", {120, 210, 255}, scopeRows));

            std::vector<CCNode*> choices;
            choices.reserve(presets().size());
            for (auto const& preset : presets()) {
                bool const activePreset = configsMatch(current, preset.config);
                choices.push_back(kit::makeButtonRow(
                    376.f,
                    preset.name.c_str(),
                    nullptr,
                    activePreset ? "Activo" : "Usar",
                    [self, value = preset.config] {
                        auto* owner = self.get();
                        if (!owner) return;
                        owner->changed(value, true);
                        owner->rebuildLater();
                    },
                    activePreset ? paimon::ui::Btn::Cyan : paimon::ui::Btn::Green));
            }
            rows.push_back(kit::makeCard(396.f, "24 estilos listos para usar",
                {255, 210, 100}, choices));
        } else if (tab == 1) {
            rows.push_back(kit::makeCard(396.f, "Forma y movimiento", {120, 210, 255}, {
                kit::makeToggleRow(376.f, "Animar este estilo", nullptr, current.enabled,
                    [self](bool enabled) {
                        auto* owner = self.get();
                        if (!owner) return;
                        auto value = owner->config();
                        value.enabled = enabled;
                        owner->changed(value, true);
                    }),
                slider("Escala", &Config::scale, .5f, 1.8f, "x"),
                slider("Ancho / alto", &Config::stretch, .6f, 1.5f, "x"),
                slider("Desplazamiento vertical", &Config::lift, -25.f, 25.f, " px"),
                slider("Desplazamiento horizontal", &Config::slide, -25.f, 25.f, " px"),
                slider("Rotacion", &Config::rotation, -180.f, 180.f, " deg")
            }));
            rows.push_back(kit::makeCard(396.f, "Tiempo", {255, 200, 100}, {
                kit::makeSelectRow(376.f, "Curva", nullptr,
                    {"Lineal", "Suave", "Rapida", "Rebote", "Elastica"},
                    current.easing, [self](int selected) {
                        auto* owner = self.get();
                        if (!owner) return;
                        auto value = owner->config();
                        value.easing = selected;
                        owner->changed(value, true);
                    }),
                slider("Entrada", &Config::enter, .05f, 1.5f, "s"),
                slider("Salida", &Config::exit, .05f, 1.5f, "s"),
                slider("Espera al entrar", &Config::delay, 0.f, 1.f, "s")
            }));
            rows.push_back(kit::makeCard(396.f, "Mientras esta encima", {220, 150, 255}, {
                kit::makeSelectRow(376.f, "Movimiento continuo", nullptr,
                    {"Ninguno", "Latido", "Flotar", "Balanceo", "Gelatina"},
                    current.loop, [self](int selected) {
                        auto* owner = self.get();
                        if (!owner) return;
                        auto value = owner->config();
                        value.loop = selected;
                        owner->changed(value, true);
                    }),
                slider("Intensidad", &Config::amplitude, 0.f, .25f),
                slider("Frecuencia", &Config::frequency, .2f, 5.f, " Hz")
            }));
            rows.push_back(kit::makeButtonRow(396.f, "Restaurar estilo",
                "Vuelve al hover suave por defecto.", "Restaurar", [self] {
                    auto* owner = self.get();
                    if (!owner) return;
                    owner->changed(Config{}, true);
                    owner->rebuildLater();
                }, paimon::ui::Btn::Gray));
            if (!key.empty() && !manager.linked && manager.hasOverride(key)) {
                rows.push_back(kit::makeButtonRow(396.f, "Heredar global",
                    "Elimina el ajuste individual de este boton.", "Heredar", [self] {
                        auto* owner = self.get();
                        if (!owner) return;
                        Manager::get().removeButton(owner->key);
                        owner->rebuildLater();
                    }, paimon::ui::Btn::Blue));
            }
        } else {
            rows.push_back(kit::makeButtonRow(396.f, "Guardar preset",
                "Hasta 100 presets. El mismo nombre actualiza el existente.", "Guardar", [self] {
                    auto* owner = self.get();
                    if (!owner) return;
                    auto snapshot = owner->config();
                    auto* popup = paimon::iconcopy::IconSetNamePopup::create(
                        "Guardar hover", "", [self, snapshot](std::string const& name) {
                            if (!Manager::get().savePreset(name, snapshot)) {
                                Notification::create("Limite de 100 presets",
                                    NotificationIcon::Warning)->show();
                                return;
                            }
                            if (auto* ownerRef = self.get()) ownerRef->rebuildLater();
                        });
                    if (popup) kit::showAbove(popup, owner);
                }));
            if (manager.saved.empty()) {
                rows.push_back(kit::makeHint(396.f,
                    "Ajusta un estilo y guardalo con tu propio nombre."));
            }
            for (auto const& [name, saved] : manager.saved) {
                rows.push_back(kit::makeCard(396.f, name.c_str(), {140, 240, 170}, {
                    kit::makeButtonRow(376.f, "Aplicar preset", nullptr, "Usar",
                        [self, saved] {
                            auto* owner = self.get();
                            if (!owner) return;
                            owner->changed(saved, true);
                            owner->rebuildLater();
                        }),
                    kit::makeButtonRow(376.f, "Eliminar preset", nullptr, "Borrar",
                        [self, name] {
                            createQuickPopup("Eliminar preset",
                                fmt::format("Eliminar <cy>{}</c>?", name),
                                "Cancelar", "Borrar",
                                [self, name](auto*, bool yes) {
                                    if (!yes) return;
                                    Manager::get().deletePreset(name);
                                    if (auto* owner = self.get()) owner->rebuildLater();
                                });
                        }, paimon::ui::Btn::Red)
                }));
            }
        }

        rows.push_back(kit::makeHint(396.f,
            "Vista automatica arriba. Respeta Movimiento reducido. No afecta controles de nivel ni editor."));
        scroll = kit::makeScrollStack({396.f, 196.f}, rows);
        scroll->setPosition({12.f, 14.f});
        m_mainLayer->addChild(scroll);
    }

    void buildPreviewStage() {
        auto* stage = paimon::ui::makeInset({396.f, 48.f}, 95);
        stage->setPosition({12.f, 215.f});
        m_mainLayer->addChild(stage);

        char const* scopeText = Manager::get().linked
            ? "Alcance: Todos vinculados"
            : (key.empty() ? "Alcance: Global" : "Alcance: Boton individual");
        auto* scopeLbl = paimon::ui::makeLabel(scopeText, 210.f, 0.36f, paimon::ui::palette::gold);
        scopeLbl->setAnchorPoint({0.f, 0.5f});
        scopeLbl->setPosition({10.f, 34.f});
        stage->addChild(scopeLbl);

        auto* statusLbl = paimon::ui::makeLabel("Vista previa", 210.f, 0.42f, paimon::ui::palette::muted);
        statusLbl->setAnchorPoint({0.f, 0.5f});
        statusLbl->setPosition({10.f, 15.f});
        stage->addChild(statusLbl);
        previewStatus = statusLbl;

        paimon::SafeNodeHandle<HoverPopup> self(this);
        auto* sprite = ButtonSprite::create("Hover", "bigFont.fnt", "GJ_button_01.png", .7f);
        auto* button = CCMenuItemExt::createSpriteExtra(sprite, [self](auto*) {
            if (auto* owner = self.get()) {
                owner->elapsed = 0.f;
            }
        });
        button->setScale(.52f);
        button->setPosition({322.f, 24.f});

        auto* stageMenu = paimon::ui::makeZeroMenu();
        stageMenu->addChild(button);
        stage->addChild(stageMenu);

        preview = sprite;
        previewButton = button;
    }

    bool init(std::string const& target) {
        if (!PaimonPopup::init(420.f, 300.f)) return false;
        key = target;
        setID("custom-hover-popup"_spr);
        paimon::markDynamicPopup(this);
        setTitle("Custom Hover");
        addCorners();
        addInfoButton("Custom Hover",
            "Anima los botones cuando pasas el cursor por encima (o deslizas el dedo en "
            "movil).\n\n"
            "<cy>Presets</c>: 24 estilos listos. <cj>Ajustes</c>: forma, movimiento, tiempo "
            "y animacion continua. <cg>Guardados</c>: tus propios estilos.\n\n"
            "La <co>vista previa</c> reproduce el ciclo completo. Respeta Movimiento "
            "reducido y no afecta los controles del nivel ni el lienzo del editor.");

        buildPreviewStage();
        rebuild();
        scheduleUpdate();
        setActivePopup(this);
        return true;
    }

    void update(float dt) override {
        float const safeDt = std::isfinite(dt) ? std::clamp(dt, 0.f, .05f) : 0.f;
        elapsed += safeDt;

        if (flushTimer >= 0.f) {
            flushTimer -= safeDt;
            if (flushTimer <= 0.f) {
                flushTimer = -1.f;
                Manager::get().saveIfDirty();
            }
        }

        auto* previewNode = preview.get();
        if (!previewNode) return;
        auto* parent = previewNode->getParent();
        if (!parent) return;

        auto const current = sanitize(config());
        float const hold = 1.3f;
        float const pause = 0.6f;
        float const total = std::max(0.1f,
            current.delay + current.enter + hold + current.exit + pause);
        float const cycle = std::fmod(elapsed, total);

        float amount = 0.f;
        char const* phase = "Reposo";
        if (!current.enabled) {
            phase = "Estilo desactivado";
        } else if (paimon::settings::smoothui::reducedMotion()) {
            phase = "Movimiento reducido";
        } else if (cycle < current.delay) {
            phase = "Espera";
        } else if (cycle < current.delay + current.enter) {
            amount = ease((cycle - current.delay) / current.enter, current.easing);
            phase = "Entrada";
        } else if (cycle < current.delay + current.enter + hold) {
            amount = 1.f;
            phase = "En hover";
        } else if (cycle < current.delay + current.enter + hold + current.exit) {
            amount = 1.f - ease((cycle - current.delay - current.enter - hold) / current.exit, 1);
            phase = "Salida";
        }

        if (auto* status = previewStatus.get()) {
            status->setString(fmt::format("Fase: {}", phase).c_str());
            status->limitLabelWidth(210.f, 0.42f, 0.15f);
        }

        auto const currentPose = pose(current, amount, elapsed);
        if (isFinitePose(currentPose)) {
            previewNode->setScaleX(std::max(.1f, currentPose.sx));
            previewNode->setScaleY(std::max(.1f, currentPose.sy));
            previewNode->setRotation(currentPose.rotation);
            auto const size = parent->getContentSize();
            previewNode->setPosition({
                size.width * 0.5f + currentPose.x,
                size.height * 0.5f + currentPose.y
            });
        }
    }

    void onExit() override {
        unscheduleUpdate();
        Manager::get().saveIfDirty();
        clearActivePopup(this);
        PaimonPopup::onExit();
    }

public:
    void groupAll() {
        Manager::get().group(config());
        rebuildLater();
        Notification::create("Hover: todos vinculados", NotificationIcon::Success)->show();
    }

    static HoverPopup* create(std::string const& target) {
        auto* popup = new HoverPopup();
        if (popup->init(target)) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

paimon::SafeNodeHandle<HoverPopup> active;

void setActivePopup(HoverPopup* popup) {
    active = popup;
}

void clearActivePopup(HoverPopup* popup) {
    if (active.get() == popup) {
        active = nullptr;
    }
}

} // namespace

bool popupOpen() {
    return active.get() != nullptr;
}

void groupFromPopup() {
    if (auto* owner = active.get()) {
        owner->groupAll();
    }
}

void open(std::string key) {
    if (!popupOpen()) {
        if (auto* popup = HoverPopup::create(key)) {
            popup->show();
        }
    }
}

} // namespace paimon::hover
