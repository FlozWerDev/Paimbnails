#include "PaimonGuideChatPopup.hpp"

#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../services/PaimonGuideService.hpp"
#include "../services/PopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <Geode/utils/general.hpp>

using namespace geode::prelude;

namespace paimon::guide {

namespace {

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 290.f;

constexpr float kSideX = 12.f;
constexpr float kSideY = 12.f;
constexpr float kSideW = 100.f;
constexpr float kSideH = 238.f;
constexpr float kSideCX = kSideX + kSideW * 0.5f;

constexpr float kChatFrameX = 120.f;
constexpr float kChatFrameY = 100.f;
constexpr float kChatFrameW = 308.f;
constexpr float kChatFrameH = 150.f;

constexpr float kScrollbarW  = 8.f;
constexpr float kChatScrollW = kChatFrameW - 8.f - kScrollbarW - 2.f;
constexpr float kChatScrollH = kChatFrameH - 8.f;
constexpr float kChatRowW    = kChatScrollW - 12.f;

constexpr float kBubblePadX     = 8.f;
constexpr float kBubblePadY     = 6.f;
constexpr float kBubbleGap      = 5.f;
constexpr float kChatEdgePad    = 6.f;
constexpr float kLabelScale     = 0.45f;
constexpr std::size_t kWrapChars  = 42;
constexpr std::size_t kMaxBubbles = 30;
constexpr std::size_t kMaxHistory = 20;
constexpr std::size_t kMaxInput   = 120;

constexpr float kInputW = 238.f;
constexpr float kInputY = 74.f;
constexpr float kHintY  = 53.f;
constexpr float kChipsY = 28.f;

std::string tr(char const* key, char const* fallback = "") {
    auto v = Localization::get().getString(key);
    if (v == key && fallback && fallback[0] != '\0') return fallback;
    return v;
}

bool isSpanish() {
    return Localization::get().getCurrentLanguageId() == "spanish";
}

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

// strip gd color tags; cclabelbmfont would render them literally.
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

CCMenuItemSpriteExtra* sideButton(std::string const& text, paimon::ui::Btn skin,
    CCObject* target, SEL_MenuHandler handler) {
    auto* sprite = paimon::ui::makeButtonSprite(text.c_str(), skin, kSideW - 14.f, 0.5f);
    return CCMenuItemSpriteExtra::create(sprite, target, handler);
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
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);

    auto title = tr("pai.guide.title", "Paimon Guide");
    this->setTitle(title.c_str());
    this->addCorners();

    buildSidebar();
    buildChatArea();
    buildInputRow();

    m_suggestionsMenu = CCMenu::create();
    m_suggestionsMenu->setID("guide-suggestions"_spr);
    m_suggestionsMenu->setContentSize({kChatFrameW, 24.f});
    m_suggestionsMenu->setAnchorPoint({0.f, 0.5f});
    m_suggestionsMenu->ignoreAnchorPointForPosition(false);
    m_suggestionsMenu->setPosition({kChatFrameX, kChipsY});
    m_mainLayer->addChild(m_suggestionsMenu, 5);
    restoreDefaultChips();

