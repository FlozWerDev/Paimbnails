#include "MainMenuDrawShapeNode.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace cocos2d;

namespace paimon::menu_layout {
namespace {
    constexpr float kPi = 3.14159265358979323846f;

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

    void drawRingQuads(PaimonDrawNode* node, std::vector<CCPoint> const& inner,
                       std::vector<CCPoint> const& outer, ccColor4F border) {
        size_t const n = std::min(inner.size(), outer.size());
        for (size_t i = 0; i < n; ++i) {
            size_t const j = (i + 1) % n;
            CCPoint quad[4] = { inner[i], outer[i], outer[j], inner[j] };
            node->drawPolygon(quad, 4, border, 0.f, { 0.f, 0.f, 0.f, 0.f });
        }
    }

    void drawRoundedRect(PaimonDrawNode* drawNode, float width, float height, float radius,
                         ccColor4F fill, float strokeWidth, ccColor4F strokeColor) {
        drawNode->clear();
        width = std::max(width, 2.f);
        height = std::max(height, 2.f);
        radius = std::clamp(radius, 0.f, std::min(width, height) * 0.5f);

        bool const hasStroke = strokeWidth > 0.05f && strokeColor.a > 0.001f;

        if (radius <= 0.01f) {
            CCPoint points[4] = {
                { -width * 0.5f, -height * 0.5f },
                {  width * 0.5f, -height * 0.5f },
                {  width * 0.5f,  height * 0.5f },
                { -width * 0.5f,  height * 0.5f },
            };
            drawNode->drawPolygon(points, 4, fill, 0.f, { 0.f, 0.f, 0.f, 0.f });
            if (hasStroke) {
                float const hw = strokeWidth * 0.5f;
                std::vector<CCPoint> inner = {
                    { -width * 0.5f + hw, -height * 0.5f + hw },
                    {  width * 0.5f - hw, -height * 0.5f + hw },
                    {  width * 0.5f - hw,  height * 0.5f - hw },
                    { -width * 0.5f + hw,  height * 0.5f - hw },
                };
                std::vector<CCPoint> outer = {
                    { -width * 0.5f - hw, -height * 0.5f - hw },
                    {  width * 0.5f + hw, -height * 0.5f - hw },
                    {  width * 0.5f + hw,  height * 0.5f + hw },
                    { -width * 0.5f - hw,  height * 0.5f + hw },
                };
                drawRingQuads(drawNode, inner, outer, strokeColor);
            }
            return;
        }

        constexpr int kSegments = 10;
        std::vector<CCPoint> pts;
        pts.reserve(kSegments * 4);
        auto pushArcs = [&](std::vector<CCPoint>& dst, float r) {
            auto addArc = [&](float cx, float cy, float startAngle) {
                for (int i = 0; i < kSegments; ++i) {
                    auto angle = startAngle + (kPi * 0.5f) * (static_cast<float>(i) / static_cast<float>(kSegments));
                    dst.emplace_back(cx + std::cos(angle) * r, cy + std::sin(angle) * r);
                }
            };
            addArc(-width * 0.5f + radius, -height * 0.5f + radius, kPi);
            addArc( width * 0.5f - radius, -height * 0.5f + radius, kPi * 1.5f);
            addArc( width * 0.5f - radius,  height * 0.5f - radius, 0.f);
            addArc(-width * 0.5f + radius,  height * 0.5f - radius, kPi * 0.5f);
        };

        pushArcs(pts, radius);
        drawNode->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), fill, 0.f, { 0.f, 0.f, 0.f, 0.f });

        if (hasStroke) {
            float const hw = strokeWidth * 0.5f;
            std::vector<CCPoint> inner;
            std::vector<CCPoint> outer;
            inner.reserve(kSegments * 4);
            outer.reserve(kSegments * 4);
            pushArcs(inner, std::max(0.f, radius - hw));
            pushArcs(outer, radius + hw);
            drawRingQuads(drawNode, inner, outer, strokeColor);
        }
    }

    void drawCircleShape(PaimonDrawNode* drawNode, float radius, ccColor4F fill,
                         float strokeWidth, ccColor4F strokeColor) {
        drawNode->clear();
        drawNode->drawSolidCircle({ 0.f, 0.f }, radius, fill, 48);

        bool const hasStroke = strokeWidth > 0.05f && strokeColor.a > 0.001f;
        if (hasStroke) {
            constexpr int kSegments = 48;
            float const hw = strokeWidth * 0.5f;
            float const rIn = std::max(0.f, radius - hw);
            float const rOut = radius + hw;
            std::vector<CCPoint> inner;
            std::vector<CCPoint> outer;
            inner.reserve(kSegments);
            outer.reserve(kSegments);
            for (int i = 0; i < kSegments; ++i) {
                float a = 2.f * kPi * static_cast<float>(i) / static_cast<float>(kSegments);
                float c = std::cos(a), s = std::sin(a);
                inner.emplace_back(c * rIn, s * rIn);
                outer.emplace_back(c * rOut, s * rOut);
            }
            drawRingQuads(drawNode, inner, outer, strokeColor);
        }
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
    this->setRotation(layout.rotation);
    this->setVisible(!layout.hidden);
    this->setZOrder(layout.layer);
    this->setID(fmt::format("{}{}", "paimon-draw-shape-"_spr, layout.id));

    auto fill = color4f(layout.color, layout.opacity);
    auto stroke = color4f(layout.strokeColor, layout.opacity);
    if (layout.kind == DrawShapeKind::Circle) {
        auto radius = std::max(2.f, std::max(layout.width, layout.height) * 0.5f);
        drawCircleShape(this, radius, fill, layout.strokeWidth, stroke);
        this->setContentSize({ radius * 2.f, radius * 2.f });
    } else {
        drawRoundedRect(
            this,
            layout.width,
            layout.height,
            layout.kind == DrawShapeKind::RoundedRect ? clampRadius(layout) : 0.f,
            fill,
            layout.strokeWidth,
            stroke
        );
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
    out.rotation = this->getRotation();
    out.hidden = !this->isVisible();
    out.zOrder = this->getZOrder();
    out.layer = this->getZOrder();
    out.opacity = static_cast<float>(this->getOpacity()) / 255.f;
    return out;
}

} // namespace paimon::menu_layout
