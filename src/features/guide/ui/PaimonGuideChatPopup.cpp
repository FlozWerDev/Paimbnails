#include "PaimonGuideChatPopup.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../services/PaimonGuideService.hpp"
#include "../services/PopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>

using namespace geode::prelude;

namespace paimon::guide {

namespace {

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 290.f;

constexpr float kChatFrameX = 100.f;
constexpr float kChatFrameY = 94.f;
constexpr float kChatFrameW = 326.f;
constexpr float kChatFrameH = 152.f;

constexpr float kChatScrollW = kChatFrameW - 8.f;
constexpr float kChatScrollH = kChatFrameH - 8.f;
constexpr float kChatRowW    = kChatScrollW - 12.f;

constexpr float kBubblePadX     = 8.f;
constexpr float kBubblePadY     = 6.f;
constexpr float kBubbleGap      = 5.f;
constexpr float kChatEdgePad    = 6.f;
constexpr float kLabelScale     = 0.45f;
constexpr std::size_t kWrapChars  = 44;
constexpr std::size_t kMaxBubbles = 30;

constexpr float kInputY = 66.f;

std::string tr(char const* key, char const* fallback = "") {
    auto v = Localization::get().getString(key);
    if (v == key && fallback && fallback[0] != '\0') return fallback;
    return v;
}

// Wrap text to roughly maxChars while preserving words.
std::string wrapText(std::string const& text, std::size_t maxChars) {
    std::string out;
    std::size_t lineLen = 0;
    std::string word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        if (lineLen + word.size() + (lineLen > 0 ? 1 : 0) > maxChars && lineLen > 0) {
            out.push_back('\n');
            lineLen = 0;
        }
        if (lineLen > 0) {
            out.push_back(' ');
            ++lineLen;
        }
        out += word;
        lineLen += word.size();
        word.clear();
    };
    for (char c : text) {
        if (c == '\n') {
            flushWord();
            out.push_back('\n');
            lineLen = 0;
        } else if (c == ' ' || c == '\t') {
            flushWord();
        } else {
            word.push_back(c);
        }
    }
    flushWord();
    return out;
}

// Strip GD color tags; CCLabelBMFont would render them literally.
std::string stripGDColorTags(std::string const& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ) {
        if (in[i] == '<' && i + 2 < in.size()) {
            if (in[i + 1] == '/' && in[i + 2] == 'c' && i + 3 < in.size() && in[i + 3] == '>') {
                i += 4;
                continue;
            }
            if (in[i + 1] == 'c' && i + 3 < in.size() && in[i + 3] == '>') {
                char x = in[i + 2];
                bool isColor = (x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || x == '_';
                if (isColor) {
                    i += 4;
                    continue;
                }
            }
        }
        out.push_back(in[i]);
        ++i;
    }
    return out;
}

}

