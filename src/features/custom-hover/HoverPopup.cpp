#include "../../ui/PaimonPopup.hpp"
#include "CustomHover.hpp"
#include "../../ui/PaiConfigKit.hpp"
#include "../../ui/PaimonUI.hpp"
#include "../../utils/DynamicPopupRegistry.hpp"
#include "../../core/modules/ModuleRegistry.hpp"
#include "../../core/Settings.hpp"
#include "../icon-copy/ui/IconSetNamePopup.hpp"

using namespace geode::prelude;

namespace paimon::hover {
namespace {

namespace kit = paimon::configkit;

class HoverPopup;
HoverPopup* active = nullptr;

class HoverPopup : public PaimonPopup {
    std::string key;
    int tab = 0;
    ScrollLayer* scroll = nullptr;
    CCNode* preview = nullptr;
    float elapsed = 0;
    bool pending = false;

    Config config() const {
        return Manager::get().resolve(key);
    }

    void changed(Config value) {
        Manager::get().set(key, value);
        elapsed = 0;
    }

    void rebuildLater() {
        if (pending) return;
        pending = true;
        Ref<HoverPopup> self = this;
        Loader::get()->queueInMainThread([self] {
            self->pending = false;
            if (self->getParent()) self->rebuild();
        });
    }

    CCNode* slider(char const* name, float Config::*field, float lo, float hi,
                   char const* unit = "") {
        return kit::makeSliderRow(356, name, nullptr, config().*field, lo, hi,
            [unit](double value) { return fmt::format("{:.2f}{}", value, unit); },
            [this, field](double value) {
                auto current = config();
                current.*field = static_cast<float>(value);
                changed(current);
            });
    }

    void rebuild() {
        if (scroll) {
            scroll->removeFromParent();
            scroll = nullptr;
        }

        auto& manager = Manager::get();
        auto current = config();
        std::vector<CCNode*> rows;
        rows.push_back(kit::makeHint(376, key.empty()
            ? "Configuracion global. Ctrl+clic: editar un boton."
            : "Boton seleccionado. Los cambios se ven al instante."));
        rows.push_back(kit::makeToggleRow(376, "Custom Hover",
            "Cursor en PC; deslizar el dedo en movil.",
            paimon::modules::isEnabled("paimbnails.customhover.global"),
            [](bool enabled) {
                paimon::modules::setEnabled("paimbnails.customhover.global", enabled);
            }));
        rows.push_back(kit::makeTabBar(376, {"Presets", "Ajustes", "Guardados"}, tab,
            [this](int selected) {
                tab = selected;
                rebuildLater();
            }));

        if (tab == 0) {
            rows.push_back(kit::makeCard(376, "Aplicar a", {120, 210, 255}, {
                kit::makeToggleRow(356, "Todos vinculados",
                    "Ctrl+D: usa este estilo en todos. Desactiva para recuperar los individuales.",
                    manager.linked, [this](bool enabled) {
                        auto& manager = Manager::get();
                        if (enabled) {
                            manager.group(config());
                        } else {
                            manager.linked = false;
                            manager.save();
                        }
                        rebuildLater();
                    }),
                kit::makeButtonRow(356, "Elegir boton",
                    "Cierra esta ventana y toca el boton que quieras editar.", "Elegir", [this] {
                        Manager::get().picking = true;
                        onClose(nullptr);
                        Notification::create("Toca un boton para editar su hover",
                            NotificationIcon::Info)->show();
                    }),
                kit::makeButtonRow(356, "Cancelar seleccion",
                    "Desactiva el modo Elegir boton.", "Cancelar", [] {
                        Manager::get().picking = false;
                    })
            }));

            std::vector<CCNode*> choices;
            choices.reserve(presets().size());
            for (auto const& preset : presets()) {
                choices.push_back(kit::makeButtonRow(356, preset.name.c_str(), nullptr,
                    "Usar", [this, value = preset.config] {
                        changed(value);
                        rebuildLater();
                    }));
            }
            rows.push_back(kit::makeCard(376, "24 estilos listos para usar",
                {255, 210, 100}, choices));
        } else if (tab == 1) {
            rows.push_back(kit::makeCard(376, "Forma y movimiento", {120, 210, 255}, {
                kit::makeToggleRow(356, "Animar este estilo", nullptr, current.enabled,
                    [this](bool enabled) {
                        auto value = config();
                        value.enabled = enabled;
                        changed(value);
                    }),
                slider("Escala", &Config::scale, .5f, 1.8f, "x"),
                slider("Ancho / alto", &Config::stretch, .6f, 1.5f, "x"),
                slider("Desplazamiento vertical", &Config::lift, -25, 25),
                slider("Desplazamiento horizontal", &Config::slide, -25, 25),
                slider("Rotacion", &Config::rotation, -180, 180, " deg")
            }));
            rows.push_back(kit::makeCard(376, "Tiempo", {255, 200, 100}, {
                kit::makeSelectRow(356, "Curva", nullptr,
                    {"Lineal", "Suave", "Rapida", "Rebote", "Elastica"},
                    current.easing, [this](int selected) {
                        auto value = config();
                        value.easing = selected;
                        changed(value);
                    }),
                slider("Entrada", &Config::enter, .05f, 1.5f, "s"),
                slider("Salida", &Config::exit, .05f, 1.5f, "s"),
                slider("Espera al entrar", &Config::delay, 0, 1, "s")
            }));
            rows.push_back(kit::makeCard(376, "Mientras esta encima", {220, 150, 255}, {
                kit::makeSelectRow(356, "Movimiento continuo", nullptr,
                    {"Ninguno", "Latido", "Flotar", "Balanceo", "Gelatina"},
                    current.loop, [this](int selected) {
                        auto value = config();
                        value.loop = selected;
                        changed(value);
                    }),
                slider("Intensidad", &Config::amplitude, 0, .25f),
                slider("Frecuencia", &Config::frequency, .2f, 5, " Hz")
            }));
            rows.push_back(kit::makeButtonRow(376, "Restaurar estilo",
                "Vuelve al hover suave.", "Restaurar", [this] {
                    changed(Config{});
                    rebuildLater();
                }));
            if (!key.empty() && !manager.linked) {
                rows.push_back(kit::makeButtonRow(376, "Heredar global",
                    "Elimina el ajuste de este boton.", "Heredar", [this] {
                        Manager::get().buttons.erase(key);
                        Manager::get().save();
                        rebuildLater();
                    }));
            }
        } else {
            rows.push_back(kit::makeButtonRow(376, "Guardar preset",
                "Hasta 100 presets. El mismo nombre actualiza el existente.", "Guardar", [this] {
                    WeakRef<HoverPopup> self = this;
                    auto snapshot = config();
                    auto* popup = paimon::iconcopy::IconSetNamePopup::create(
                        "Guardar hover", "", [self, snapshot](std::string const& name) {
                            auto& manager = Manager::get();
                            if (manager.saved.size() >= 100 && !manager.saved.count(name)) {
                                Notification::create("Limite de 100 presets",
                                    NotificationIcon::Warning)->show();
                                return;
                            }
                            manager.saved[name] = snapshot;
                            manager.save();
                            if (auto owner = self.lock()) owner->rebuildLater();
                        });
                    if (popup) kit::showAbove(popup, this);
                }));
            if (manager.saved.empty()) {
                rows.push_back(kit::makeHint(376,
                    "Ajusta un estilo y guardalo con tu propio nombre."));
            }
            for (auto const& [name, saved] : manager.saved) {
                rows.push_back(kit::makeCard(376, name.c_str(), {140, 240, 170}, {
                    kit::makeButtonRow(356, "Aplicar preset", nullptr, "Usar",
                        [this, saved] {
                            changed(saved);
                            rebuildLater();
                        }),
                    kit::makeButtonRow(356, "Eliminar preset", nullptr, "Borrar",
                        [this, name] {
                            createQuickPopup("Eliminar preset",
                                fmt::format("Eliminar <cy>{}</c>?", name),
                                "Cancelar", "Borrar",
                                [self = WeakRef<HoverPopup>(this), name](auto*, bool yes) {
                                    if (!yes) return;
                                    Manager::get().saved.erase(name);
                                    Manager::get().save();
                                    if (auto owner = self.lock()) owner->rebuildLater();
                                });
                        })
                }));
            }
        }

        rows.push_back(kit::makeHint(376,
            "Vista automatica arriba. Respeta Movimiento reducido. No afecta los controles del nivel ni el lienzo del editor."));
        scroll = kit::makeScrollStack({376, 205}, rows);
        scroll->setPosition({12, 20});
        m_mainLayer->addChild(scroll);
    }

