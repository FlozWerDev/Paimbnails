#include "EmoteAutocomplete.hpp"
#include "../services/EmoteService.hpp"
#include "../services/EmoteCache.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::emotes;

EmoteAutocomplete* EmoteAutocomplete::create(
    CCTextInputNode* input,
    CopyableFunction<void(std::string const&)> setTextFn)
{
    auto ret = new EmoteAutocomplete();
    if (ret && ret->init(input, std::move(setTextFn))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool EmoteAutocomplete::init(
    CCTextInputNode* input,
    CopyableFunction<void(std::string const&)> setTextFn)
{
    if (!CCNode::init()) return false;

    m_inputNode = input;
    m_setTextFn = std::move(setTextFn);
    this->setID("emote-autocomplete"_spr);

    // Background (hidden until suggestions appear); prefer NineSlice, fall back to Scale9.
    m_bg = paimon::SpriteHelper::safeCreateNineSliceFromFile("square02_small.png");
    if (!m_bg) m_bg = paimon::SpriteHelper::safeCreateScale9("square02_small.png");
    if (!m_bg) m_bg = paimon::SpriteHelper::createDarkPanel(200.f, 60.f, 220, 4.f);
    m_bg->setColor({25, 25, 35});
    m_bg->setOpacity(220);
    m_bg->setVisible(false);
    this->addChild(m_bg, 0);

    // Menu for clickable suggestions
    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    m_menu->setVisible(false);
    this->addChild(m_menu, 1);

    this->scheduleUpdate();
    return true;
}

void EmoteAutocomplete::update(float /*dt*/) {
    if (!m_inputNode || !m_inputNode->getParent()) {
        clearSuggestions();
        return;
    }

    std::string text = m_inputNode->getString();
    if (text == m_lastText) return;
    m_lastText = text;

    // Find the last unmatched ':' followed by a 2+ char partial.
    size_t lastColon = text.rfind(':');
    if (lastColon == std::string::npos) {
        clearSuggestions();
        return;
    }

    // Check there's no closing ':' after this one
    std::string partial = text.substr(lastColon + 1);
    if (partial.empty() || partial.size() < 2) {
        clearSuggestions();
        return;
    }

    // Only allow word-like characters in the partial (letters, digits, _, -)
    for (char c : partial) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            clearSuggestions();
            return;
        }
    }

    // Search for matching emotes
    auto matches = EmoteService::get().searchEmotes(partial, 8);
    if (matches.empty()) {
        clearSuggestions();
        return;
    }

    rebuildSuggestions(matches, partial, lastColon);
}

