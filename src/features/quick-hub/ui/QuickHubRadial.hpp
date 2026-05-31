#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <string>

namespace paimon::quickhub {

// ─────────────────────────────────────────────────────────────────────────────
// QuickHubRadial — Menu radial circular que aparece al mantener Ctrl 1.5s.
// Muestra las opciones configuradas en un circulo con hover glow y click.
// ─────────────────────────────────────────────────────────────────────────────

class QuickHubRadial : public cocos2d::CCLayer {
public:
    static QuickHubRadial* create();
    static void openRadial();
    static void closeRadial();
    static bool isOpen();

protected:
    bool init() override;
    void onExit() override;
    void update(float dt) override;

    // Touch
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void keyBackClicked() override;

private:
    struct RadialItem {
        std::string id;
        cocos2d::CCNode* node = nullptr;       // contenedor: maneja posicion + open/close
        cocos2d::CCNode* inner = nullptr;      // hijo: maneja la escala de hover (no conflicta con open/close)
        cocos2d::CCNode* glowNode = nullptr;
        cocos2d::CCPoint position;
        float angle = 0.f;
    };

    std::vector<RadialItem> m_items;
    cocos2d::CCNode* m_centerLabel = nullptr;
    cocos2d::CCSprite* m_blurBg = nullptr;
    cocos2d::CCLayerColor* m_darkOverlay = nullptr;
    int m_hoveredIndex = -1;

    void buildRadialItems();
    void animateOpen();
    void animateClose();
    int getHoveredIndex(cocos2d::CCPoint const& worldPos);
    void updateHover(int index);
    void executeOption(int index);

    static QuickHubRadial* s_instance;
};

} // namespace paimon::quickhub
