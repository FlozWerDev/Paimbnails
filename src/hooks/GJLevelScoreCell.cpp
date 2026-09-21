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

using namespace geode::prelude;

static SimplePlayer* findSimplePlayerRec(CCNode* node, int depth = 0) {
    if (!node || depth > 6) return nullptr;
    for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
        if (!child) continue;
        if (auto* sp = typeinfo_cast<SimplePlayer*>(child)) return sp;
        if (auto* found = findSimplePlayerRec(child, depth + 1)) return found;
    }
    return nullptr;
}

static CCPoint getGLMousePos() {
    return geode::cocos::getMousePos();
}

struct LevelScoreCellHoverData {
    bool    wasHovered    = false;
    float   hoverLerp     = 0.f;
    float   hoverTime     = 0.f;

    Ref<CCNode>  cubeNode      = nullptr;
    float        cubeBaseScale = 1.f;

    // Movable children that aren't rank/bg
    struct Entry { CCNode* node; CCPoint base; };
    std::vector<Entry> movable;

    Ref<CCNode> gradient = nullptr;   // actual type: CCLayerGradient*
};

// Self-scheduled helper node for reliable updates
class PaimonLevelScoreCellHelper : public CCNode {
public:
    GJLevelScoreCell* m_cell = nullptr;
    LevelScoreCellHoverData m_data;
    int m_frameSkip = 0;

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

    void triggerShine() {
        if (!m_cell) return;
        CCSize cs = m_cell->getContentSize();
        if (cs.width <= 0.f || cs.height <= 0.f) return;

        if (auto* old = m_cell->getChildByID("paimon-lls-shine"_spr))
            old->removeFromParent();

        auto* shine = PaimonDrawNode::create();
        if (!shine) return;
        shine->setID("paimon-lls-shine"_spr);
        shine->setZOrder(50);

        // Subtle diagonal sheen, retriggered on every mouse-enter.
        constexpr float kW    = 28.f;
        constexpr float kSkew = 20.f;
        constexpr float kEdge = 12.f;

        ccColor4F bright = {1.f, 1.f, 1.f, 0.20f};
        ccColor4F faded  = {1.f, 1.f, 1.f, 0.f  };

        CCPoint center[4] = {
            ccp(kSkew,       cs.height),
            ccp(kSkew + kW,  cs.height),
            ccp(kW,          0.f),
            ccp(0.f,         0.f),
        };
        shine->drawPolygon(center, 4, bright, 0.f, bright);

        CCPoint lEdge[4] = {
            ccp(kSkew - kEdge, cs.height),
            ccp(kSkew,         cs.height),
            ccp(0.f,           0.f),
            ccp(-kEdge,        0.f),
        };
        shine->drawPolygon(lEdge, 4, faded, 0.f, bright);

        CCPoint rEdge[4] = {
            ccp(kSkew + kW,         cs.height),
            ccp(kSkew + kW + kEdge, cs.height),
            ccp(kW + kEdge,         0.f),
            ccp(kW,                 0.f),
        };
        shine->drawPolygon(rEdge, 4, bright, 0.f, faded);

        shine->setContentSize(cs);
        shine->setPosition({-(kW + kSkew + kEdge), 0.f});
        m_cell->addChild(shine);

        float travel = cs.width + kW + kSkew + kEdge * 2.f;
        auto move = CCEaseSineOut::create(CCMoveBy::create(0.50f, ccp(travel, 0.f)));
        auto fade = CCSequence::create(
            CCDelayTime::create(0.30f),
            CCFadeTo::create(0.20f, 0),
            nullptr);
        shine->runAction(CCSequence::create(
            CCSpawn::create(move, fade, nullptr),
            CCRemoveSelf::create(),
            nullptr
        ));
    }