void EmoteAutocomplete::rebuildSuggestions(
    std::vector<EmoteInfo> const& matches,
    std::string const& /*partial*/, size_t colonPos)
{
    bool wasHidden = !m_menu->isVisible();
    m_colonPos = colonPos;

    // Clear old items
    m_menu->removeAllChildren();
    m_menu->setVisible(true);
    m_bg->setVisible(true);

    constexpr float ROW_H = 28.f;
    constexpr float PAD_X = 10.f;
    constexpr float EMOTE_SZ = 22.f;
    float maxWidth = 0.f;
    float totalHeight = matches.size() * ROW_H;

    for (size_t i = 0; i < matches.size(); ++i) {
        auto const& info = matches[i];

        // Row container: [emote] :name:
        auto row = CCNode::create();
        row->setAnchorPoint({0.f, 0.5f});
        row->setContentSize({220.f, ROW_H});

        // Emote sprite placeholder (left side)
        auto emotePh = CCNode::create();
        emotePh->setContentSize({EMOTE_SZ, EMOTE_SZ});
        emotePh->setAnchorPoint({0.f, 0.5f});
        emotePh->setPosition({0.f, ROW_H / 2.f});
        row->addChild(emotePh, 5);

        // Label: ":name:" (right of emote)
        auto label = CCLabelBMFont::create(
            fmt::format(":{}:", info.name).c_str(), "chatFont.fnt");
        label->setScale(0.55f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({EMOTE_SZ + 6.f, ROW_H / 2.f});
        row->addChild(label);

        float textW = label->getScaledContentWidth();

        float rowW = EMOTE_SZ + 6.f + textW + PAD_X * 2.f;
        if (rowW > maxWidth) maxWidth = rowW;
        row->setContentSize({rowW, ROW_H});

        // Async load emote image into placeholder
        Ref<CCNode> phRef = emotePh;
        EmoteCache::get().loadEmote(info, [phRef, EMOTE_SZ](CCTexture2D* tex, bool isGif, std::vector<uint8_t> const& gifData) {
            Loader::get()->queueInMainThread([phRef, tex, isGif, gifData, EMOTE_SZ]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (!phRef || !phRef->getParent()) return;

                CCNode* sprite = nullptr;
                if (isGif && !gifData.empty()) {
                    sprite = AnimatedGIFSprite::create(gifData.data(), gifData.size());
                } else if (tex) {
                    sprite = CCSprite::createWithTexture(tex);
                }

                if (sprite) {
                    float scale = EMOTE_SZ / std::max(sprite->getContentSize().width, sprite->getContentSize().height);
                    sprite->setScale(scale);
                    sprite->setAnchorPoint({0.5f, 0.5f});
                    sprite->setPosition({EMOTE_SZ / 2.f, EMOTE_SZ / 2.f});
                    phRef->addChild(sprite);
                }
            });
        });

        // Wrap row in a button
        auto btn = CCMenuItemSpriteExtra::create(
            row, this, menu_selector(EmoteAutocomplete::onSuggestionClicked));

        // Store emote name in UserObject
        btn->setUserObject(CCString::create(info.name));

        // Position: stack rows downward from y=0 (negative y)
        float rowY = -(static_cast<float>(i) + 0.5f) * ROW_H;
        btn->setPosition({PAD_X, rowY});
        btn->setAnchorPoint({0.f, 0.5f});

        // Staggered ease-in so the list unfolds top to bottom.
        if (wasHidden) {
            btn->stopAllActions();
            btn->setScale(0.55f);
            btn->setOpacity(0);
            // CCMenuItemSpriteExtra forwards opacity to its sprite.
            float delay = 0.025f * static_cast<float>(i);
            btn->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCSpawn::create(
                    CCEaseBackOut::create(CCScaleTo::create(0.22f, 1.0f)),
                    CCFadeTo::create(0.18f, 255),
                    nullptr),
                nullptr));
        } else {
            btn->setScale(1.0f);
            btn->setOpacity(255);
        }

        m_menu->addChild(btn);
    }

    // Resize background (dropdown goes downward)
    float bgW = std::max(maxWidth + PAD_X, 160.f);
    float bgH = totalHeight + 8.f;
    m_bg->setContentSize({bgW, bgH});
    m_bg->setAnchorPoint({0.f, 1.f});
    m_bg->setPosition({-2.f, 4.f});

    // Smooth opening: the background "unfolds" downward (anchored at top).
    if (wasHidden) {
        m_bg->stopAllActions();
        m_bg->setScaleY(0.05f);
        m_bg->setScaleX(0.96f);
        m_bg->setOpacity(0);
        m_bg->runAction(CCSpawn::create(
            CCEaseBackOut::create(CCScaleTo::create(0.22f, 1.0f, 1.0f)),
            CCFadeTo::create(0.16f, 220),
            nullptr));
    } else {
        m_bg->setScale(1.0f);
        m_bg->setOpacity(220);
    }
}

void EmoteAutocomplete::clearSuggestions() {
    if (m_menu) {
        m_menu->stopAllActions();
        m_menu->removeAllChildren();
        m_menu->setVisible(false);
    }
    if (m_bg) {
        m_bg->stopAllActions();
        m_bg->setVisible(false);
        m_bg->setScale(1.0f);
        m_bg->setOpacity(220);
    }
    m_colonPos = std::string::npos;
}

void EmoteAutocomplete::onSuggestionClicked(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    auto nameObj = static_cast<CCString*>(btn->getUserObject());
    if (!nameObj || !m_inputNode) return;

    std::string emoteName = nameObj->getCString();
    std::string text = m_inputNode->getString();

    if (m_colonPos == std::string::npos || m_colonPos >= text.size()) return;

    // Replace ":partial" with ":emotename:"
    std::string newText = text.substr(0, m_colonPos) + ":" + emoteName + ":";
    if (m_setTextFn) {
        m_setTextFn(newText);
    }

    clearSuggestions();
    m_lastText = newText;
}
