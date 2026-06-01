#pragma once

// Helpers reutilizables extraidos del namespace anonimo de
// hooks/LevelSearchLayer.cpp para sacar logica del hook.

#include <cocos2d.h>
#include <Geode/binding/LevelSearchLayer.hpp>
#include <algorithm>

namespace paimon::levelsearch {

// Libera todo el estado de foco del input de busqueda para que ningun
// listener de IME/teclado quede vivo tras cambiar de escena (si no, el text
// input sigue tragando teclas en gameplay: ESC, flechas, letras, etc).
void releaseSearchInputFocus(LevelSearchLayer* layer);

// CCMenu que ignora toques fuera del rect (en coordenadas de mundo) de un
// nodo de bounds externo. Usado por las filas de busqueda en tiempo real:
// CCClippingNode solo recorta el RENDER, no el input, asi que una fila
// scrolleada fuera del area visible seguiria capturando toques sin esto.
class BoundedTouchMenu : public cocos2d::CCMenu {
public:
    static BoundedTouchMenu* create() {
        auto ret = new BoundedTouchMenu();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init() override {
        if (!cocos2d::CCMenu::init()) return false;
        return true;
    }

    // El menu no retiene el nodo: el caller debe mantenerlo vivo en la
    // jerarquia normal de nodos.
    void setBoundsNode(cocos2d::CCNode* bounds) { m_boundsNode = bounds; }

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {
        if (!isTouchInsideBounds(touch)) {
            return false;
        }
        return cocos2d::CCMenu::ccTouchBegan(touch, event);
    }

private:
    cocos2d::CCNode* m_boundsNode = nullptr;

    bool isTouchInsideBounds(cocos2d::CCTouch* touch) const {
        if (!m_boundsNode || !touch) return true;

        auto location = touch->getLocation();
        auto size = m_boundsNode->getContentSize();
        cocos2d::CCRect localRect{0.f, 0.f, size.width, size.height};
        auto worldOrigin = m_boundsNode->convertToWorldSpace({localRect.origin.x, localRect.origin.y});
        auto worldOpposite = m_boundsNode->convertToWorldSpace({localRect.getMaxX(), localRect.getMaxY()});
        float minX = std::min(worldOrigin.x, worldOpposite.x);
        float minY = std::min(worldOrigin.y, worldOpposite.y);
        float maxX = std::max(worldOrigin.x, worldOpposite.x);
        float maxY = std::max(worldOrigin.y, worldOpposite.y);
        return location.x >= minX && location.x <= maxX &&
               location.y >= minY && location.y <= maxY;
    }
};

} // namespace paimon::levelsearch
