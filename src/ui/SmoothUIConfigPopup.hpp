#pragma once
#include <Geode/Geode.hpp>
#include <string>

namespace paimon::ui {

void applySmoothUIPreset(std::string const& preset);
void setGlobalTransitionDuration(float duration);

class SmoothUIConfigPopup : public geode::Popup {
public:
    static SmoothUIConfigPopup* create();

protected:
    bool init() override;

    void rebuild();
    void scheduleRebuild();

    geode::ScrollLayer* m_scroll = nullptr;
    int m_tab = 0; // 0 basic, 1 advanced
};

} // namespace paimon::ui
