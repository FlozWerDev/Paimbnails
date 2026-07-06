#include <Geode/Geode.hpp>
#include <Geode/modify/CCTextInputNode.hpp>
#include <Geode/binding/TextArea.hpp>
#include <Geode/binding/MultilineBitmapFont.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <string>
#include <vector>

using namespace geode::prelude;

// Per-character entrance/exit animation for GD text fields. Freshly typed
// glyphs fade + rise + pop into place; deleted ones leave a ghost that fades
// away. Reacts live to the mod settings and stays out of Geode's own setting
// inputs, where rebuilding the label mid-animation looks glitchy.
namespace {

constexpr int kInTag    = 0x9A11;
constexpr int kGhostTag = 0x9A12;

struct Cfg {
    bool  on;
    float fadeIn;
    float fadeOut;
    float rise;
    bool  pop;
};

Cfg readCfg() {
    auto* m = Mod::get();
    return {
        m->getSettingValue<bool>("smooth-text-enabled"),
        static_cast<float>(m->getSettingValue<double>("smooth-text-fade-in")),
        static_cast<float>(m->getSettingValue<double>("smooth-text-fade-out")),
        static_cast<float>(m->getSettingValue<double>("smooth-text-rise")),
        m->getSettingValue<bool>("smooth-text-scale-pop"),
    };
}

bool cfgAnimates(Cfg const& c) {
    return c.on && (c.fadeIn > 0.f || c.fadeOut > 0.f || c.rise > 0.f || c.pop);
}

// Frozen copy of one glyph, enough to rebuild it as a fading ghost.
struct GlyphShot {
    CCTexture2D* tex = nullptr;
    CCRect       rect;
    CCPoint      world;
    CCPoint      anchor;
    float        scaleX = 1.f;
    float        scaleY = 1.f;
    float        rotation = 0.f;
    ccColor3B    color{255, 255, 255};
    GLubyte      opacity = 255;
};

} // namespace

class $modify(SmoothTextInput, CCTextInputNode) {
    struct Fields {
        std::string           lastText;
        std::vector<GlyphShot> lastShots;
        bool                  programmatic = false;
    };

    CCNode* animLabel() {
        if (m_textArea && m_textArea->m_label) return m_textArea->m_label;
        return m_textLabel;
    }

    GLubyte fullOpacity() {
        if (m_textArea && m_textArea->m_label) return m_textArea->m_label->getOpacity();
        return m_textLabel ? m_textLabel->getOpacity() : 255;
    }

    // Sprite for raw index i. TextArea labels drop newlines from their glyph
    // array, so we skip those when mapping raw text position to glyph slot.
    CCSprite* glyphAt(std::string const& text, size_t i) {
        if (i >= text.size()) return nullptr;

        if (m_textArea && m_textArea->m_label) {
            if (text[i] == '\n' || text[i] == '\r') return nullptr;
            auto chars = m_textArea->m_label->m_characters;
            if (!chars) return nullptr;
            size_t slot = 0;
            for (size_t k = 0; k < i; ++k)
                if (text[k] != '\n' && text[k] != '\r') ++slot;
            if (slot >= chars->count()) return nullptr;
            return typeinfo_cast<CCSprite*>(chars->objectAtIndex(static_cast<unsigned int>(slot)));
        }

        if (!m_textLabel) return nullptr;
        return typeinfo_cast<CCSprite*>(m_textLabel->getChildByTag(static_cast<int>(i)));
    }

    void forEachGlyph(auto&& fn) {
        if (m_textArea && m_textArea->m_label) {
            if (auto chars = m_textArea->m_label->m_characters)
                for (unsigned int i = 0; i < chars->count(); ++i)
                    if (auto s = typeinfo_cast<CCSprite*>(chars->objectAtIndex(i))) fn(s);
            return;
        }
        if (!m_textLabel) return;
        if (auto kids = m_textLabel->getChildren())
            for (unsigned int i = 0; i < kids->count(); ++i)
                if (auto s = typeinfo_cast<CCSprite*>(kids->objectAtIndex(i))) fn(s);
    }