PaimonGuideChatPopup* PaimonGuideChatPopup::create() {
    auto ret = new PaimonGuideChatPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool PaimonGuideChatPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);

    auto title = tr("pai.guide.title", "Paimon Guide");
    this->setTitle(title.c_str());

    auto layerSize = m_mainLayer->getContentSize();


    m_paimon = AnimatedPaimon::create(0.5f);
    if (m_paimon) {
        m_paimon->setLively(true);
        m_paimon->setAnchorPoint({0.5f, 0.5f});
        m_paimon->setPosition({50.f, 185.f});
        m_mainLayer->addChild(m_paimon, 5);
        m_paimon->play(AnimatedPaimon::Animation::Wave);
    }

    {
        int featureCount = static_cast<int>(PopupRegistry::get().entries().size());
        std::string version = "?";
        // toNonVString: el formato de abajo ya pone la "v", toVString daria "vv1.1.0".
        if (auto* mod = Mod::get()) version = mod->getVersion().toNonVString(false);

        auto featuresWord = tr("pai.guide.subtitle", "features");
        auto subtitle = fmt::format("{} {}\nv{}", featureCount, featuresWord, version);

        auto badge = CCLabelBMFont::create(subtitle.c_str(), "goldFont.fnt");
        badge->setScale(0.3f);
        badge->setAlignment(kCCTextAlignmentCenter);
        badge->setPosition({50.f, 128.f});
        badge->setID("guide-feature-badge"_spr);
        m_mainLayer->addChild(badge, 5);
    }

    {
        m_topicLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_topicLabel->setScale(0.32f);
        m_topicLabel->setAlignment(kCCTextAlignmentCenter);
        m_topicLabel->setPosition({50.f, 113.f});
        m_topicLabel->setOpacity(160);
        m_topicLabel->setID("guide-topic-label"_spr);
        m_mainLayer->addChild(m_topicLabel, 5);
    }

    {
        auto utilMenu = CCMenu::create();
        utilMenu->setContentSize({90.f, 30.f});
        utilMenu->setAnchorPoint({0.5f, 0.5f});
        utilMenu->ignoreAnchorPointForPosition(false);
        utilMenu->setPosition({50.f, 98.f});
        utilMenu->setID("guide-util-menu"_spr);

        auto trashSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
        if (trashSpr) {
            trashSpr->setScale(0.5f);
            auto clearBtn = CCMenuItemSpriteExtra::create(
                trashSpr, this, menu_selector(PaimonGuideChatPopup::onClearChat)
            );
            clearBtn->setID("guide-clear-btn"_spr);
            clearBtn->setPosition({27.f, 15.f});
            utilMenu->addChild(clearBtn);
        }

        auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        if (infoSpr) {
            infoSpr->setScale(0.65f);
            auto helpBtn = CCMenuItemSpriteExtra::create(
                infoSpr, this, menu_selector(PaimonGuideChatPopup::onHelpButton)
            );
            helpBtn->setID("guide-help-btn"_spr);
            helpBtn->setPosition({63.f, 15.f});
            utilMenu->addChild(helpBtn);
        }

        m_mainLayer->addChild(utilMenu, 5);
    }

    {
        m_modeBtn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"),
            this, menu_selector(PaimonGuideChatPopup::onToggleMode)
        );
        m_modeBtn->setID("guide-mode-btn"_spr);
        m_modeBtn->setScale(0.6f);

        auto modeMenu = CCMenu::create();
        modeMenu->setContentSize({40.f, 40.f});
        modeMenu->setPosition({50.f, 74.f});
        modeMenu->addChild(m_modeBtn);
        m_modeBtn->setPosition({0.f, 0.f});
        m_mainLayer->addChild(modeMenu, 5);

        m_modeLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_modeLabel->setScale(0.28f);
        m_modeLabel->setPosition({50.f, 62.f});
        m_modeLabel->setID("guide-mode-label"_spr);
        m_mainLayer->addChild(m_modeLabel, 5);

        refreshModeButton();
    }


    auto chatFrame = CCScale9Sprite::create("GJ_square01.png");
    chatFrame->setColor({25, 28, 40});
    chatFrame->setOpacity(210);
    chatFrame->setContentSize({kChatFrameW, kChatFrameH});
    chatFrame->setAnchorPoint({0.f, 0.f});
    chatFrame->setPosition({kChatFrameX, kChatFrameY});
    chatFrame->setID("guide-chat-frame"_spr);
    m_mainLayer->addChild(chatFrame, 3);

    m_scroll = ScrollLayer::create({kChatScrollW, kChatScrollH});
    m_scroll->setPosition({kChatFrameX + 4.f, kChatFrameY + 4.f});
    m_scroll->setID("guide-chat-scroll"_spr);
    m_mainLayer->addChild(m_scroll, 4);


    constexpr float kInputW = 250.f;

    m_input = AnimatedTextInput::create(kInputW,
        tr("pai.guide.placeholder", "Ask me anything..."));
    if (m_input) {
        m_input->setAnchorPoint({0.f, 0.5f});
        m_input->setPosition({kChatFrameX, kInputY});
        m_mainLayer->addChild(m_input, 5);

// Enter submits a focused query.
        geode::WeakRef<PaimonGuideChatPopup> weak = this;
        m_input->setOnSubmit([weak]() {
// Defer mutation out of the IME callback.
            Loader::get()->queueInMainThread([weak]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (auto self = weak.lock()) {
                    static_cast<PaimonGuideChatPopup*>(self.data())->trySubmitFromEnter();
                }
            });
        });
    }

    auto sendSpr = ButtonSprite::create(
        tr("pai.guide.send", "Ask").c_str(),
        "goldFont.fnt", "GJ_button_01.png", 0.8f
    );
    sendSpr->setScale(0.55f);
    auto sendBtn = CCMenuItemSpriteExtra::create(
        sendSpr, this, menu_selector(PaimonGuideChatPopup::onSubmitButton)
    );
    sendBtn->setID("guide-send-btn"_spr);

    auto sendMenu = CCMenu::create();
    sendMenu->setContentSize({70.f, 40.f});
    sendMenu->setPosition({(kChatFrameX + kInputW + layerSize.width) * 0.5f - 6.f, kInputY});
    sendMenu->addChild(sendBtn);
    sendBtn->setPosition({0.f, 0.f});
    m_mainLayer->addChild(sendMenu, 5);

    {
        auto hintText = tr("pai.guide.hint.enter", "Enter to send");
        auto hint = CCLabelBMFont::create(hintText.c_str(), "chatFont.fnt");
        hint->setScale(0.35f);
        hint->setOpacity(110);
        hint->setPosition({kChatFrameX + kInputW * 0.5f, kInputY - 20.f});
        hint->setID("guide-enter-hint"_spr);
        m_mainLayer->addChild(hint, 5);
    }


    auto takeMeSpr = ButtonSprite::create(
        tr("pai.guide.take.me.there", "Take me there").c_str(),
        "bigFont.fnt", "GJ_button_05.png", 0.8f
    );
    takeMeSpr->setScale(0.45f);
    m_takeMeBtn = CCMenuItemSpriteExtra::create(
        takeMeSpr, this, menu_selector(PaimonGuideChatPopup::onTakeMeThere)
    );
    m_takeMeBtn->setID("guide-take-me-btn"_spr);
    m_takeMeBtn->setVisible(false);

    m_takeMeMenu = CCMenu::create();
    m_takeMeMenu->setContentSize({150.f, 22.f});
    m_takeMeMenu->setPosition({kChatFrameX + kChatFrameW * 0.5f, kChatFrameY});
    m_takeMeMenu->addChild(m_takeMeBtn);
    m_takeMeBtn->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_takeMeMenu, 10);


    m_suggestionsMenu = CCMenu::create();
    m_suggestionsMenu->setID("guide-suggestions"_spr);
    m_suggestionsMenu->setContentSize({layerSize.width - 30.f, 22.f});
    m_suggestionsMenu->setAnchorPoint({0.5f, 0.5f});
    m_suggestionsMenu->ignoreAnchorPointForPosition(false);
    m_suggestionsMenu->setPosition({layerSize.width * 0.5f, 22.f});
    m_suggestionsMenu->setLayout(
        RowLayout::create()
            ->setGap(5.f)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
    );

    auto suggestions = PaimonGuideService::get().getSuggestions();
    for (auto const& [chipText, query] : suggestions) {
        auto* chipSpr = ButtonSprite::create(
            chipText.c_str(), "bigFont.fnt", "GJ_button_05.png", 0.6f
        );
        chipSpr->setScale(0.42f);
        auto* chipBtn = CCMenuItemSpriteExtra::create(
            chipSpr, this, menu_selector(PaimonGuideChatPopup::onSuggestionChip)
        );
        chipBtn->setUserObject(CCString::create(query.c_str()));
        chipBtn->setID(("flozwer.paimbnails2/guide-chip-" + chipText));
        m_suggestionsMenu->addChild(chipBtn);
    }
    m_suggestionsMenu->updateLayout();
    m_mainLayer->addChild(m_suggestionsMenu, 5);


    auto& mem = PaimonGuideService::get().memory();
    std::string welcome;
    if (mem.size() > 0) {
        if (auto last = mem.lastFunctionalTurn();
            last && (std::time(nullptr) - last->timestamp) < 120)
        {
            auto langId = Localization::get().getCurrentLanguageId();
            welcome = (langId == "spanish")
                ? "Hola otra vez! En que mas te ayudo?"
                : "Hello again! What else can I help with?";
        }
    }
    if (welcome.empty()) {
        welcome = tr("pai.guide.welcome",
            "Hi! I'm Paimon, your guide. Ask me where to configure things!");
    }
    displayMessage(welcome);

    if (m_paimon) {
        auto finalPos = m_paimon->getPosition();
        m_paimon->setPosition({finalPos.x - 80.f, finalPos.y});
        m_paimon->runAction(
            CCEaseBackOut::create(
                CCMoveTo::create(0.45f, finalPos)
            )
        );
    }

    this->setID("paimon-guide-chat-popup"_spr);

    return true;
}