    auto& mem = PaimonGuideService::get().memory();
    std::string welcome;
    if (mem.size() > 0) {
        if (auto last = mem.lastFunctionalTurn();
            last && (std::time(nullptr) - last->timestamp) < 120)
        {
            welcome = tr("pai.guide.welcome.back", isSpanish()
                ? "Hola otra vez! En que mas te ayudo?"
                : "Hello again! What else can I help with?");
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

void PaimonGuideChatPopup::buildSidebar() {
    auto* panel = paimon::ui::makeInset({kSideW, kSideH}, 70);
    panel->setPosition({kSideX, kSideY});
    panel->setID("guide-sidebar"_spr);
    m_mainLayer->addChild(panel, 1);

    float const top = kSideY + kSideH;

    m_paimon = AnimatedPaimon::create(0.5f);
    if (m_paimon) {
        m_paimon->setLively(true);
        m_paimon->setAnchorPoint({0.5f, 0.5f});
        m_paimon->setPosition({kSideCX, top - 52.f});
        m_mainLayer->addChild(m_paimon, 5);
        m_paimon->play(AnimatedPaimon::Animation::Wave);
    }

    {
        int featureCount = static_cast<int>(PopupRegistry::get().entries().size());
        std::string version = "?";
        // the label already prints the "v"; tovstring would give "vv1.1.0".
        if (auto* mod = Mod::get()) version = mod->getVersion().toNonVString(false);

        auto featuresWord = tr("pai.guide.subtitle", "features");
        auto* badge = paimon::ui::makeTitle(fmt::format("{} {}", featureCount, featuresWord).c_str(),
            kSideW - 12.f, 0.36f);
        badge->setPosition({kSideCX, top - 108.f});
        badge->setID("guide-feature-badge"_spr);
        m_mainLayer->addChild(badge, 5);

        auto* ver = paimon::ui::makeLabel(fmt::format("v{}", version).c_str(), kSideW - 12.f, 0.26f,
            paimon::ui::palette::dim);
        ver->setPosition({kSideCX, top - 121.f});
        m_mainLayer->addChild(ver, 5);
    }

    m_topicLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_topicLabel->setScale(0.4f);
    m_topicLabel->setColor(paimon::ui::palette::info);
    m_topicLabel->setPosition({kSideCX, top - 135.f});
    m_topicLabel->setID("guide-topic-label"_spr);
    m_mainLayer->addChild(m_topicLabel, 5);

    auto* line = paimon::ui::makeDivider(kSideW - 14.f, paimon::ui::palette::gold, 90);
    line->setPosition({kSideCX, top - 146.f});
    m_mainLayer->addChild(line, 5);

    m_sideMenu = CCMenu::create();
    m_sideMenu->setPosition({0.f, 0.f});
    m_sideMenu->setID("guide-util-menu"_spr);
    m_mainLayer->addChild(m_sideMenu, 6);

    m_modeLabel = paimon::ui::makeLabel(tr("pai.guide.mode", "Mode").c_str(), kSideW - 14.f, 0.24f,
        paimon::ui::palette::muted);
    m_modeLabel->setPosition({kSideCX, top - 157.f});
    m_modeLabel->setID("guide-mode-label"_spr);
    m_mainLayer->addChild(m_modeLabel, 5);

    m_modeBtn = sideButton(tr("pai.guide.mode.assistant", "Assistant"), paimon::ui::Btn::Pink,
        this, menu_selector(PaimonGuideChatPopup::onToggleMode));
    m_modeBtn->setID("guide-mode-btn"_spr);
    m_modeBtn->setPosition({kSideCX, top - 172.f});
    m_sideMenu->addChild(m_modeBtn);
    refreshModeButton();

    auto* helpBtn = sideButton(tr("pai.guide.help", "Help"), paimon::ui::Btn::Blue,
        this, menu_selector(PaimonGuideChatPopup::onHelpButton));
    helpBtn->setID("guide-help-btn"_spr);
    helpBtn->setPosition({kSideCX, kSideY + 44.f});
    m_sideMenu->addChild(helpBtn);

    auto* clearBtn = sideButton(tr("pai.guide.clear", "Clear chat"), paimon::ui::Btn::Red,
        this, menu_selector(PaimonGuideChatPopup::onClearChat));
    clearBtn->setID("guide-clear-btn"_spr);
    clearBtn->setPosition({kSideCX, kSideY + 18.f});
    m_sideMenu->addChild(clearBtn);
}

void PaimonGuideChatPopup::buildChatArea() {
    auto chatFrame = paimon::ui::makeInset({kChatFrameW, kChatFrameH}, 210);
    chatFrame->setPosition({kChatFrameX, kChatFrameY});
    chatFrame->setID("guide-chat-frame"_spr);
    m_mainLayer->addChild(chatFrame, 3);

    m_scroll = ScrollLayer::create({kChatScrollW, kChatScrollH});
    m_scroll->setPosition({kChatFrameX + 4.f, kChatFrameY + 4.f});
    m_scroll->setID("guide-chat-scroll"_spr);
    m_mainLayer->addChild(m_scroll, 4);

    if (auto* bar = Scrollbar::create(m_scroll)) {
        bar->setContentSize({kScrollbarW, kChatScrollH - 6.f});
        bar->setPosition({kChatFrameX + kChatFrameW - 4.f - kScrollbarW * 0.5f,
            kChatFrameY + kChatFrameH * 0.5f});
        bar->setID("guide-chat-scrollbar"_spr);
        m_mainLayer->addChild(bar, 6);
    }

    auto* copyMenu = CCMenu::create();
    copyMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(copyMenu, 8);

    auto* copySpr = CCSprite::createWithSpriteFrameName("GJ_copyBtn_001.png");
    if (copySpr) {
        copySpr->setScale(0.42f);
        m_copyBtn = CCMenuItemExt::createSpriteExtra(copySpr, [this](auto*) { this->onCopyReply(); });
        m_copyBtn->setID("guide-copy-btn"_spr);
        m_copyBtn->setPosition({kChatFrameX + kChatFrameW - 10.f, kChatFrameY + kChatFrameH + 11.f});
        copyMenu->addChild(m_copyBtn);
    }

    auto takeMeSpr = paimon::ui::makeButtonSprite(
        tr("pai.guide.take.me.there", "Take me there").c_str(), paimon::ui::Btn::Green, 0.f, 0.55f);
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
}

void PaimonGuideChatPopup::buildInputRow() {
    m_input = AnimatedTextInput::create(kInputW,
        tr("pai.guide.placeholder", "Ask me anything..."));
    if (m_input) {
        m_input->setAnchorPoint({0.f, 0.5f});
        m_input->setPosition({kChatFrameX, kInputY});
        m_mainLayer->addChild(m_input, 5);
        if (auto* inner = m_input->getInput()) inner->setMaxCharCount(static_cast<int>(kMaxInput));

        geode::WeakRef<PaimonGuideChatPopup> weak = this;
        m_input->setOnSubmit([weak]() {
            // defer mutation out of the ime callback.
            Loader::get()->queueInMainThread([weak]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (auto self = weak.lock()) {
                    static_cast<PaimonGuideChatPopup*>(self.data())->trySubmitFromEnter();
                }
            });
        });
        m_input->setCallback([this](std::string const& text) {
            if (m_counterLabel) {
                m_counterLabel->setString(fmt::format("{}/{}", text.size(), kMaxInput).c_str());
                m_counterLabel->setColor(text.size() + 10 >= kMaxInput
                    ? paimon::ui::palette::warning : paimon::ui::palette::dim);
            }
        });
    }

    float const sendX = (kChatFrameX + kInputW + kChatFrameX + kChatFrameW) * 0.5f + 3.f;
    auto sendSpr = paimon::ui::makeButtonSprite(tr("pai.guide.send", "Ask").c_str(),
        paimon::ui::Btn::Green, kChatFrameW - kInputW - 12.f, 0.6f);
    auto sendBtn = CCMenuItemSpriteExtra::create(
        sendSpr, this, menu_selector(PaimonGuideChatPopup::onSubmitButton)
    );
    sendBtn->setID("guide-send-btn"_spr);

    auto sendMenu = CCMenu::create();
    sendMenu->setPosition({0.f, 0.f});
    sendMenu->addChild(sendBtn);
    sendBtn->setPosition({sendX, kInputY});
    m_mainLayer->addChild(sendMenu, 5);

    m_hintLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_hintLabel->setScale(0.38f);
    m_hintLabel->setAnchorPoint({0.f, 0.5f});
    m_hintLabel->setColor(paimon::ui::palette::dim);
    m_hintLabel->setPosition({kChatFrameX + 4.f, kHintY});
    m_hintLabel->setID("guide-enter-hint"_spr);
    m_mainLayer->addChild(m_hintLabel, 5);

    m_counterLabel = CCLabelBMFont::create(fmt::format("0/{}", kMaxInput).c_str(), "chatFont.fnt");
    m_counterLabel->setScale(0.38f);
    m_counterLabel->setAnchorPoint({1.f, 0.5f});
    m_counterLabel->setColor(paimon::ui::palette::dim);
    m_counterLabel->setPosition({kChatFrameX + kInputW - 4.f, kHintY});
    m_mainLayer->addChild(m_counterLabel, 5);

    updateHint();
}

void PaimonGuideChatPopup::onExit() {
    this->unschedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick));
    Popup::onExit();
}

void PaimonGuideChatPopup::keyDown(cocos2d::enumKeyCodes key, double p1) {
    switch (key) {
        case cocos2d::enumKeyCodes::KEY_Enter:
        case cocos2d::enumKeyCodes::KEY_NumEnter:
            trySubmitFromEnter();
            return;
        case cocos2d::enumKeyCodes::KEY_Up:
        case cocos2d::enumKeyCodes::KEY_ArrowUp:
            recallHistory(1);
            return;
        case cocos2d::enumKeyCodes::KEY_Down:
        case cocos2d::enumKeyCodes::KEY_ArrowDown:
            recallHistory(-1);
            return;
        default:
            break;
    }
    Popup::keyDown(key, p1);
}

void PaimonGuideChatPopup::trySubmitFromEnter() {
// ignore duplicate enter delivery from ime and keyboard dispatch.
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastEnterSubmit < std::chrono::milliseconds(250)) return;
    m_lastEnterSubmit = now;

