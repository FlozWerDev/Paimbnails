#include "ProfileNameDecorator.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::profile_name {

namespace {
constexpr float kTwoPi = 6.283185307179586f;

ccColor3B lerpColor(ccColor3B a, ccColor3B b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {
        static_cast<GLubyte>(a.r + (b.r - a.r) * t),
        static_cast<GLubyte>(a.g + (b.g - a.g) * t),
        static_cast<GLubyte>(a.b + (b.b - a.b) * t),
    };
}

ccColor3B hsv(float h) {
    h = h - std::floor(h);
    float r = std::fabs(h * 6.f - 3.f) - 1.f;
    float g = 2.f - std::fabs(h * 6.f - 2.f);
    float b = 2.f - std::fabs(h * 6.f - 4.f);
    auto cl = [](float v) { return static_cast<GLubyte>(std::clamp(v, 0.f, 1.f) * 255.f); };
    return {cl(r), cl(g), cl(b)};
}

// three-stop sample across [0,1].
ccColor3B sampleGradient(PicNameConfig const& cfg, float t) {
    if (cfg.gradientMode >= 2) {
        if (t < 0.5f) return lerpColor(cfg.gradA, cfg.gradB, t * 2.f);
        return lerpColor(cfg.gradB, cfg.gradC, (t - 0.5f) * 2.f);
    }
    return lerpColor(cfg.gradA, cfg.gradB, t);
}

bool needsAnimator(PicNameConfig const& cfg) {
    if (cfg.gradientMode > 0 && cfg.gradAnim != "static") return true;
    if (cfg.letterAnim != "none" && !cfg.letterAnim.empty()) return true;
    return false;
}

class NameAnimator : public CCNode {
public:
    PicNameConfig m_cfg;
    CCLabelBMFont* m_label = nullptr;
    float m_time = 0.f;
    std::vector<CCPoint> m_basePos;

    static NameAnimator* create(CCLabelBMFont* label, PicNameConfig const& cfg) {
        auto* n = new NameAnimator();
        if (n && n->init()) {
            n->m_label = label;
            n->m_cfg = cfg;
            n->captureBase();
            n->scheduleUpdate();
            n->autorelease();
            return n;
        }
        CC_SAFE_DELETE(n);
        return nullptr;
    }

    void captureBase() {
        m_basePos.clear();
        if (auto* kids = m_label->getChildren()) {
            for (int i = 0; i < static_cast<int>(kids->count()); i++) {
                auto* s = static_cast<CCNode*>(kids->objectAtIndex(i));
                m_basePos.push_back(s->getPosition());
            }
        }
    }

    void update(float dt) override {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!m_label || !m_label->getParent()) return;
        // string changes rebuild letter sprites; resync base offsets.
        auto* kids = m_label->getChildren();
        int count = kids ? static_cast<int>(kids->count()) : 0;
        if (count != static_cast<int>(m_basePos.size())) captureBase();
        if (count == 0) return;

        m_time += dt * std::max(m_cfg.gradSpeed, 0.05f);
        float ltime = m_time * std::max(m_cfg.letterSpeed, 0.05f);