void PaimonGuideChatPopup::onExit() {
    this->unschedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick));
    Popup::onExit();
}

void PaimonGuideChatPopup::keyDown(cocos2d::enumKeyCodes key, double p1) {
    if (key == cocos2d::enumKeyCodes::KEY_Enter
        || key == cocos2d::enumKeyCodes::KEY_NumEnter) {
        trySubmitFromEnter();
        return;
    }
    Popup::keyDown(key, p1);
}

void PaimonGuideChatPopup::trySubmitFromEnter() {
// Ignore duplicate Enter delivery from IME and keyboard dispatch.
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastEnterSubmit < std::chrono::milliseconds(250)) return;
    m_lastEnterSubmit = now;

    onSubmitButton(nullptr);
}

cocos2d::CCNode* PaimonGuideChatPopup::makeBubble(std::string const& wrapped, bool fromUser) {
    auto label = CCLabelBMFont::create(wrapped.c_str(), "chatFont.fnt");
    label->setScale(kLabelScale);
    label->setAlignment(kCCTextAlignmentLeft);

    auto labelSize = label->getScaledContentSize();
    float bubbleW = std::min(labelSize.width + kBubblePadX * 2.f, kChatRowW);
    float bubbleH = std::max(18.f, labelSize.height + kBubblePadY * 2.f);

    auto bg = CCScale9Sprite::create("GJ_square01.png");
    bg->setColor(fromUser ? ccColor3B{45, 90, 60} : ccColor3B{38, 44, 66});
    bg->setOpacity(230);
    bg->setContentSize({bubbleW, bubbleH});
    bg->setAnchorPoint(fromUser ? CCPoint{1.f, 0.f} : CCPoint{0.f, 0.f});

    auto row = CCNode::create();
    row->setContentSize({kChatRowW, bubbleH});
    row->setAnchorPoint({0.f, 0.f});
    bg->setPosition(fromUser ? CCPoint{kChatRowW, 0.f} : CCPoint{0.f, 0.f});
    row->addChild(bg);

// Anchor top-left so typewriter updates do not shift existing lines.
    label->setAnchorPoint({0.f, 1.f});
    label->setPosition({kBubblePadX, bubbleH - kBubblePadY});
    bg->addChild(label);

    m_lastBubbleLabel = label;
    return row;
}