    onSubmitButton(nullptr);
}

// direction 1 walks back to older queries, -1 forward to the draft.
void PaimonGuideChatPopup::recallHistory(int direction) {
    if (!m_input || m_history.empty()) return;
    int const count = static_cast<int>(m_history.size());
    if (m_historyIndex < 0) {
        if (direction < 0) return;
        m_draft = m_input->getString();
    }
    int const next = std::clamp(m_historyIndex + direction, -1, count - 1);
    if (next == m_historyIndex) return;
    m_historyIndex = next;
    m_input->setString(m_historyIndex < 0
        ? m_draft
        : m_history[static_cast<std::size_t>(count - 1 - m_historyIndex)]);
}

void PaimonGuideChatPopup::updateHint() {
    if (!m_hintLabel) return;
    bool const typing = m_responseLabel && m_typewriterIndex < m_pendingMessage.size();
    auto text = typing
        ? tr("pai.guide.typing", "Paimon is typing...")
        : fmt::format("{}  -  {}", tr("pai.guide.hint.enter", "Enter to send"),
            tr("pai.guide.hint.history", "Up/Down: history"));
    m_hintLabel->setString(text.c_str());
    m_hintLabel->setColor(typing ? paimon::ui::palette::info : paimon::ui::palette::dim);
    m_hintLabel->limitLabelWidth(kInputW - 50.f, 0.38f, 0.2f);
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

// anchor top-left so typewriter updates do not shift existing lines.
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

// finish the previous bubble before starting a new one.
    finishTypewriter();

    auto* content = m_scroll->m_contentLayer;
    if (auto* children = content->getChildren();
        children && children->count() >= kMaxBubbles) {
        content->removeChild(static_cast<CCNode*>(children->objectAtIndex(0)));
    }

    m_lastReply = stripGDColorTags(message);
    m_pendingMessage = wrapText(m_lastReply, kWrapChars);

// size bubbles for full text; typewriter only controls label content.
    content->addChild(makeBubble(m_pendingMessage, false));
    m_responseLabel = m_lastBubbleLabel;
    m_responseLabel->setString("");
    m_typewriterIndex = 0;
    relayoutChat();

    this->schedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick), 0.04f);
    updateHint();

    if (m_paimon) m_paimon->play(AnimatedPaimon::Animation::Talk);
}