    void update(float dt) override {
        CCNode::update(dt);

        if (!m_cell || !m_cell->getParent()) return;

        if (++m_frameSkip % 2 != 0) return;

        auto& d = m_data;

        // Hit-test the mouse against the cell
        bool isHovered = false;
        {
            CCPoint gl    = getGLMousePos();
            CCPoint local = m_cell->convertToNodeSpace(gl);
            CCSize  cs    = m_cell->getContentSize();
            isHovered = (local.x >= 0.f && local.x <= cs.width &&
                         local.y >= 0.f && local.y <= cs.height);
        }

        if (isHovered && !d.wasHovered) triggerShine();
        d.wasHovered = isHovered;

        float target = isHovered ? 1.f : 0.f;
        d.hoverLerp += (target - d.hoverLerp) * std::min(1.f, dt * 10.f);
        if (std::abs(d.hoverLerp - target) < 0.004f) d.hoverLerp = target;
        float lerp = d.hoverLerp;

        if (lerp > 0.004f) d.hoverTime += dt;
        else                d.hoverTime  = 0.f;

        if (d.gradient && d.gradient->getParent()) {
            if (auto* grad = typeinfo_cast<CCLayerGradient*>(d.gradient.data())) {
                GLubyte alpha = static_cast<GLubyte>(110.f + lerp * 70.f);
                grad->setStartOpacity(alpha);
                grad->setVector(ccp(1.f, -0.10f - 0.30f * lerp));
            }
        }

        for (auto& e : d.movable) {
            if (!e.node || !e.node->getParent()) continue;
            e.node->setPositionX(e.base.x + lerp * 6.f);
        }

        if (d.cubeNode && d.cubeNode->getParent()) {
            d.cubeNode->setScale(d.cubeBaseScale * (1.f + lerp * 0.07f));
            d.cubeNode->setRotation(std::sinf(d.hoverTime * 4.f) * 2.f * lerp);
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
                // find(), not starts_with(): "_spr" IDs expand to
                // "<mod-id>/paimon-...", so a prefix check never matches and
                // reused cells would pile up stale gradient nodes.
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
                auto stencil = paimon::SpriteHelper::createRoundedRectStencil(cs.width, cs.height, 7.f);
                if (auto clip = cocos2d::CCClippingNode::create(stencil)) {
                    clip->setContentSize(cs);
                    clip->setAnchorPoint({0.f, 0.f});
                    clip->setPosition({0.f, 0.f});
                    clip->setAlphaThreshold(0.05f);
                    clip->setZOrder(-1);
                    clip->setID("paimon-lls-gradient-clip"_spr);
                    clip->addChild(gradient);
                    paimon::scorecell::attachCellOverlays(clip, cs);
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

        // Diagonal tint so wide cells blend instead of hard-stepping; wrapped
        // in the same rounded clip as the full gradient.
        ccColor3B iconColor = {100, 150, 255};
        if (auto* gm = GameManager::get())
            iconColor = gm->colorForIdx(score->m_color1);
        {
            auto tuned = paimon::scorecell::designScoreGradient(iconColor, iconColor);
            iconColor = tuned.first;
        }

        auto* gradient = CCLayerGradient::create(
            ccc4(iconColor.r, iconColor.g, iconColor.b, 255),  // left: subtle base
            ccc4(iconColor.r, iconColor.g, iconColor.b, 0),    // right: transparent
            ccp(1.f, -0.35f)
        );
        gradient->setContentSize(cs);
        gradient->setAnchorPoint({0.f, 0.f});
        gradient->setPosition({0.f, 0.f});
        gradient->setStartOpacity(110);
        gradient->setID("paimon-lls-gradient"_spr);
        if (auto stencil = paimon::SpriteHelper::createRoundedRectStencil(cs.width, cs.height, 7.f)) {
            if (auto clip = cocos2d::CCClippingNode::create(stencil)) {
                clip->setContentSize(cs);
                clip->setAnchorPoint({0.f, 0.f});
                clip->setPosition({0.f, 0.f});
                clip->setAlphaThreshold(0.05f);
                clip->setZOrder(-1);
                clip->setID("paimon-lls-gradient-clip"_spr);
                clip->addChild(gradient);
                this->addChild(clip);
            } else {
                gradient->setZOrder(-1);
                this->addChild(gradient);
            }
        } else {
            gradient->setZOrder(-1);
            this->addChild(gradient);
        }

        // Create helper and fill hover data
        auto* helper = PaimonLevelScoreCellHelper::create(this);
        if (!helper) return;
        helper->setID("paimon-lls-helper"_spr);
        helper->setZOrder(-2);
        this->addChild(helper);
        f->helper = helper;

        auto& d = helper->m_data;
        d = LevelScoreCellHoverData{};
        d.gradient = gradient;

        if (auto* sp = findSimplePlayerRec(this)) {
            d.cubeNode      = sp;
            d.cubeBaseScale = sp->getScale();
        }

        // Collect movable children that aren't rank/background
        for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
            if (!child) continue;
            std::string_view id = child->getID();

            if (id.find("paimon-") != std::string_view::npos) continue;
            if (typeinfo_cast<CCLayerColor*>(child) != nullptr) continue;

            bool isRank = false;
            if (!id.empty() &&
                (id.find("rank")   != std::string_view::npos ||
                 id.find("trophy") != std::string_view::npos ||
                 id.find("medal")  != std::string_view::npos))
                isRank = true;
            if (!isRank && child->getPositionX() < 22.f) isRank = true;
            if (isRank) continue;

            d.movable.push_back({child, child->getPosition()});
        }
    }
};
