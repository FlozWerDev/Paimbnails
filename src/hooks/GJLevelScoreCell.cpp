#include <Geode/modify/GJLevelScoreCell.hpp>
#include "../framework/HookConventions.hpp"
#include <Geode/binding/GJLevelScoreCell.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/utils/cocos.hpp>
#include "../utils/SpriteHelper.hpp"
#include "../utils/PaimonDrawNode.hpp"
#include "../core/modules/ModuleRegistry.hpp"
#include "../features/scorecell/ScoreCellSettings.hpp"
#include "../features/scorecell/fx/ScoreGradientDesign.hpp"
#include "../features/scorecell/fx/ScoreGradientLayer.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace {

constexpr float kCellRadius = 7.f;
// x (cell space) below which a node belongs to the rank column
constexpr float kRankColumnMaxX = 38.f;

SimplePlayer* findSimplePlayerRec(CCNode* node, int depth = 0) {
    if (!node || depth > 6) return nullptr;
    for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
        if (!child) continue;
        if (auto* sp = typeinfo_cast<SimplePlayer*>(child)) return sp;
        if (auto* found = findSimplePlayerRec(child, depth + 1)) return found;
    }
    return nullptr;
}

float smoothstep01(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

ccColor3B mixToWhite(ccColor3B c, float t) {
    auto mix = [t](GLubyte v) {
        return static_cast<GLubyte>(static_cast<float>(v) + (255.f - static_cast<float>(v)) * t);
    };
    return {mix(c.r), mix(c.g), mix(c.b)};
}

bool isAncestorOf(CCNode* maybeAncestor, CCNode* node) {
    for (auto* p = node ? node->getParent() : nullptr; p; p = p->getParent()) {
        if (p == maybeAncestor) return true;
    }
    return false;
}

// thin ring along the cell edge, inset so the rounded clip never cuts it.
// the node uses straight alpha blending, so colors stay non-premultiplied.
void drawRoundedRing(PaimonDrawNode* node, CCSize const& size, float radius,
                     float thickness, ccColor4F const& color) {
    node->clear();
    if (color.a <= 0.003f || size.width <= radius * 2.f || size.height <= radius * 2.f) return;

    constexpr int kSegments = 8;
    constexpr float kHalfPi = 1.57079632679f;
    float const inset = thickness * 0.5f + 0.4f;
    float const mid = radius - inset;
    float const outerR = mid + thickness * 0.5f;
    float const innerR = std::max(mid - thickness * 0.5f, 0.f);

    CCPoint const centers[4] = {
        {radius, radius},
        {size.width - radius, radius},
        {size.width - radius, size.height - radius},
        {radius, size.height - radius},
    };
    float const starts[4] = {kHalfPi * 2.f, kHalfPi * 3.f, 0.f, kHalfPi};

    std::vector<CCPoint> outer;
    std::vector<CCPoint> inner;
    outer.reserve(4 * (kSegments + 1));
    inner.reserve(4 * (kSegments + 1));
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i <= kSegments; ++i) {
            float const a = starts[c] + kHalfPi * (static_cast<float>(i) / kSegments);
            float const cx = std::cos(a);
            float const cy = std::sin(a);
            outer.push_back(ccp(centers[c].x + cx * outerR, centers[c].y + cy * outerR));
            inner.push_back(ccp(centers[c].x + cx * innerR, centers[c].y + cy * innerR));
        }
    }

    auto const none = ccc4f(0.f, 0.f, 0.f, 0.f);
    size_t const n = outer.size();
    for (size_t i = 0; i < n; ++i) {
        size_t const j = (i + 1) % n;
        CCPoint quad[4] = {inner[i], outer[i], outer[j], inner[j]};
        node->drawPolygon(quad, 4, color, 0.f, none);
    }
}

// pointer inside the cell and inside every clipping ancestor (list viewport)
bool isPointerOver(CCNode* cell) {
#if !defined(GEODE_IS_DESKTOP)
    (void)cell;
    return false;
#else
    CCPoint const pointer = geode::cocos::getMousePos();
    auto const size = cell->getContentSize();
    if (!CCRect(0.f, 0.f, size.width, size.height).containsPoint(cell->convertToNodeSpace(pointer))) {
        return false;
    }
    for (auto* node = cell; node; node = node->getParent()) {
        if (!node->isVisible()) return false;
        if (node == cell || !typeinfo_cast<CCLayer*>(node)) continue;
        auto const bounds = node->getContentSize();
        if (bounds.width <= 0.f || bounds.height <= 0.f) continue;
        if (!CCRect(0.f, 0.f, bounds.width, bounds.height).containsPoint(node->convertToNodeSpace(pointer))) {
            return false;
        }
    }
    return true;
#endif
}

} // namespace

