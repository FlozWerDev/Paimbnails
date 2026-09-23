#include "PreviewNode.hpp"

#include "../../../utils/SpriteHelper.hpp"
#include "../services/SaveString.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace geode::prelude;

namespace paimon::autobuild {

namespace {

ccColor4F colorFor(ObjectKind kind, bool behind) {
    switch (kind) {
        case ObjectKind::Hazard:      return {1.f, 0.34f, 0.34f, 1.f};
        case ObjectKind::Solid:
        case ObjectKind::Slope:       return {0.86f, 0.89f, 0.96f, 1.f};
        case ObjectKind::Portal:      return {0.42f, 0.86f, 1.f, 1.f};
        case ObjectKind::Pad:
        case ObjectKind::Orb:         return {1.f, 0.85f, 0.35f, 1.f};
        case ObjectKind::Collectible: return {1.f, 0.72f, 0.95f, 1.f};
        case ObjectKind::Trigger:     return {0.68f, 0.55f, 1.f, 1.f};
        case ObjectKind::Text:        return {0.75f, 0.8f, 0.85f, 1.f};
        default: break;
    }
    return behind ? ccColor4F{0.32f, 0.42f, 0.62f, 1.f} : ccColor4F{0.47f, 0.85f, 0.6f, 1.f};
}

int zLayerOf(std::string const& save) {
    std::string value;
    if (!objectKey(save, 24, value)) return kZLayerDefault;
    return utils::numFromString<int>(value).unwrapOr(kZLayerDefault);
}

bool isBehind(int zLayer) {
    return zLayer == kZLayerB1 || zLayer == kZLayerB2 || zLayer == kZLayerB3 ||
           zLayer == kZLayerB4;
}

} // namespace

PreviewNode* PreviewNode::create(CCSize size) {
    auto* ret = new PreviewNode();
    if (ret && ret->init(size)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool PreviewNode::init(CCSize size) {
    if (!CCNode::init()) return false;
    this->setContentSize(size);
    this->setAnchorPoint({0.f, 0.f});

    if (auto* panel = SpriteHelper::createColorPanel(size.width, size.height,
                                                     {8, 11, 20}, 210, 5.f)) {
        panel->setAnchorPoint({0.f, 0.f});
        this->addChild(panel, -1);
    }

    m_draw = CCDrawNode::create();
    this->addChild(m_draw);

    m_empty = CCLabelBMFont::create("sin objetos", "chatFont.fnt");
    m_empty->setScale(0.36f);
    m_empty->setColor({120, 128, 148});
    m_empty->setPosition({size.width / 2.f, size.height / 2.f});
    m_empty->setVisible(false);
    this->addChild(m_empty);
    return true;
}

void PreviewNode::draw(std::vector<Dot> const& dots) {
    if (!m_draw) return;
    m_draw->clear();
    if (dots.empty()) {
        if (m_empty) m_empty->setVisible(true);
        return;
    }
    if (m_empty) m_empty->setVisible(false);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (auto const& dot : dots) {
        minX = std::min(minX, dot.x);
        minY = std::min(minY, dot.y);
        maxX = std::max(maxX, dot.x);
        maxY = std::max(maxY, dot.y);
    }

    auto const size = this->getContentSize();
    float const padding = 6.f;
    float const spanX = std::max(30.f, maxX - minX);
    float const spanY = std::max(30.f, maxY - minY);
    float const scale = std::min((size.width - padding * 2.f) / spanX,
                                 (size.height - padding * 2.f) / spanY);
    float const offsetX = (size.width - spanX * scale) / 2.f - minX * scale;
    float const offsetY = (size.height - spanY * scale) / 2.f - minY * scale;

    // Cells never drop below a pixel or a wide backdrop turns into nothing.
    for (auto const& dot : dots) {
        float const half = std::max(1.2f, dot.size * scale / 2.f);
        float const cx = dot.x * scale + offsetX;
        float const cy = dot.y * scale + offsetY;
        CCPoint corners[4] = {{cx - half, cy - half}, {cx + half, cy - half},
                              {cx + half, cy + half}, {cx - half, cy + half}};
        auto const color = colorFor(dot.kind, dot.behind);
        m_draw->drawPolygon(corners, 4, color, 0.f, color);
    }
}

void PreviewNode::showRegion(LevelData const& data, Region const& region) {
    std::vector<Dot> dots;
    dots.reserve(region.objects.size());
    for (int index : region.objects) {
        auto const& object = data.objects[index];
        auto kind = kindOf(object.id);
        if (kind == ObjectKind::Unknown && looksLikeTrigger(object)) kind = ObjectKind::Trigger;
        dots.push_back({object.x, object.y,
                        30.f * std::max(0.4f, (std::abs(object.scaleX) +
                                               std::abs(object.scaleY)) / 2.f),
                        kind, isBehind(object.zLayer)});
    }
    draw(dots);
}

void PreviewNode::showPiece(Piece const& piece) {
    std::vector<Dot> dots;
    dots.reserve(piece.objects.size());
    for (auto const& object : piece.objects) {
        dots.push_back({object.dx, object.dy, 30.f, kindOf(object.objectId),
                        isBehind(zLayerOf(object.save))});
    }
    draw(dots);
}

void PreviewNode::showTemplate(Template const& tpl) {
    std::vector<Dot> dots;
    auto collect = [&](Piece const& piece, float originX, float originY) {
        for (auto const& object : piece.objects) {
            dots.push_back({originX + object.dx, originY + object.dy, 30.f,
                            kindOf(object.objectId), isBehind(zLayerOf(object.save))});
        }
    };

    if (tpl.mode == Mode::Wave && !tpl.grids.empty()) {
        auto const& grid = tpl.grids.front();
        for (auto const& cell : grid.cells) {
            if (cell.piece < 0 || cell.piece >= static_cast<int>(tpl.pieces.size())) continue;
            collect(tpl.pieces[cell.piece], cell.x * tpl.cell, cell.y * tpl.cell);
        }
    } else {
        // Stamps have no layout of their own, so the pieces go in a row.
        float originX = 0.f;
        for (auto const& piece : tpl.pieces) {
            collect(piece, originX + piece.width / 2.f, 0.f);
            originX += piece.width + 30.f;
        }
    }
    draw(dots);
}

} // namespace paimon::autobuild
