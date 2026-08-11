#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <string>

namespace paimon::quickhub {

// QuickHubRadial — Menu radial circular que aparece al mantener Ctrl 1.5s.
// La seleccion es por sector angular: basta apuntar hacia una opcion, no hace
// falta acertar dentro de su icono. El centro cancela.

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

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void keyBackClicked() override;

private:
    struct RadialItem {
        std::string id;
        std::string name;
        std::string hint;                      // aviso cuando no se puede pulsar aqui
        cocos2d::ccColor3B color{255, 255, 255};
        cocos2d::CCNode* node = nullptr;       // contenedor: posicion + open/close
        cocos2d::CCNode* inner = nullptr;      // hijo: escala de hover
        cocos2d::CCNode* ring = nullptr;       // aro de acento: solo al apuntar
        cocos2d::CCPoint position;
        float angle = 0.f;
        bool reachable = true;
    };

    std::vector<RadialItem> m_items;
    cocos2d::CCNode* m_hub = nullptr;
    cocos2d::CCLabelBMFont* m_nameLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    cocos2d::CCPoint m_center;
    float m_radius = 96.f;
    float m_badgeSize = 46.f;
    float m_deadZone = 40.f;  // radio del centro: dentro no hay seleccion
    int m_hoveredIndex = -1;

    void buildBackdrop();
    void buildRadialItems();
    void animateOpen();
    void animateClose();
    int getHoveredIndex(cocos2d::CCPoint const& worldPos);
    void updateHover(int index);
    void executeOption(int index);

    static QuickHubRadial* s_instance;
};

} // namespace paimon::quickhub