struct LevelScoreCellHoverData {
    float hoverLerp = 0.f;   // eased 0..1 hover amount
    float pop       = 0.f;   // spring value for icon and rank pop
    float popVel    = 0.f;
    int   ringAlpha = -1;    // last drawn ring alpha, avoids redraws
    CCSize size;

    struct Entry {
        Ref<CCNode> node;
        CCPoint base;
        float baseScale = 1.f;
    };
    Entry icon;
    std::vector<Entry> text;   // name and percent slide right
    std::vector<Entry> rank;   // trophy or position number pops

    Ref<CCNode> gradient = nullptr;   // actual type: cclayergradient*
    Ref<CCLayerGradient> highlight = nullptr;
    Ref<CCLayerColor> accent = nullptr;
    Ref<PaimonDrawNode> ring = nullptr;
};

// self-scheduled for reliable updates
class PaimonLevelScoreCellHelper : public CCNode {
public:
    GJLevelScoreCell* m_cell = nullptr;
    LevelScoreCellHoverData m_data;

    static PaimonLevelScoreCellHelper* create(GJLevelScoreCell* cell) {
        auto* n = new PaimonLevelScoreCellHelper();
        if (n && n->init()) {
            n->m_cell = cell;
            n->autorelease();
            n->scheduleUpdate();
            return n;
        }
        CC_SAFE_DELETE(n);
        return nullptr;
    }

    void update(float dt) override {
        CCNode::update(dt);
        if (!m_cell || !m_cell->getParent()) return;
        dt = std::clamp(dt, 0.f, 0.05f);

        auto& d = m_data;
        float const target = isPointerOver(m_cell) ? 1.f : 0.f;
        if (d.hoverLerp == target && d.pop == target && d.popVel == 0.f) return;

        // fast ease in, slightly slower release
        float const rate = target > 0.5f ? 16.f : 10.f;
        d.hoverLerp += (target - d.hoverLerp) * (1.f - std::exp(-rate * dt));
        if (std::abs(d.hoverLerp - target) < 0.002f) d.hoverLerp = target;

        // under-damped spring, about 10% overshoot, sub-stepped for low fps
        constexpr float kStiffness = 320.f;
        constexpr float kDamping = 21.f;
        constexpr float kStep = 1.f / 240.f;
        for (float left = dt; left > 0.f; left -= kStep) {
            float const h = std::min(left, kStep);
            d.popVel += ((target - d.pop) * kStiffness - d.popVel * kDamping) * h;
            d.pop += d.popVel * h;
        }
        if (std::abs(d.pop - target) < 0.001f && std::abs(d.popVel) < 0.01f) {
            d.pop = target;
            d.popVel = 0.f;
        }

        apply();
    }

    void apply() {
        auto& d = m_data;
        float const e = smoothstep01(d.hoverLerp);

        if (auto* grad = typeinfo_cast<CCLayerGradient*>(d.gradient.data())) {
            grad->setStartOpacity(static_cast<GLubyte>(110.f + 65.f * e));
            grad->setVector(ccp(1.f, -0.10f - 0.30f * e));
        }

        if (d.highlight) {
            d.highlight->setStartOpacity(static_cast<GLubyte>(42.f * e));
            d.highlight->setEndOpacity(static_cast<GLubyte>(6.f * e));
        }

        if (d.accent) {
            d.accent->setVisible(e > 0.002f);
            d.accent->setScaleY(e);
            d.accent->setOpacity(static_cast<GLubyte>(255.f * std::min(1.f, e * 1.5f)));
        }

        if (d.ring) {
            int const alpha = static_cast<int>(std::lround(125.f * e));
            if (alpha != d.ringAlpha) {
                d.ringAlpha = alpha;
                drawRoundedRing(d.ring, d.size, kCellRadius, 1.f,
                    ccc4f(1.f, 1.f, 1.f, static_cast<float>(alpha) / 255.f));
            }
        }

        for (auto& t : d.text) {
            if (t.node) t.node->setPositionX(t.base.x + 5.f * e);
        }
        if (d.icon.node) {
            d.icon.node->setScale(d.icon.baseScale * (1.f + 0.12f * d.pop));
        }
        for (auto& r : d.rank) {
            if (r.node) r.node->setScale(r.baseScale * (1.f + 0.08f * d.pop));
        }
    }
};

