#include <Geode/modify/GJLevelScoreCell.hpp>
#include "../framework/HookConventions.hpp"
#include <Geode/binding/GJLevelScoreCell.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/utils/cocos.hpp>
#include "../utils/SpriteHelper.hpp"
#include "../utils/PaimonDrawNode.hpp"

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

        // Subtle diagonal parallelogram
        constexpr float kW    = 18.f;
        constexpr float kSkew = 14.f;
        constexpr float kEdge = 9.f;

        ccColor4F bright = {0.30f, 0.30f, 0.30f, 0.30f};
        ccColor4F faded  = {0.f,   0.f,   0.f,   0.f  };

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
        shine->runAction(CCSequence::create(
            CCEaseSineOut::create(CCMoveBy::create(0.40f, ccp(travel, 0.f))),
            CCRemoveSelf::create(),
            nullptr
        ));
    }

    // Per-frame hover logic
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

        // Hover timer for rotation
        if (lerp > 0.004f) d.hoverTime += dt;
        else                d.hoverTime  = 0.f;

        if (d.gradient && d.gradient->getParent()) {
            if (auto* grad = typeinfo_cast<CCLayerGradient*>(d.gradient.data())) {
                GLubyte alpha = static_cast<GLubyte>(60.f + lerp * 170.f);
                grad->setStartOpacity(alpha);
            }
        }

        // Move non-rank content +15px
        for (auto& e : d.movable) {
            if (!e.node || !e.node->getParent()) continue;
            e.node->setPositionX(e.base.x + lerp * 15.f);
        }

        if (d.cubeNode && d.cubeNode->getParent()) {
            d.cubeNode->setScale(d.cubeBaseScale * (1.f + lerp * 0.15f));
            d.cubeNode->setRotation(std::sinf(d.hoverTime * 5.f) * 5.f * lerp);
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

        // Remove previous paimon nodes
        {
            std::vector<CCNode*> rem;
            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (!child) continue;
                std::string_view cid = child->getID();
                if (cid.starts_with("paimon-")) rem.push_back(child);
            }
            for (auto* n : rem) n->removeFromParent();
        }
        f->helper = nullptr;

        CCSize cs = this->getContentSize();
        if (cs.width  <= 1.f) cs.width  = this->m_width;
        if (cs.height <= 1.f) cs.height = this->m_height;
        if (cs.width <= 0.f || cs.height <= 0.f) return;

        // Hide vanilla backgrounds
        for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
            if (!child) continue;
            std::string_view cid = child->getID();
            if (cid.starts_with("paimon-")) continue;
            if (typeinfo_cast<CCLayerColor*>(child) != nullptr)
                child->setVisible(false);
        }

        // Color-to-transparent gradient
        ccColor3B iconColor = {100, 150, 255};
        if (auto* gm = GameManager::get())
            iconColor = gm->colorForIdx(score->m_color1);

        auto* gradient = CCLayerGradient::create(
            ccc4(iconColor.r, iconColor.g, iconColor.b, 255),  // left: subtle base
            ccc4(iconColor.r, iconColor.g, iconColor.b, 0),    // right: transparent
            ccp(1.f, 0.f)
        );
        gradient->setContentSize(cs);
        gradient->setAnchorPoint({0.f, 0.f});
        gradient->setPosition({0.f, 0.f});
        gradient->setZOrder(-1);
        gradient->setID("paimon-lls-gradient"_spr);
        this->addChild(gradient);

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

            if (id.starts_with("paimon-")) continue;
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