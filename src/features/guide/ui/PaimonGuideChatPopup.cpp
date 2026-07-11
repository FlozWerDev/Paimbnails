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

// Popup and chat layout constants.
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

// Manual word-wrap for CCLabelBMFont: split into lines of ~maxChars, respecting words.
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

// Strip GD color tags (<cy>, </c>, ...); CCLabelBMFont renders them literally,
// while responses are formatted for alertLayer. Any other "<...>" is kept.
std::string stripGDColorTags(std::string const& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ) {
        if (in[i] == '<' && i + 2 < in.size()) {
            // </c> case
            if (in[i + 1] == '/' && in[i + 2] == 'c' && i + 3 < in.size() && in[i + 3] == '>') {
                i += 4;
                continue;
            }
            // <cX> case where X is a letter or '_'
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

} // namespace

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

    // --- Left column: Paimon, feature badge and utility buttons ---

    m_paimon = AnimatedPaimon::create(0.5f);
    if (m_paimon) {
        m_paimon->setLively(true);
        m_paimon->setAnchorPoint({0.5f, 0.5f});
        m_paimon->setPosition({50.f, 185.f});
        m_mainLayer->addChild(m_paimon, 5);
        m_paimon->play(AnimatedPaimon::Animation::Wave);
    }

    // Badge: "N funciones - vX.Y.Z" under Paimon.
    {
        int featureCount = static_cast<int>(PopupRegistry::get().entries().size());
        std::string version = "?";
        if (auto* mod = Mod::get()) version = mod->getVersion().toVString(false);

        auto featuresWord = tr("pai.guide.subtitle", "features");
        auto subtitle = fmt::format("{} {}\nv{}", featureCount, featuresWord, version);

        auto badge = CCLabelBMFont::create(subtitle.c_str(), "goldFont.fnt");
        badge->setScale(0.3f);
        badge->setAlignment(kCCTextAlignmentCenter);
        badge->setPosition({50.f, 128.f});
        badge->setID("guide-feature-badge"_spr);
        m_mainLayer->addChild(badge, 5);
    }

    // Utility buttons: clear chat + help.
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

    // --- Chat history: dark frame + scrollable bubble list ---

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

    // --- Input row: animated input + Ask button + Enter hint ---

    constexpr float kInputW = 250.f;

    m_input = AnimatedTextInput::create(kInputW,
        tr("pai.guide.placeholder", "Ask me anything..."));
    if (m_input) {
        m_input->setAnchorPoint({0.f, 0.5f});
        m_input->setPosition({kChatFrameX, kInputY});
        m_mainLayer->addChild(m_input, 5);

        // Enter while the input is focused submits the query.
        geode::WeakRef<PaimonGuideChatPopup> weak = this;
        m_input->setOnSubmit([weak]() {
            // Defer out of the IME callback before mutating the input.
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

    // Small hint under the input: Enter also sends.
    {
        auto hintText = tr("pai.guide.hint.enter", "Enter to send");
        auto hint = CCLabelBMFont::create(hintText.c_str(), "chatFont.fnt");
        hint->setScale(0.35f);
        hint->setOpacity(110);
        hint->setPosition({kChatFrameX + kInputW * 0.5f, kInputY - 20.f});
        hint->setID("guide-enter-hint"_spr);
        m_mainLayer->addChild(hint, 5);
    }

    // --- "Take me there": floats over the chat frame's bottom edge ---

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

    // --- Suggestion chips at the bottom ---

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

    // --- Welcome message ---

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
    // Enter can arrive twice for one press (IME delegate + keyboard dispatcher).
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

    // Anchor top-left so the typewriter fills downward without shifting lines.
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

    // Stack oldest-first from the top; the newest bubble ends near y = 0.
    float y = contentH - kChatEdgePad;
    for (int i = 0; i < count; ++i) {
        auto* node = static_cast<CCNode*>(children->objectAtIndex(i));
        y -= node->getContentSize().height;
        node->setPosition({kChatEdgePad, y});
        y -= kBubbleGap;
    }

    // Scroll to the bottom (newest message).
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

    // Flush the previous bubble to its full text before starting a new one.
    finishTypewriter();

    auto* content = m_scroll->m_contentLayer;
    if (auto* children = content->getChildren();
        children && children->count() >= kMaxBubbles) {
        content->removeChild(static_cast<CCNode*>(children->objectAtIndex(0)));
    }

    // Strip GD tags (CCLabelBMFont can't render them), then wrap.
    auto cleaned = stripGDColorTags(message);
    m_pendingMessage = wrapText(cleaned, kWrapChars);

    // The bubble is sized for the full text; the typewriter only fills the label.
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

    auto answer = PaimonGuideService::get().ask(query);
    displayMessage(answer.message);

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

    // Dynamic chips: related features / near-misses, else default examples.
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
        // Short chip label: truncate long display names.
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
        // Same path as "Take me there": close chat, then open the feature.
        m_pendingAction = nullptr;
        this->onClose(nullptr);
        Loader::get()->queueInMainThread([action = rec.action]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (action) action(nullptr);
        });
        return;
    }
    // No open action: re-ask with the feature name so Paimon explains it.
    if (!rec.label.empty()) {
        submitQuery(rec.label);
    }
}

void PaimonGuideChatPopup::onTakeMeThere(cocos2d::CCObject* /*sender*/) {
    if (!m_pendingAction) return;

    // Capture the action before closing; actions open layers on the current scene, not the popup.
    auto action = m_pendingAction;
    m_pendingAction = nullptr;

    // Queue the action after closing; don't capture self (destroyed by onClose), pass nullptr.
    this->onClose(nullptr);
    Loader::get()->queueInMainThread([action]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (action) action(nullptr);
    });
}

void PaimonGuideChatPopup::onSuggestionChip(cocos2d::CCObject* sender) {
    auto* btn = typeinfo_cast<CCNode*>(sender);
    if (!btn) return;

    // The query was stored as a CCString in the chip's UserObject.
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

    if (m_scroll) m_scroll->m_contentLayer->removeAllChildren();
    PaimonGuideService::get().resetMemory();

    displayMessage(tr("pai.guide.cleared",
        "Done, fresh chat! What can I help with now?"));
    if (m_paimon) m_paimon->play(AnimatedPaimon::Animation::Wave);
}

void PaimonGuideChatPopup::onHelpButton(cocos2d::CCObject* /*sender*/) {
    submitQuery(tr("pai.guide.help.query", "help"));
}

} // namespace paimon::guide
