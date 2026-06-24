#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <algorithm>
#include <cmath>
#include <new>

namespace paimon {

// CCClippingNode that clips axis-aligned rectangles via GL scissor instead of
// the stencil buffer: scissor is a single GL state, draws no mask geometry, and
// doesn't break batching (a win in lists with many thumbnails).
//
// Only correct for axis-aligned RECTANGULAR clips. If the node's world transform
// has rotation/skew (b/c != 0) or the rect is degenerate, it falls back to the
// stencil path (CCClippingNode::visit), so it's a safe drop-in. The stencil
// passed to create() is kept for the fallback but isn't drawn on the fast path.
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

    // stencil-less variant (for the create() + later setStencil() pattern)
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

        // no valid size or no GL view: use the classic clipping
        if (!view || size.width <= 0.f || size.height <= 0.f) {
            cocos2d::CCClippingNode::visit();
            return;
        }

        // These clippers' stencil is always the [0,0]-(w,h) rect in node space.
        // If the world transform has no rotation/skew, an axis-aligned scissor
        // matches it exactly.
        auto t = this->nodeToWorldTransform();
        if (std::fabs(t.b) > 1e-3f || std::fabs(t.c) > 1e-3f) {
            cocos2d::CCClippingNode::visit(); // rotated/skewed -> stencil
            return;
        }

        // node rect corners in world space (cocos points)
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
            // intersect with an ancestor's scissor (list, popup) to stay within its area
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

        // render children without stencil; the scissor does the clipping
        cocos2d::CCNode::visit();

        // restore the previous scissor state
        if (prevEnabled) {
            view->setScissorInPoints(prev.origin.x, prev.origin.y, prev.size.width, prev.size.height);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
    }
};

} // namespace paimon
