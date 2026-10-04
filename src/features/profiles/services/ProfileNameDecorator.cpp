#include "ProfileNameDecorator.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::profile_name {

namespace {
constexpr float kTwoPi = 6.283185307179586f;
constexpr int kAnimActionTag = 0x50A7;

// CCLabelBMFont is a sprite batch: any child that isn't a CCSprite on the font
// texture crashes in addChild, so glow/outline are per-letter copies tagged above letter indices.
constexpr int kExtraTagBase = 0x50A10000;
constexpr int kGlowSlot = 0;
constexpr int kExtraSlots = 5;
CCPoint const kExtraOffsets[kExtraSlots] = {
    {0.f, 0.f}, {1.2f, 1.2f}, {-1.2f, -1.2f}, {1.2f, -1.2f}, {-1.2f, 1.2f},
};
constexpr float kGlowScale = 1.08f;

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

bool movesLetters(PicNameConfig const& cfg) {
    return cfg.letterAnim == "wave" || cfg.letterAnim == "bounce" || cfg.letterAnim == "jitter";
}

bool isExtra(CCNode* node) {
    return node->getTag() >= kExtraTagBase;
}

int extraTag(int letterTag, int slot) {
    return kExtraTagBase + letterTag * kExtraSlots + slot;
}

// hidden children are leftovers from a longer previous string.
std::vector<CCSprite*> collectLetters(CCLabelBMFont* label) {
    std::vector<CCSprite*> out;
    auto* kids = label->getChildren();
    if (!kids) return out;
    for (auto* node : CCArrayExt<CCNode*>(kids)) {
        if (!node || isExtra(node) || node->getTag() < 0 || !node->isVisible()) continue;
        if (auto* spr = typeinfo_cast<CCSprite*>(node)) out.push_back(spr);
    }
    return out;
}

void removeExtras(CCLabelBMFont* label) {
    auto* kids = label->getChildren();
    if (!kids) return;
    std::vector<Ref<CCNode>> doomed;
    for (auto* node : CCArrayExt<CCNode*>(kids)) {
        if (node && isExtra(node)) doomed.emplace_back(node);
    }
    for (auto& node : doomed) label->removeChild(node, true);
}

void addLetterCopies(CCLabelBMFont* label, PicNameConfig const& cfg) {
    auto* tex = label->getTexture();
    if (!tex) return;

    auto addCopy = [&](CCSprite* letter, int slot, ccColor3B color, GLubyte opacity, float scale, int z) {
        auto* copy = CCSprite::createWithTexture(tex, letter->getTextureRect());
        if (!copy) return;
        copy->setAnchorPoint(letter->getAnchorPoint());
        copy->setPosition(letter->getPosition() + kExtraOffsets[slot]);
        copy->setScale(letter->getScale() * scale);
        copy->setColor(color);
        copy->setOpacity(opacity);
        label->addChild(copy, z, extraTag(letter->getTag(), slot));
    };

    for (auto* letter : collectLetters(label)) {
        if (cfg.glow) addCopy(letter, kGlowSlot, cfg.glowColor, 140, kGlowScale, -2);
        if (cfg.outline) {
            for (int slot = 1; slot < kExtraSlots; slot++) {
                addCopy(letter, slot, cfg.outlineColor, 255, 1.f, -3);
            }
        }
    }
}

// runs as an action so nothing but sprites ever lands inside the label batch.
class NameAnimAction : public CCAction {
public:
    PicNameConfig m_cfg;
    float m_time = 0.f;
    std::vector<CCPoint> m_basePos;

    static NameAnimAction* create(PicNameConfig const& cfg) {
        auto* action = new NameAnimAction();
        action->m_cfg = cfg;
        action->setTag(kAnimActionTag);
        action->autorelease();
        return action;
    }

    bool isDone() override { return false; }

    void step(float dt) override {
        if (paimon::isRuntimeShuttingDown()) return;
        auto* label = typeinfo_cast<CCLabelBMFont*>(getTarget());
        if (!label) return;

        auto letters = collectLetters(label);
        int count = static_cast<int>(letters.size());
        // string changes rebuild letter sprites; resync base offsets.
        if (count != static_cast<int>(m_basePos.size())) {
            m_basePos.clear();
            for (auto* letter : letters) m_basePos.push_back(letter->getPosition());
        }
        if (count == 0) return;

        m_time += dt * std::max(m_cfg.gradSpeed, 0.05f);
        float ltime = m_time * std::max(m_cfg.letterSpeed, 0.05f);

        for (int i = 0; i < count; i++) {
            auto* letter = letters[i];
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

        if (movesLetters(m_cfg) && (m_cfg.glow || m_cfg.outline)) syncExtras(label, letters);
    }

private:
    void syncExtras(CCLabelBMFont* label, std::vector<CCSprite*> const& letters) {
        std::vector<CCSprite*> byTag;
        for (auto* letter : letters) {
            int tag = letter->getTag();
            if (tag >= static_cast<int>(byTag.size())) byTag.resize(tag + 1, nullptr);
            byTag[tag] = letter;
        }
        for (auto* node : CCArrayExt<CCNode*>(label->getChildren())) {
            if (!node || !isExtra(node)) continue;
            int rel = node->getTag() - kExtraTagBase;
            int letterTag = rel / kExtraSlots;
            int slot = rel % kExtraSlots;
            if (letterTag >= static_cast<int>(byTag.size()) || !byTag[letterTag]) continue;
            node->setPosition(byTag[letterTag]->getPosition() + kExtraOffsets[slot]);
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

    removeExtras(label);
    if (cfg.glow || cfg.outline) addLetterCopies(label, cfg);
    return label;
}

void decorate(CCLabelBMFont* label, PicNameConfig const& cfg) {
    if (!label) return;

    label->stopActionByTag(kAnimActionTag);
    if (!cfg.enabled) {
        removeExtras(label);
        return;
    }
    applyStatic(label, cfg);

    if (needsAnimator(cfg)) {
        label->runAction(NameAnimAction::create(cfg));
        return;
    }

    // static gradient still needs a one-shot per-letter paint.
    if (cfg.gradientMode > 0) {
        auto letters = collectLetters(label);
        int count = static_cast<int>(letters.size());
        for (int i = 0; i < count; i++) {
            float frac = count > 1 ? static_cast<float>(i) / (count - 1) : 0.f;
            letters[i]->setColor(sampleGradient(cfg, frac));
        }
    }
}

}