void PaimonGuideChatPopup::relayoutChat() {
    if (!m_scroll) return;
    auto* content = m_scroll->m_contentLayer;

    float total = kChatEdgePad * 2.f;
    auto* children = content->getChildren();
    int count = children ? children->count() : 0;
    for (int i = 0; i < count; ++i) {
        auto* node = static_cast<CCNode*>(children->objectAtIndex(i));
        total += node->getContentSize().height;
        if (i + 1 < count) total += kBubbleGap;
    }

    float contentH = std::max(total, kChatScrollH);
    content->setContentSize({kChatScrollW, contentH});

    float y = contentH - kChatEdgePad;
    for (int i = 0; i < count; ++i) {
        auto* node = static_cast<CCNode*>(children->objectAtIndex(i));
        y -= node->getContentSize().height;
        node->setPosition({kChatEdgePad, y});
        y -= kBubbleGap;
    }

    content->setPositionY(0.f);
}

void PaimonGuideChatPopup::appendUserMessage(std::string const& message) {
    if (!m_scroll) return;

    auto* content = m_scroll->m_contentLayer;
    if (auto* children = content->getChildren();
        children && children->count() >= kMaxBubbles) {
        content->removeChild(static_cast<CCNode*>(children->objectAtIndex(0)));
    }

    auto wrapped = wrapText(message, kWrapChars);
    content->addChild(makeBubble(wrapped, true));
    relayoutChat();
}

