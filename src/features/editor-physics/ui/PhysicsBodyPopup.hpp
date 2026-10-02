#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "../services/PhysicsWorkspace.hpp"

#include <Geode/Geode.hpp>

#include <array>
#include <functional>
#include <string>

class ButtonSprite;

namespace paimon::editorphysics {

std::string bodyName(std::size_t index);

// per body and per object materials: what the lab sliders set for everyone, one
// captured body (and one of its objects) can override here.
class PhysicsBodyPopup : public PaimonPopup {
public:
    static PhysicsBodyPopup* create(
        std::size_t body,
        int object,
        std::function<void()> onChange
    );

private:
    bool init() override;
    void adjust(int field, int direction);
    void cycleBackend();
    void cycleNativePreset(int direction);
    void adjustNative(int field, int direction);
    void toggleNativePlayer(int player);
    void resetOverrides();
    void refreshValues();
    void refreshNativeValues();

    std::size_t m_body = 0;
    int m_object = -1;
    std::function<void()> m_onChange;
    std::array<cocos2d::CCLabelBMFont*, 10> m_valueLabels{};
    ButtonSprite* m_backendSprite = nullptr;
    ButtonSprite* m_presetSprite = nullptr;
    ButtonSprite* m_player1Sprite = nullptr;
    ButtonSprite* m_player2Sprite = nullptr;
    cocos2d::CCLabelBMFont* m_strengthLabel = nullptr;
    cocos2d::CCLabelBMFont* m_sensorLabel = nullptr;
};

} // namespace paimon::editorphysics
