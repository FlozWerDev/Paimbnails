#include "SegmentedBar.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

#include <algorithm>
#include <memory>

using namespace geode::prelude;

namespace paimon::icon_gallery::ui {

namespace {

constexpr float kGap = 12.f;
// Holgura extra: el boton se dibuja mas estrecho que su hueco para que los
// bordes blancos de dos vecinos no se toquen. La separacion visible entre dos
// botones es kGap + kShrink.
constexpr float kShrink = 12.f;
constexpr float kFaceHeight = 26.f;
// Al estrechar el boton hay que bajar tambien la letra, o "Instalados" y
// "Recientes" se comen el borde.
constexpr float kFontScale = 0.42f;

// Colores reales de los sprites del juego: 01 es verde (122,222,45) y 02 es
// celeste (36,229,228). Ojo, el 04 NO es celeste sino gris (154,154,154).
constexpr char const* kFaceOff = "GJ_button_01.png";  // verde
constexpr char const* kFaceOn = "GJ_button_02.png";   // celeste

// Las dos caras de cada segmento se crean de una vez y solo se alterna cual
// se ve: repintar la barra desde el callback del propio boton destruiria el
// menu que el dispatcher todavia esta recorriendo.
struct BarState {
    std::vector<CCNode*> onFaces;
    std::vector<CCNode*> offFaces;
    int selected = 0;

    void restyle() const {
        for (std::size_t i = 0; i < onFaces.size(); ++i) {
            bool const sel = static_cast<int>(i) == selected;
            if (onFaces[i]) onFaces[i]->setVisible(sel);
            if (i < offFaces.size() && offFaces[i]) offFaces[i]->setVisible(!sel);
        }
    }
};

}  // anonymous namespace

CCNode* makeSegmentedBar(
    float width,
    std::vector<std::string> const& labels,
    int selected,
    std::function<void(int)> onSelect
) {
    float const barH = kSegmentedBarHeight;

    auto* bar = CCNode::create();
    bar->setAnchorPoint({0.f, 0.f});
    bar->setContentSize({width, barH});

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(
        CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    bar->addChild(menu, 5);

    int const n = std::max<int>(1, static_cast<int>(labels.size()));
    // segW es el hueco reservado a cada opcion; btnW es lo que se dibuja y
    // tambien lo que ocupa la zona pulsable, para que dos vecinos no compartan
    // borde ni hitbox.
    float const segW = (width - kGap * static_cast<float>(n - 1)) / static_cast<float>(n);
    float const btnW = std::max(24.f, segW - kShrink);

    auto state = std::make_shared<BarState>();
    state->selected = std::clamp(selected, 0, n - 1);
    auto cb = std::make_shared<std::function<void(int)>>(std::move(onSelect));

    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        auto const& text = labels[static_cast<std::size_t>(i)];

        auto* holder = CCNode::create();
        holder->setAnchorPoint({0.5f, 0.5f});
        holder->setContentSize({btnW, barH});

        auto addFace = [&](char const* texture) -> CCNode* {
            // Ancho absoluto para que los segmentos queden parejos aunque los
            // textos midan distinto.
            auto* spr = ButtonSprite::create(
                text.c_str(), static_cast<int>(btnW), true,
                "bigFont.fnt", texture, kFaceHeight, kFontScale);
            if (!spr) return nullptr;
            spr->setPosition({btnW / 2.f, barH / 2.f});
            holder->addChild(spr);
            return spr;
        };

        state->onFaces.push_back(addFace(kFaceOn));
        state->offFaces.push_back(addFace(kFaceOff));

        auto* btn = CCMenuItemExt::createSpriteExtra(holder,
            [state, cb, i](CCMenuItemSpriteExtra*) {
                if (state->selected == i) return;
                state->selected = i;
                state->restyle();
                if (*cb) (*cb)(i);
            });
        btn->setPosition({segW / 2.f + static_cast<float>(i) * (segW + kGap),
                          barH / 2.f});
        menu->addChild(btn);
    }

    state->restyle();
    return bar;
}

}  // namespace paimon::icon_gallery::ui