void PaimonGuideChatPopup::displayMessage(std::string const& message) {
    if (!m_scroll) return;

// Finish the previous bubble before starting a new one.
    finishTypewriter();

    auto* content = m_scroll->m_contentLayer;
    if (auto* children = content->getChildren();
        children && children->count() >= kMaxBubbles) {
        content->removeChild(static_cast<CCNode*>(children->objectAtIndex(0)));
    }

    auto cleaned = stripGDColorTags(message);
    m_pendingMessage = wrapText(cleaned, kWrapChars);

// Size bubbles for full text; typewriter only controls label content.
    content->addChild(makeBubble(m_pendingMessage, false));
    m_responseLabel = m_lastBubbleLabel;
    m_responseLabel->setString("");
    m_typewriterIndex = 0;
    relayoutChat();

    this->schedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick), 0.04f);

    if (m_paimon) m_paimon->play(AnimatedPaimon::Animation::Talk);
}

void PaimonGuideChatPopup::finishTypewriter() {
    this->unschedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick));
    if (m_responseLabel && m_typewriterIndex < m_pendingMessage.size()) {
        m_responseLabel->setString(m_pendingMessage.c_str());
    }
    m_typewriterIndex = m_pendingMessage.size();
}

void PaimonGuideChatPopup::onTypewriterTick(float /*dt*/) {
    if (!m_responseLabel) return;

    if (m_typewriterIndex >= m_pendingMessage.size()) {
        this->unschedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick));
        return;
    }

    std::size_t advance = 2;
    std::size_t newIdx = std::min(m_typewriterIndex + advance, m_pendingMessage.size());

    auto partial = m_pendingMessage.substr(0, newIdx);
    m_responseLabel->setString(partial.c_str());
    m_typewriterIndex = newIdx;
}

void PaimonGuideChatPopup::updateTopicLabel(std::string const& topicId) {
    if (!m_topicLabel) return;
    if (topicId.empty()) {
        m_topicLabel->setString("");
        return;
    }
    auto langId = Localization::get().getCurrentLanguageId();
    auto name = PopupRegistry::get().displayNameFor(topicId, langId);
    if (name.empty()) {
        m_topicLabel->setString("");
        return;
    }
    bool es = (langId == "spanish");
    auto text = (es ? "Hablando de: " : "Talking about: ") + name;
    m_topicLabel->setString(text.c_str());
}

void PaimonGuideChatPopup::submitQuery(std::string const& query) {
    if (m_input) m_input->setString(query);
    onSubmitButton(nullptr);
}

void PaimonGuideChatPopup::onSubmitButton(cocos2d::CCObject* /*sender*/) {
    if (!m_input) return;
    auto query = m_input->getString();
    if (query.empty()) return;

    m_input->playSendSweep();
    m_input->clear();

    appendUserMessage(query);

// Max mode answers asynchronously; drop the result if the popup is gone.
    geode::WeakRef<PaimonGuideChatPopup> weak = this;
    auto answer = PaimonGuideService::get().ask(query, [weak](GuideAnswer const& ans) {
        Loader::get()->queueInMainThread([weak, ans]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (auto self = weak.lock()) {
                static_cast<PaimonGuideChatPopup*>(self.data())->onMaxReply(ans);
            }
        });
    });
    displayMessage(answer.message);

    updateTopicLabel(answer.matchedIntentId);

    if (m_paimon) {
        switch (answer.animation) {
            case GuideAnimation::Talk:     m_paimon->play(AnimatedPaimon::Animation::Talk); break;
            case GuideAnimation::Surprise: m_paimon->play(AnimatedPaimon::Animation::Surprise); break;
            case GuideAnimation::Wave:     m_paimon->play(AnimatedPaimon::Animation::Wave); break;
            case GuideAnimation::Sleep:    m_paimon->play(AnimatedPaimon::Animation::Sleep); break;
            case GuideAnimation::Point:    m_paimon->play(AnimatedPaimon::Animation::Point); break;
        }
    }

    m_pendingAction = answer.action;
    if (m_takeMeBtn) {
        bool hasAction = static_cast<bool>(m_pendingAction);
        m_takeMeBtn->setVisible(hasAction);

        if (hasAction) {
            m_takeMeBtn->stopAllActions();
            m_takeMeBtn->setScale(0.f);
            m_takeMeBtn->runAction(
                CCEaseElasticOut::create(CCScaleTo::create(0.45f, 0.45f), 0.5f)
            );
            if (m_paimon && m_takeMeBtn) {
                m_paimon->pointAt(m_takeMeBtn, 0.5f);
            }
        }
    }

    if (!answer.recommendations.empty()) {
        setRecommendationChips(answer.recommendations);
    } else {
        restoreDefaultChips();
    }
}

