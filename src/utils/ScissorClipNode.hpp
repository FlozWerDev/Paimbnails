#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <algorithm>
#include <cmath>
#include <new>

namespace paimon {

// CCClippingNode que recorta rectangulos axis-aligned usando GL scissor en vez
// del stencil buffer.
//
// Motivacion: cada CCClippingNode con stencil rompe el batching de quads y
// agrega pasadas extra de stencil cada frame. En listas con muchas miniaturas
// (LevelCell) eso se multiplica por celda visible y baja los FPS. El scissor
// es un unico estado de GL, no dibuja geometria de mascara ni rompe el batch,
// y esta disponible incluso en GLES2.
//
// Solo es correcto para recortes RECTANGULARES axis-aligned. Si el nodo tiene
// rotacion o skew en su transform al mundo (componentes b/c != 0) o el rect es
// degenerado, cae automaticamente al comportamiento stencil original
// (CCClippingNode::visit), asi que es un reemplazo directo y seguro.
//
// El stencil que se pasa a create() se mantiene (para el fallback), pero en el
// camino rapido no se dibuja. IMPORTANTE: solo usar cuando el stencil sea el
// rect completo del contentSize (createRectStencil(w,h) sin setSkewX/rotacion);
// si el stencil es redondeado o sesgado, el scissor NO lo replica.
class ScissorClipNode : public cocos2d::CCClippingNode {
public:
    static ScissorClipNode* create(cocos2d::CCNode* stencil) {
        auto ret = new (std::nothrow) ScissorClipNode();
        if (ret && ret->init(stencil)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    // Variante sin stencil (para el patron create() + setStencil() posterior).
    static ScissorClipNode* create() {
        auto ret = new (std::nothrow) ScissorClipNode();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void visit() override {
        if (!this->isVisible()) return;

        auto size = this->getContentSize();
        auto* director = cocos2d::CCDirector::get();
        auto* view = director ? director->getOpenGLView() : nullptr;

        // Sin tamano valido o sin vista GL: usar el clipping clasico.
        if (!view || size.width <= 0.f || size.height <= 0.f) {
            cocos2d::CCClippingNode::visit();
            return;
        }

        // El stencil de estos clippers es siempre el rect [0,0]-(w,h) en
        // espacio del nodo. Si el transform al mundo no tiene rotacion/skew
        // podemos representar ese rect exactamente con un scissor axis-aligned.
        auto t = this->nodeToWorldTransform();
        if (std::fabs(t.b) > 1e-3f || std::fabs(t.c) > 1e-3f) {
            cocos2d::CCClippingNode::visit(); // rotado/sesgado -> stencil
            return;
        }

        // Esquinas del rect del nodo en espacio del mundo (puntos cocos).
        float x0 = t.tx;
        float y0 = t.ty;
        float x1 = t.a * size.width + t.tx;
        float y1 = t.d * size.height + t.ty;
        cocos2d::CCRect rect(
            std::min(x0, x1), std::min(y0, y1),
            std::fabs(x1 - x0), std::fabs(y1 - y0));

        bool prevEnabled = view->isScissorEnabled();
        cocos2d::CCRect prev;
        if (prevEnabled) {
            // Intersectar con el scissor de un ancestro (lista, popup) para no
            // dibujar fuera de su area.
            prev = view->getScissorRect();
            float nx = std::max(rect.getMinX(), prev.getMinX());
            float ny = std::max(rect.getMinY(), prev.getMinY());
            float xx = std::min(rect.getMaxX(), prev.getMaxX());
            float yy = std::min(rect.getMaxY(), prev.getMaxY());
            rect = cocos2d::CCRect(nx, ny, std::max(0.f, xx - nx), std::max(0.f, yy - ny));
        } else {
            glEnable(GL_SCISSOR_TEST);
        }

        view->setScissorInPoints(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);

        // Render de hijos sin stencil; el scissor hace el recorte.
        cocos2d::CCNode::visit();

        // Restaurar estado previo de scissor.
        if (prevEnabled) {
            view->setScissorInPoints(prev.origin.x, prev.origin.y, prev.size.width, prev.size.height);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
    }
};

} // namespace paimon