    void snapshot() {
        auto& shots = m_fields->lastShots;
        shots.clear();
        auto const& text = m_fields->lastText;
        shots.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            GlyphShot g;
            if (auto s = glyphAt(text, i)) {
                auto parent = s->getParent();
                g.tex      = s->getTexture();
                g.rect     = s->getTextureRect();
                g.world    = parent ? parent->convertToWorldSpace(s->getPosition()) : s->getPosition();
                g.anchor   = s->getAnchorPoint();
                g.scaleX   = s->getScaleX();
                g.scaleY   = s->getScaleY();
                g.rotation = s->getRotation();
                g.color    = s->getColor();
                g.opacity  = s->getOpacity();
            }
            shots.push_back(g);
        }
    }

    void popIn(CCSprite* s, Cfg const& c) {
        s->stopActionByTag(kInTag);

        CCPoint dest = s->getPosition();
        float   fx = s->getScaleX(), fy = s->getScaleY();
        GLubyte full = fullOpacity();
        float   dur = std::max(c.fadeIn, 0.05f);

        auto steps = CCArray::create();
        s->setOpacity(0);
        steps->addObject(CCFadeTo::create(dur, full));

        if (c.rise > 0.f) {
            s->setPosition(dest - CCPoint(0.f, c.rise));
            steps->addObject(CCEaseSineOut::create(CCMoveTo::create(dur, dest)));
        }
        if (c.pop) {
            s->setScaleX(fx * 0.3f);
            s->setScaleY(fy * 0.3f);
            steps->addObject(CCEaseBackOut::create(CCScaleTo::create(dur, fx, fy)));
        }

        auto act = CCSpawn::create(steps);
        act->setTag(kInTag);
        s->runAction(act);
    }

    void ghostOut(std::vector<GlyphShot> const& ghosts, Cfg const& c) {
        if (ghosts.empty() || c.fadeOut <= 0.f) return;
        auto label = animLabel();
        if (!label) return;
        auto parent = label->getParent();
        if (!parent) return;

        float lsx = label->getScaleX(), lsy = label->getScaleY(), lrot = label->getRotation();
        int   z = label->getZOrder();

        for (auto const& g : ghosts) {
            if (!g.tex) continue;
            auto s = CCSprite::createWithTexture(g.tex, g.rect);
            if (!s) continue;

            s->setPosition(parent->convertToNodeSpace(g.world));
            s->setAnchorPoint(g.anchor);
            s->setScaleX(g.scaleX * lsx);
            s->setScaleY(g.scaleY * lsy);
            s->setRotation(g.rotation + lrot);
            s->setColor(g.color);
            s->setOpacity(g.opacity);
            s->setTag(kGhostTag);
            parent->addChild(s, z);

            auto steps = CCArray::create();
            steps->addObject(CCFadeOut::create(c.fadeOut));
            if (c.rise > 0.f)
                steps->addObject(CCMoveBy::create(c.fadeOut, CCPoint(0.f, c.rise)));
            if (c.pop)
                steps->addObject(CCScaleTo::create(c.fadeOut, s->getScaleX() * 0.3f, s->getScaleY() * 0.3f));

            s->runAction(CCSequence::createWithTwoActions(
                CCSpawn::create(steps), CCRemoveSelf::create()));
        }
    }

    void purgeGhosts() {
        CCNode* labels[] = {
            m_textLabel,
            m_textArea ? static_cast<CCNode*>(m_textArea->m_label) : nullptr,
        };
        for (auto label : labels) {
            if (!label) continue;
            auto parent = label->getParent();
            if (!parent) continue;
            auto kids = parent->getChildren();
            if (!kids) continue;
            std::vector<CCNode*> dead;
            for (unsigned int i = 0; i < kids->count(); ++i) {
                auto n = typeinfo_cast<CCNode*>(kids->objectAtIndex(i));
                if (n && n->getTag() == kGhostTag) dead.push_back(n);
            }
            for (auto n : dead) n->removeFromParent();
        }
    }

    void settle() {
        GLubyte full = fullOpacity();
        forEachGlyph([&](CCSprite* s) {
            s->stopActionByTag(kInTag);
            s->setOpacity(full);
        });
    }

    void plainRefresh() {
        CCTextInputNode::refreshLabel();
        purgeGhosts();
        settle();
        m_fields->lastText = m_textField ? m_textField->getString() : "";
        snapshot();
    }

    void setString(gd::string text) {
        m_fields->programmatic = true;
        CCTextInputNode::setString(text);
        m_fields->programmatic = false;
    }

    void refreshLabel() {
        if (m_fields->programmatic || !m_selected || getParentByType<SettingNodeV3>(0)) {
            plainRefresh();
            return;
        }

        auto cfg = readCfg();
        if (!cfgAnimates(cfg)) { plainRefresh(); return; }

        std::string oldStr = m_fields->lastText;
        std::string newStr = m_textField ? m_textField->getString() : "";

        if (oldStr == newStr) {
            CCTextInputNode::refreshLabel();
            m_fields->lastText = newStr;
            snapshot();
            return;
        }

        // Shrink the change down to the run that actually differs: common
        // prefix, common suffix, and whatever sits between them.
        size_t bound = std::min(oldStr.size(), newStr.size());
        size_t p = 0;
        while (p < bound && oldStr[p] == newStr[p]) ++p;
        size_t suf = 0;
        while (suf < bound - p &&
               oldStr[oldStr.size() - 1 - suf] == newStr[newStr.size() - 1 - suf]) ++suf;

        size_t newStart = p, newEnd = newStr.size() - suf;
        size_t oldStart = p, oldEnd = oldStr.size() - suf;

        std::vector<GlyphShot> ghosts;
        for (size_t i = oldStart; i < oldEnd && i < m_fields->lastShots.size(); ++i)
            ghosts.push_back(m_fields->lastShots[i]);

        CCTextInputNode::refreshLabel();
        m_fields->lastText = newStr;
        snapshot();

        for (size_t i = newStart; i < newEnd; ++i)
            if (auto g = glyphAt(newStr, i)) popIn(g, cfg);

        ghostOut(ghosts, cfg);
    }
};