void PaimonGuideChatPopup::setRecommendationChips(
    std::vector<GuideRecommendation> const& recs)
{
    if (!m_suggestionsMenu) return;
    m_suggestionsMenu->removeAllChildren();
    m_pendingRecommendations = recs;

    int idx = 0;
    for (auto const& rec : m_pendingRecommendations) {
        if (rec.label.empty()) continue;
// Truncate long chip labels.
        std::string chipText = rec.label;
        if (chipText.size() > 16) chipText = chipText.substr(0, 14) + "..";

        auto* chipSpr = ButtonSprite::create(
            chipText.c_str(), "bigFont.fnt", "GJ_button_01.png", 0.6f
        );
        chipSpr->setScale(0.40f);
        auto* chipBtn = CCMenuItemSpriteExtra::create(
            chipSpr, this, menu_selector(PaimonGuideChatPopup::onRecommendationChip)
        );
        chipBtn->setTag(idx);
        chipBtn->setID(fmt::format("flozwer.paimbnails2/guide-rec-{}", rec.intentId));
        m_suggestionsMenu->addChild(chipBtn);
        ++idx;
        if (idx >= 4) break;
    }
    m_suggestionsMenu->updateLayout();
}

void PaimonGuideChatPopup::restoreDefaultChips() {
    if (!m_suggestionsMenu) return;
    m_suggestionsMenu->removeAllChildren();
    m_pendingRecommendations.clear();

    auto suggestions = PaimonGuideService::get().getSuggestions();
    for (auto const& [chipText, query] : suggestions) {
        auto* chipSpr = ButtonSprite::create(
            chipText.c_str(), "bigFont.fnt", "GJ_button_05.png", 0.6f
        );
        chipSpr->setScale(0.42f);
        auto* chipBtn = CCMenuItemSpriteExtra::create(
            chipSpr, this, menu_selector(PaimonGuideChatPopup::onSuggestionChip)
        );
        chipBtn->setUserObject(CCString::create(query.c_str()));
        chipBtn->setID(("flozwer.paimbnails2/guide-chip-" + chipText));
        m_suggestionsMenu->addChild(chipBtn);
    }
    m_suggestionsMenu->updateLayout();
}

void PaimonGuideChatPopup::onRecommendationChip(cocos2d::CCObject* sender) {
    auto* btn = typeinfo_cast<CCNode*>(sender);
    if (!btn) return;
    int idx = btn->getTag();
    if (idx < 0 || idx >= static_cast<int>(m_pendingRecommendations.size())) return;

    auto rec = m_pendingRecommendations[static_cast<std::size_t>(idx)];
    if (rec.action) {
// Close the chat before opening the feature.
        m_pendingAction = nullptr;
        this->onClose(nullptr);
        Loader::get()->queueInMainThread([action = rec.action]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (action) action(nullptr);
        });
        return;
    }
// Without an open action, re-ask with the feature name.
    if (!rec.label.empty()) {
        submitQuery(rec.label);
    }
}

void PaimonGuideChatPopup::onTakeMeThere(cocos2d::CCObject* /*sender*/) {
    if (!m_pendingAction) return;

// Capture the action before closing; it targets the current scene.
    auto action = m_pendingAction;
    m_pendingAction = nullptr;

// Run the action after closing; the popup may already be destroyed.
    this->onClose(nullptr);
    Loader::get()->queueInMainThread([action]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (action) action(nullptr);
    });
}