        for (int i = 0; i < count; i++) {
            auto* letter = static_cast<CCSprite*>(kids->objectAtIndex(i));
            float frac = count > 1 ? static_cast<float>(i) / (count - 1) : 0.f;

            if (m_cfg.gradientMode > 0) {
                ccColor3B col;
                if (m_cfg.gradAnim == "rainbow") {
                    col = hsv(frac + m_time * 0.2f);
                } else if (m_cfg.gradAnim == "flow") {
                    col = sampleGradient(m_cfg, std::fmod(frac + m_time * 0.3f, 1.f));
                } else if (m_cfg.gradAnim == "pulse") {
                    float k = 0.5f + 0.5f * std::sin(m_time * 2.f);
                    col = sampleGradient(m_cfg, k);
                } else if (m_cfg.gradAnim == "wave") {
                    float k = 0.5f + 0.5f * std::sin(frac * kTwoPi + m_time * 2.f);
                    col = sampleGradient(m_cfg, k);
                } else {
                    col = sampleGradient(m_cfg, frac);
                }
                letter->setColor(col);
            }

            if (static_cast<int>(m_basePos.size()) == count) {
                CCPoint base = m_basePos[i];
                if (m_cfg.letterAnim == "wave") {
                    float dy = std::sin(ltime * 3.f + i * 0.6f) * 3.5f * m_cfg.letterAmount;
                    letter->setPosition({base.x, base.y + dy});
                } else if (m_cfg.letterAnim == "bounce") {
                    float ph = std::fmod(ltime * 2.f + i * 0.4f, kTwoPi);
                    float dy = std::fabs(std::sin(ph)) * 4.f * m_cfg.letterAmount;
                    letter->setPosition({base.x, base.y + dy});
                } else if (m_cfg.letterAnim == "jitter") {
                    float dx = std::sin(ltime * 20.f + i * 1.3f) * 1.2f * m_cfg.letterAmount;
                    float dy = std::cos(ltime * 23.f + i * 1.7f) * 1.2f * m_cfg.letterAmount;
                    letter->setPosition({base.x + dx, base.y + dy});
                }
            }
        }
    }
};
}

CCLabelBMFont* applyStatic(CCLabelBMFont* label, PicNameConfig const& cfg) {
    if (!label) return label;
    if (!cfg.enabled) return label;

    if (cfg.gradientMode == 0) {
        label->setColor(cfg.color);
    }

    // shadow via a dim offset copy behind the label.
    label->removeChildByID("paimon-name-glow"_spr);
    label->removeChildByID("paimon-name-outline"_spr);

    if (cfg.glow) {
        if (auto* glow = CCLabelBMFont::create(label->getString(), label->getFntFile())) {
            glow->setID("paimon-name-glow"_spr);
            glow->setColor(cfg.glowColor);
            glow->setOpacity(140);
            glow->setScale(1.08f);
            glow->setAnchorPoint({0.5f, 0.5f});
            glow->setPosition({label->getContentSize().width / 2.f, label->getContentSize().height / 2.f});
            glow->setZOrder(-2);
            label->addChild(glow);
        }
    }
    if (cfg.outline) {
        for (auto off : {CCPoint{1.2f, 1.2f}, CCPoint{-1.2f, -1.2f}, CCPoint{1.2f, -1.2f}, CCPoint{-1.2f, 1.2f}}) {
            if (auto* out = CCLabelBMFont::create(label->getString(), label->getFntFile())) {
                out->setID("paimon-name-outline"_spr);
                out->setColor(cfg.outlineColor);
                out->setAnchorPoint({0.5f, 0.5f});
                out->setPosition({label->getContentSize().width / 2.f + off.x,
                                  label->getContentSize().height / 2.f + off.y});
                out->setZOrder(-3);
                label->addChild(out);
            }
        }
    }
    return label;
}

void decorate(CCLabelBMFont* label, PicNameConfig const& cfg) {
    if (!label || !cfg.enabled) return;

    applyStatic(label, cfg);

    // static gradient still needs a one-shot per-letter paint.
    if (cfg.gradientMode > 0 && !needsAnimator(cfg)) {
        if (auto* kids = label->getChildren()) {
            int count = static_cast<int>(kids->count());
            for (int i = 0; i < count; i++) {
                auto* letter = static_cast<CCSprite*>(kids->objectAtIndex(i));
                float frac = count > 1 ? static_cast<float>(i) / (count - 1) : 0.f;
                letter->setColor(sampleGradient(cfg, frac));
            }
        }
        return;
    }

    label->removeChildByID("paimon-name-animator"_spr);
    if (needsAnimator(cfg)) {
        if (auto* anim = NameAnimator::create(label, cfg)) {
            anim->setID("paimon-name-animator"_spr);
            label->addChild(anim);
        }
    }
}

}