    bool init(std::string const& target) {
        if (!PaimonPopup::init(400, 290)) return false;
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
            "La <co>vista previa</c> de la derecha se mueve sola. Respeta Movimiento "
            "reducido y no afecta a los controles del nivel ni al lienzo del editor.");

        auto* sprite = ButtonSprite::create("Hover", "bigFont.fnt", "GJ_button_01.png", .7f);
        auto* button = CCMenuItemExt::createSpriteExtra(sprite, [this](auto*) {
            elapsed = 0;
        });
        button->setScale(.55f);
        button->setPosition({325, 250});
        m_buttonMenu->addChild(button);
        preview = sprite;

        auto* caption = paimon::ui::makeLabel("Vista previa", 90.f, 0.45f, paimon::ui::palette::muted);
        caption->setPosition({325, 228});
        m_mainLayer->addChild(caption);

        rebuild();
        scheduleUpdate();
        active = this;
        return true;
    }

    void update(float dt) override {
        elapsed += std::clamp(dt, 0.f, .05f);
        auto current = config();
        float cycle = std::fmod(elapsed,
            current.delay + current.enter + 1.3f + current.exit + .6f);
        float amount = 0;
        if (cycle > current.delay && cycle < current.delay + current.enter) {
            amount = ease((cycle - current.delay) / current.enter, current.easing);
        } else if (cycle >= current.delay + current.enter &&
                   cycle < current.delay + current.enter + 1.3f) {
            amount = 1;
        } else if (cycle >= current.delay + current.enter + 1.3f) {
            amount = 1 - ease((cycle - current.delay - current.enter - 1.3f) /
                current.exit, 1);
        }
        if (!current.enabled || paimon::settings::smoothui::reducedMotion()) amount = 0;

        auto currentPose = pose(current, amount, elapsed);
        preview->setScaleX(std::max(.1f, currentPose.sx));
        preview->setScaleY(std::max(.1f, currentPose.sy));
        preview->setRotation(currentPose.rotation);
        auto size = preview->getParent()->getContentSize();
        preview->setPosition({size.width / 2 + currentPose.x,
            size.height / 2 + currentPose.y});
    }

    void onExit() override {
        if (active == this) active = nullptr;
        Popup::onExit();
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

} // namespace

bool popupOpen() {
    return active != nullptr;
}

void groupFromPopup() {
    if (active) active->groupAll();
}

void open(std::string key) {
    if (!active) {
        if (auto* popup = HoverPopup::create(key)) popup->show();
    }
}

} // namespace paimon::hover