void PaimonGuideChatPopup::onSuggestionChip(cocos2d::CCObject* sender) {
    auto* btn = typeinfo_cast<CCNode*>(sender);
    if (!btn) return;

    auto* obj = btn->getUserObject();
    if (auto* str = typeinfo_cast<CCString*>(obj)) {
        std::string query = str->getCString();
        submitQuery(query);
    }
}

void PaimonGuideChatPopup::onClearChat(cocos2d::CCObject* /*sender*/) {
    finishTypewriter();
    m_responseLabel = nullptr;
    m_lastBubbleLabel = nullptr;
    m_pendingMessage.clear();
    m_typewriterIndex = 0;
    m_pendingAction = nullptr;
    if (m_takeMeBtn) m_takeMeBtn->setVisible(false);
    restoreDefaultChips();
    updateTopicLabel("");

    if (m_scroll) m_scroll->m_contentLayer->removeAllChildren();
    PaimonGuideService::get().resetMemory();

    displayMessage(tr("pai.guide.cleared",
        "Done, fresh chat! What can I help with now?"));
    if (m_paimon) m_paimon->play(AnimatedPaimon::Animation::Wave);
}

void PaimonGuideChatPopup::onHelpButton(cocos2d::CCObject* /*sender*/) {
    submitQuery(tr("pai.guide.help.query", "help"));
}

void PaimonGuideChatPopup::refreshModeButton() {
    bool max = (PaimonGuideService::get().getMode() == GuideMode::Max);
    if (m_modeLabel) {
        m_modeLabel->setString(max ? "MAX" : "ASISTENTE");
    }
    if (m_modeBtn) {
        if (auto* spr = static_cast<cocos2d::CCSprite*>(m_modeBtn->getNormalImage())) {
            spr->setColor(max ? cocos2d::ccc3(90, 200, 255) : cocos2d::ccc3(255, 150, 200));
        }
    }
}

void PaimonGuideChatPopup::onToggleMode(cocos2d::CCObject* /*sender*/) {
    auto& svc = PaimonGuideService::get();
    bool max = (svc.getMode() == GuideMode::Max);

    if (!max && !svc.isMaxAvailable()) {
        bool es = (Localization::get().getCurrentLanguageId() == "spanish");
        displayMessage(es
            ? "El modo <cy>Max</c> no esta disponible por ahora. Me quedo en "
              "<cy>Asistente</c>, que responde al instante con mi conocimiento local."
            : "<cy>Max</c> mode is not available right now. Staying on "
              "<cy>Assistant</c>, which answers instantly from my local knowledge.");
        if (m_paimon) m_paimon->play(AnimatedPaimon::Animation::Talk);
        return;
    }

    svc.setMode(max ? GuideMode::Assistant : GuideMode::Max);
    refreshModeButton();

    auto langId = Localization::get().getCurrentLanguageId();
    bool es = (langId == "spanish");
    std::string msg = max
        ? (es ? "Modo <cy>Asistente</c> activado: respondo al instante con mi conocimiento local."
              : "<cy>Assistant</c> mode on: instant answers from my local knowledge.")
        : (es ? "Modo <cy>Max</c> activado: ahora pienso con PaimonIA y puedo seguir la conversacion mejor."
              : "<cy>Max</c> mode on: I now think with PaimonIA and follow conversations better.");
    displayMessage(msg);
}

void PaimonGuideChatPopup::onMaxReply(GuideAnswer const& ans) {
    finishTypewriter();
    displayMessage(ans.message);
    if (m_paimon) m_paimon->play(AnimatedPaimon::Animation::Talk);
    updateTopicLabel(ans.matchedIntentId);
    restoreDefaultChips();

// Max mode may request a feature action; close first, then open it.
    if (ans.action) {
        auto action = ans.action;
        this->onClose(nullptr);
        Loader::get()->queueInMainThread([action]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (action) action(nullptr);
        });
    }
}

}
