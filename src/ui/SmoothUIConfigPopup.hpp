#pragma once
#include <Geode/Geode.hpp>
#include <string>

namespace paimon::ui {

// Aplica un preset global de Smooth UI (popups, botones, scroll, blur y
// transiciones de golpe). Ids: balanced, subtle, silky, bouncy, cinematic, off.
void applySmoothUIPreset(std::string const& preset);

// Popup dedicado de Smooth UI montado sobre PaiConfigKit: preset rapido
// arriba y tarjetas por area (popups, botones, scroll, blur/transiciones).
class SmoothUIConfigPopup : public geode::Popup {
public:
    static SmoothUIConfigPopup* create();

protected:
    bool init() override;

    void rebuild();
    void scheduleRebuild();

    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::ui