class $modify(PaimonGJLevelScoreCell, GJLevelScoreCell) {

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "GJLevelScoreCell::loadFromScore");
    }

    struct Fields {
        PaimonLevelScoreCellHelper* helper = nullptr;
    };

    void triggerClickFlash() {
        if (!paimon::modules::isEnabled("paimbnails.leaderboardcells.browser")) return;

        CCSize cs = this->getContentSize();
        if (cs.width  <= 1.f) cs.width  = this->m_width;
        if (cs.height <= 1.f) cs.height = this->m_height;
        if (cs.width <= 0.f || cs.height <= 0.f) return;

        if (auto* old = this->getChildByID("paimon-click-flash"_spr))
            old->removeFromParent();

        auto* flash = CCLayerColor::create(ccc4(255, 255, 255, 180), cs.width, cs.height);
        flash->setPosition({0.f, 0.f});
        flash->setZOrder(200);
        flash->setID("paimon-click-flash"_spr);
        this->addChild(flash);

        flash->runAction(CCSequence::create(
            CCFadeTo::create(0.25f, 0),
            CCRemoveSelf::create(),
            nullptr
        ));
    }

    $override
    void onViewProfile(CCObject* sender) {
        triggerClickFlash();
        GJLevelScoreCell::onViewProfile(sender);
    }

    // sorts the cell's labels and sprites into rank column and sliding text
    void collectHoverNodes(CCNode* root, CCNode* icon, CCSize const& cs,
                           LevelScoreCellHoverData& d, int depth = 0) {
        if (!root || depth > 2) return;
        for (auto* child : CCArrayExt<CCNode*>(root->getChildren())) {
            if (!child || child == icon) continue;
            if (std::string_view(child->getID()).find("paimon-") != std::string_view::npos) continue;
            if (typeinfo_cast<CCLayerColor*>(child)) continue;

            bool const isItem = typeinfo_cast<CCMenuItem*>(child) != nullptr;
            bool const isLeaf = isItem ||
                typeinfo_cast<CCLabelBMFont*>(child) != nullptr ||
                typeinfo_cast<CCSprite*>(child) != nullptr;
            if (!isLeaf) {
                collectHoverNodes(child, icon, cs, d, depth + 1);
                continue;
            }
            if (isAncestorOf(child, icon)) continue;

            CCPoint const inCell = this->convertToNodeSpace(
                root->convertToWorldSpace(child->getPosition()));
            if (inCell.x < kRankColumnMaxX) {
                if (!isItem) d.rank.push_back({child, child->getPosition(), child->getScale()});
            } else if (inCell.x < cs.width * 0.72f) {
                d.text.push_back({child, child->getPosition(), child->getScale()});
            }
        }
    }

    $override
    void loadFromScore(GJUserScore* score) {
        GJLevelScoreCell::loadFromScore(score);
        if (!score) return;

        auto f = m_fields.self();
        if (!f) return;

        {
            std::vector<CCNode*> rem;
            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (!child) continue;
                std::string_view cid = child->getID();
                // "_spr" expands to "<mod-id>/paimon-..."; a prefix check never
                // matches and reused cells would pile up stale gradients
                if (cid.find("paimon-") != std::string_view::npos) rem.push_back(child);
            }
            for (auto* n : rem) n->removeFromParent();
        }
        f->helper = nullptr;

        bool scoreGradient = paimon::scorecell::scoreGradientEnabled();
        if (!scoreGradient && !paimon::modules::isEnabled("paimbnails.leaderboardcells.browser")) return;

        CCSize cs = this->getContentSize();
        if (cs.width  <= 1.f) cs.width  = this->m_width;
        if (cs.height <= 1.f) cs.height = this->m_height;
        if (cs.width <= 0.f || cs.height <= 0.f) return;

        for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
            if (!child) continue;
            std::string_view cid = child->getID();
            if (cid.find("paimon-") != std::string_view::npos) continue;
            if (typeinfo_cast<CCLayerColor*>(child) != nullptr)
                child->setVisible(false);
        }

        if (scoreGradient) {
            auto* gm = GameManager::sharedState();
            if (auto* gradient = paimon::scorecell::ScoreGradientLayer::create(
                    cs, gm->colorForIdx(score->m_color1), gm->colorForIdx(score->m_color2))) {
                gradient->setAnchorPoint({0.f, 0.f});
                gradient->setPosition({0.f, 0.f});
                gradient->setBaseOpacity(static_cast<GLubyte>(paimon::scorecell::gradientOpacity()));
                gradient->setIdleSpeed(paimon::scorecell::gradientSpeed());
                if (auto* clip = paimon::scorecell::makeScoreGradientClip(gradient, cs)) {
                    clip->setZOrder(-1);
                    clip->setID("paimon-lls-gradient-clip"_spr);
                    this->addChild(clip);
                    gradient->setID("paimon-lls-gradient"_spr);
                } else {
                    gradient->setZOrder(-1);
                    gradient->setID("paimon-lls-gradient"_spr);
                    this->addChild(gradient);
                }
            }
            return;
        }

        // diagonal tint so wide cells blend instead of hard-stepping
        ccColor3B playerColor = {100, 150, 255};
        if (auto* gm = GameManager::get())
            playerColor = gm->colorForIdx(score->m_color1);
        ccColor3B iconColor = paimon::scorecell::designScoreGradient(playerColor, playerColor).first;

        auto* gradient = CCLayerGradient::create(
            ccc4(iconColor.r, iconColor.g, iconColor.b, 255),
            ccc4(iconColor.r, iconColor.g, iconColor.b, 0),
            ccp(1.f, -0.35f)
        );
        gradient->setContentSize(cs);
        gradient->setAnchorPoint({0.f, 0.f});
        gradient->setPosition({0.f, 0.f});
        gradient->setStartOpacity(110);
        gradient->setID("paimon-lls-gradient"_spr);

        CCClippingNode* clip = nullptr;
        if (auto stencil = paimon::SpriteHelper::createRoundedRectStencil(cs.width, cs.height, kCellRadius)) {
            clip = CCClippingNode::create(stencil);
        }
        if (clip) {
            clip->setContentSize(cs);
            clip->setAnchorPoint({0.f, 0.f});
            clip->setPosition({0.f, 0.f});
            clip->setAlphaThreshold(0.05f);
            clip->setZOrder(-1);
            clip->setID("paimon-lls-gradient-clip"_spr);
            clip->addChild(gradient, 0);
            this->addChild(clip);
        } else {
            gradient->setZOrder(-1);
            this->addChild(gradient);
        }

        auto* helper = PaimonLevelScoreCellHelper::create(this);
        if (!helper) return;
        helper->setID("paimon-lls-helper"_spr);
        helper->setZOrder(-2);
        this->addChild(helper);
        f->helper = helper;

        auto& d = helper->m_data;
        d = LevelScoreCellHoverData{};
        d.gradient = gradient;
        d.size = cs;

        // hover layers live in the rounded clip so they follow the cell corners
        CCNode* fxParent = clip ? static_cast<CCNode*>(clip) : static_cast<CCNode*>(this);
        auto fxZ = [clip](int z) { return clip ? z : -1; };

        if (auto* light = CCLayerGradient::create(
                ccc4(255, 255, 255, 0), ccc4(255, 255, 255, 0), ccp(0.f, -1.f))) {
            light->setContentSize(cs);
            light->setPosition({0.f, 0.f});
            light->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            light->setID("paimon-lls-hover-light"_spr);
            fxParent->addChild(light, fxZ(1));
            d.highlight = light;
        }

        ccColor3B const accentColor = mixToWhite(playerColor, 0.35f);
        if (auto* bar = CCLayerColor::create(ccc4(accentColor.r, accentColor.g, accentColor.b, 255), 3.f, cs.height)) {
            bar->ignoreAnchorPointForPosition(false);
            bar->setAnchorPoint({0.f, 0.5f});
            bar->setPosition({0.f, cs.height * 0.5f});
            bar->setScaleY(0.f);
            bar->setOpacity(0);
            bar->setVisible(false);
            bar->setID("paimon-lls-hover-accent"_spr);
            fxParent->addChild(bar, fxZ(2));
            d.accent = bar;
        }

        if (auto* ring = PaimonDrawNode::create()) {
            ring->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
            ring->setContentSize(cs);
            ring->setPosition({0.f, 0.f});
            ring->setID("paimon-lls-hover-ring"_spr);
            fxParent->addChild(ring, fxZ(3));
            d.ring = ring;
        }

        CCNode* content = this->m_mainLayer ? static_cast<CCNode*>(this->m_mainLayer) : static_cast<CCNode*>(this);
        SimplePlayer* icon = findSimplePlayerRec(content);
        if (icon) d.icon = {icon, icon->getPosition(), icon->getScale()};
        collectHoverNodes(content, icon, cs, d);
    }
};