void PaimonGuideChatPopup::finishTypewriter() {
    this->unschedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick));
    if (m_responseLabel && m_typewriterIndex < m_pendingMessage.size()) {
        m_responseLabel->setString(m_pendingMessage.c_str());
    }
    m_typewriterIndex = m_pendingMessage.size();
    updateHint();
}

void PaimonGuideChatPopup::onTypewriterTick(float /*dt*/) {
    if (!m_responseLabel) return;

    if (m_typewriterIndex >= m_pendingMessage.size()) {
        this->unschedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick));
        updateHint();
        return;
    }

    std::size_t advance = 2;
    std::size_t newIdx = std::min(m_typewriterIndex + advance, m_pendingMessage.size());

    auto partial = m_pendingMessage.substr(0, newIdx);
    m_responseLabel->setString(partial.c_str());
    m_typewriterIndex = newIdx;
    if (m_typewriterIndex >= m_pendingMessage.size()) updateHint();
}

void PaimonGuideChatPopup::updateTopicLabel(std::string const& topicId) {
    if (!m_topicLabel) return;
    std::string name;
    if (!topicId.empty()) {
        name = PopupRegistry::get().displayNameFor(topicId, Localization::get().getCurrentLanguageId());
    }
    if (name.empty()) {
        m_topicLabel->setString("");
        return;
    }
    auto text = fmt::format(fmt::runtime(tr("pai.guide.topic", "Topic: {}")), name);
    m_topicLabel->setString(text.c_str());
    m_topicLabel->limitLabelWidth(kSideW - 12.f, 0.4f, 0.2f);
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
    if (m_counterLabel) m_counterLabel->setString(fmt::format("0/{}", kMaxInput).c_str());

    if (m_history.empty() || m_history.back() != query) {
        m_history.push_back(query);
        if (m_history.size() > kMaxHistory) m_history.erase(m_history.begin());
    }
    m_historyIndex = -1;
    m_draft.clear();

    appendUserMessage(query);

// max mode answers asynchronously; drop the result if the popup is gone.
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
                CCEaseElasticOut::create(CCScaleTo::create(0.45f, 1.f), 0.5f)
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

void PaimonGuideChatPopup::addChipRow(
    std::vector<std::pair<std::string, CCObject*>> const& chips,
    char const* texture, SEL_MenuHandler handler)
{
    if (!m_suggestionsMenu) return;
    m_suggestionsMenu->removeAllChildren();
    if (chips.empty()) return;

    constexpr float kGap = 4.f;
    constexpr float kBaseScale = 0.5f;
    float const avail = m_suggestionsMenu->getContentSize().width;

    std::vector<ButtonSprite*> sprites;
    float total = 0.f;
    for (auto const& [text, _] : chips) {
        auto* spr = paimon::ui::makeButtonSprite(text.c_str(), texture, 0.f, kBaseScale, "bigFont.fnt");
        if (!spr) continue;
        total += spr->getScaledContentSize().width;
        sprites.push_back(spr);
    }
    if (sprites.empty()) return;
    total += kGap * static_cast<float>(sprites.size() - 1);

    float const fit = std::clamp(avail / std::max(total, 1.f), 0.55f, 1.f);
    float rowW = 0.f;
    for (auto* spr : sprites) {
        spr->setScale(kBaseScale * fit);
        rowW += spr->getScaledContentSize().width;
    }
    rowW += kGap * fit * static_cast<float>(sprites.size() - 1);

    float x = (avail - rowW) * 0.5f;
    float const y = m_suggestionsMenu->getContentSize().height * 0.5f;
    for (std::size_t i = 0; i < sprites.size(); ++i) {
        auto* spr = sprites[i];
        float const w = spr->getScaledContentSize().width;
        // anything past the row edge would collide with the popup border.
        if (x + w > avail + 0.5f) break;
        auto* chipBtn = CCMenuItemSpriteExtra::create(spr, this, handler);
        auto* payload = chips[i].second;
        if (auto* str = typeinfo_cast<CCString*>(payload)) chipBtn->setUserObject(str);
        else if (auto* tag = typeinfo_cast<CCInteger*>(payload)) chipBtn->setTag(tag->getValue());
        chipBtn->setID(fmt::format("flozwer.paimbnails2/guide-chip-{}", i));
        chipBtn->setPosition({x + w * 0.5f, y});
        m_suggestionsMenu->addChild(chipBtn);
        x += w + kGap * fit;
    }
}

void PaimonGuideChatPopup::setRecommendationChips(
    std::vector<GuideRecommendation> const& recs)
{
    m_pendingRecommendations = recs;

    std::vector<std::pair<std::string, CCObject*>> chips;
    int idx = 0;
    for (auto const& rec : m_pendingRecommendations) {
        if (rec.label.empty()) { ++idx; continue; }
// truncate long chip labels.
        std::string chipText = rec.label;
        if (chipText.size() > 16) chipText = chipText.substr(0, 14) + "..";
        chips.push_back({chipText, CCInteger::create(idx)});
        ++idx;
        if (chips.size() >= 4) break;
    }
    addChipRow(chips, "GJ_button_01.png", menu_selector(PaimonGuideChatPopup::onRecommendationChip));
}

void PaimonGuideChatPopup::restoreDefaultChips() {
    m_pendingRecommendations.clear();

    std::vector<std::pair<std::string, CCObject*>> chips;
    for (auto const& [chipText, query] : PaimonGuideService::get().getSuggestions()) {
        chips.push_back({chipText, CCString::create(query.c_str())});
    }
    addChipRow(chips, "GJ_button_05.png", menu_selector(PaimonGuideChatPopup::onSuggestionChip));
}

void PaimonGuideChatPopup::onRecommendationChip(cocos2d::CCObject* sender) {
    auto* btn = typeinfo_cast<CCNode*>(sender);
    if (!btn) return;
    int idx = btn->getTag();
    if (idx < 0 || idx >= static_cast<int>(m_pendingRecommendations.size())) return;

    auto rec = m_pendingRecommendations[static_cast<std::size_t>(idx)];
    if (rec.action) {
// close the chat before opening the feature.
        m_pendingAction = nullptr;
        this->onClose(nullptr);
        Loader::get()->queueInMainThread([action = rec.action]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (action) action(nullptr);
        });
        return;
    }
// without an open action, re-ask with the feature name.
    if (!rec.label.empty()) {
        submitQuery(rec.label);
    }
}

