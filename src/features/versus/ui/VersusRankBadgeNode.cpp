#include "VersusRankBadgeNode.hpp"
#include "../../progression/data/ProgressionTiers.hpp"
#include "../../progression/ui/TierBadgeNode.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

void fitSquare(CCSprite* sprite, float size) {
    float const source = std::max(sprite->getContentSize().width, sprite->getContentSize().height);
    sprite->setScale(size / std::max(1.f, source));
}

} // namespace

VersusRankBadgeNode* VersusRankBadgeNode::create(RankInfo const& rank, float size) {
    auto ret = new VersusRankBadgeNode();
    if (ret && ret->init(rank, size)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VersusRankBadgeNode::init(RankInfo const& rank, float size) {
    if (!CCNode::init()) return false;

    m_rank = rank;
    m_size = size;
    this->setContentSize({size, size});
    this->setAnchorPoint({0.5f, 0.5f});

    m_content = CCNode::create();
    m_content->setPosition({size / 2.f, size / 2.f});
    this->addChild(m_content);

    rebuild();
    return true;
}

void VersusRankBadgeNode::setRank(RankInfo const& rank) {
    m_rank = rank;
    rebuild();
}

void VersusRankBadgeNode::setShowPips(bool show) {
    if (m_showPips == show) return;
    m_showPips = show;
    rebuild();
}

void VersusRankBadgeNode::setDim(bool dim) {
    if (m_dim == dim) return;
    m_dim = dim;
    rebuild();
}

void VersusRankBadgeNode::rebuild() {
    m_content->removeAllChildren();

    auto const& tier = progression::tierAt(m_rank.tierIndex);
    bool const placing = m_rank.placing() || m_dim;

    if (auto* frame = paimon::SpriteHelper::safeCreate("paim_vsFrame.png"_spr)) {
        fitSquare(frame, m_size);
        // the laurel stays gold at every tier so the badge reads as "vs" first
        // and as a rank second; grey while the placements are still running.
        frame->setColor(placing ? ccColor3B{130, 136, 152} : ccColor3B{250, 200, 60});
        m_content->addChild(frame, 0);
    }

    if (auto* plate = progression::makeTierPlate(tier.frame)) {
        fitSquare(plate, m_size * 0.56f);
        plate->setColor(placing ? ccColor3B{110, 116, 132} : tier.base);
        m_content->addChild(plate, 1);
    }

    if (auto* swords = paimon::SpriteHelper::safeCreate("paim_vsSwords.png"_spr)) {
        fitSquare(swords, m_size * 0.30f);
        swords->setColor(placing ? ccColor3B{170, 176, 190} : tier.accent);
        swords->setPositionY(m_size * 0.02f);
        m_content->addChild(swords, 2);
    }

    if (m_rank.placing()) {
        auto* label = CCLabelBMFont::create(
            fmt::format("{}", m_rank.placementsLeft).c_str(), "goldFont.fnt");
        label->setScale(m_size * 0.010f);
        label->setPositionY(-m_size * 0.42f);
        m_content->addChild(label, 3);
        return;
    }

    if (!m_showPips || !m_rank.hasDivision()) return;

    float const pipSize = m_size * 0.13f;
    float const gap = pipSize * 1.25f;
    int const filled = 5 - m_rank.division;   // division i lights all four
    for (int i = 0; i < 4; i++) {
        auto* pip = paimon::SpriteHelper::safeCreate("paim_vsPip.png"_spr);
        if (!pip) continue;
        fitSquare(pip, pipSize);
        pip->setColor(i < filled ? tier.accent : ccColor3B{70, 74, 88});
        pip->setPosition({(i - 1.5f) * gap, -m_size * 0.46f});
        m_content->addChild(pip, 3);
    }
}

void VersusRankBadgeNode::playPromotion() {
    m_content->stopAllActions();
    m_content->setScale(0.4f);
    m_content->runAction(CCEaseElasticOut::create(CCScaleTo::create(0.7f, 1.f), 0.6f));

    if (auto* glow = progression::makeRadialGlow(
            progression::tierAt(m_rank.tierIndex).accent, m_size * 1.2f, 0.85f)) {
        m_content->addChild(glow, -1);
        glow->runAction(CCSequence::create(
            CCFadeTo::create(0.9f, 0),
            CCRemoveSelf::create(),
            nullptr));
    }
}

} // namespace paimon::versus
