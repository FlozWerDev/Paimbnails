#include "MainMenuDrawShapeNode.hpp"

#include <algorithm>
#include <cmath>

using namespace cocos2d;

namespace paimon::menu_layout {
namespace {
    float clampRadius(DrawShapeLayout const& layout) {
        return std::clamp(layout.cornerRadius, 0.f, std::min(layout.width, layout.height) * 0.5f);
    }

    ccColor4F color4f(ccColor3B const& color, float opacity) {
        return {
            color.r / 255.f,
            color.g / 255.f,
            color.b / 255.f,
            std::clamp(opacity, 0.f, 1.f),
        };
    }

    void drawRoundedRect(PaimonDrawNode* drawNode, float width, float height, float radius, ccColor4F fill) {
        drawNode->clear();
        width = std::max(width, 2.f);
        height = std::max(height, 2.f);
        radius = std::clamp(radius, 0.f, std::min(width, height) * 0.5f);

        if (radius <= 0.01f) {
            CCPoint points[4] = {
                { -width * 0.5f, -height * 0.5f },
                {  width * 0.5f, -height * 0.5f },
                {  width * 0.5f,  height * 0.5f },
                { -width * 0.5f,  height * 0.5f },
            };
            drawNode->drawPolygon(points, 4, fill, 0.f, { 0.f, 0.f, 0.f, 0.f });
            return;
        }

        constexpr int kSegments = 10;
        constexpr float kPi = 3.14159265358979323846f;
        std::vector<CCPoint> pts;
        pts.reserve((kSegments + 1) * 4);
        auto addArc = [&](float cx, float cy, float startAngle) {
            for (int i = 0; i <= kSegments; ++i) {
                auto angle = startAngle + (kPi * 0.5f) * (static_cast<float>(i) / static_cast<float>(kSegments));
                pts.emplace_back(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
            }
        };

        addArc(-width * 0.5f + radius, -height * 0.5f + radius, kPi);
        addArc( width * 0.5f - radius, -height * 0.5f + radius, kPi * 1.5f);
        addArc( width * 0.5f - radius,  height * 0.5f - radius, 0.f);
        addArc(-width * 0.5f + radius,  height * 0.5f - radius, kPi * 0.5f);
        drawNode->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), fill, 0.f, { 0.f, 0.f, 0.f, 0.f });
    }
}

MainMenuDrawShapeNode* MainMenuDrawShapeNode::create(DrawShapeLayout const& layout) {
    auto* node = new MainMenuDrawShapeNode();
    if (node && node->init()) {
        node->autorelease();
        node->applyLayout(layout);
        return node;
    }
    CC_SAFE_DELETE(node);
    return nullptr;
}

void MainMenuDrawShapeNode::applyLayout(DrawShapeLayout const& layout) {
    m_layout = layout;
    this->setPosition(layout.position);
    this->setScale(layout.scale);
    this->setScaleX(layout.scaleX);
    this->setScaleY(layout.scaleY);
    this->setVisible(!layout.hidden);
    this->setZOrder(layout.layer);
    this->setID(fmt::format("paimon-draw-shape-{}", layout.id));

    auto fill = color4f(layout.color, layout.opacity);
    if (layout.kind == DrawShapeKind::Circle) {
        auto radius = std::max(2.f, std::max(layout.width, layout.height) * 0.5f);
        this->clear();
        this->drawSolidCircle({ 0.f, 0.f }, radius, fill, 48);
        this->setContentSize({ radius * 2.f, radius * 2.f });
    } else {
        drawRoundedRect(this, layout.width, layout.height, layout.kind == DrawShapeKind::RoundedRect ? clampRadius(layout) : 0.f, fill);
        this->setContentSize({ layout.width, layout.height });
    }

    this->setOpacity(static_cast<GLubyte>(std::clamp(layout.opacity, 0.f, 1.f) * 255.f));
}

DrawShapeLayout MainMenuDrawShapeNode::readLayout() {
    auto out = m_layout;
    out.position = this->getPosition();
    out.scale = this->getScale();
    out.scaleX = this->getScaleX();
    out.scaleY = this->getScaleY();
    out.hidden = !this->isVisible();
    out.zOrder = this->getZOrder();
    out.layer = this->getZOrder();
    out.opacity = static_cast<float>(this->getOpacity()) / 255.f;
    return out;
}

} // namespace paimon::menu_layout