void PaimonGuideChatPopup::onTakeMeThere(cocos2d::CCObject* /*sender*/) {
    if (!m_pendingAction) return;

// capture the action before closing; it targets the current scene.
    auto action = m_pendingAction;
    m_pendingAction = nullptr;

// run the action after closing; the popup may already be destroyed.
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

void PaimonGuideChatPopup::onCopyReply() {
    if (m_lastReply.empty()) return;
    geode::utils::clipboard::write(m_lastReply);
    PaimonNotify::show(tr("pai.guide.copied", "Reply copied"), NotificationIcon::Success);
    if (m_copyBtn) {
        m_copyBtn->stopAllActions();
        m_copyBtn->setScale(1.25f);
        m_copyBtn->runAction(CCEaseBackOut::create(CCScaleTo::create(0.2f, 1.f)));
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
    if (!m_modeBtn) return;
    paimon::ui::setButtonSkin(m_modeBtn, max ? paimon::ui::Btn::Cyan : paimon::ui::Btn::Pink);
    if (auto* spr = typeinfo_cast<ButtonSprite*>(m_modeBtn->getNormalImage())) {
        auto text = max ? tr("pai.guide.mode.max", "Max") : tr("pai.guide.mode.assistant", "Assistant");
        spr->setString(text.c_str());
        if (spr->m_label) spr->m_label->limitLabelWidth(kSideW - 30.f, 0.8f, 0.2f);
    }
}

void PaimonGuideChatPopup::onToggleMode(cocos2d::CCObject* /*sender*/) {
    auto& svc = PaimonGuideService::get();
    bool max = (svc.getMode() == GuideMode::Max);

    if (!max && !svc.isMaxAvailable()) {
        displayMessage(isSpanish()
            ? "El modo <cy>Max</c> no esta disponible por ahora. Me quedo en "
              "<cy>Asistente</c>, que responde al instante con mi conocimiento local."
            : "<cy>Max</c> mode is not available right now. Staying on "
              "<cy>Assistant</c>, which answers instantly from my local knowledge.");
        if (m_paimon) m_paimon->play(AnimatedPaimon::Animation::Talk);
        return;
    }

    svc.setMode(max ? GuideMode::Assistant : GuideMode::Max);
    refreshModeButton();

    bool es = isSpanish();
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

// max mode may request a feature action; close first, then open it.
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
