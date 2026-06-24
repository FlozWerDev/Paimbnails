#include "PaimonGuideChatPopup.hpp"

#include "../services/PaimonGuideService.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::guide {

namespace {

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
    if (!Popup::init(380.f, 240.f)) return false;

    auto title = tr("pai.guide.title", "Paimon Guide");
    this->setTitle(title.c_str());

    auto layerSize = m_mainLayer->getContentSize();

    // Paimon on the left
    m_paimon = AnimatedPaimon::create(0.55f);
    if (m_paimon) {
        m_paimon->setLively(true);
        m_paimon->setAnchorPoint({0.5f, 0.5f});
        m_paimon->setPosition({50.f, 130.f});
        m_mainLayer->addChild(m_paimon, 5);
        m_paimon->play(AnimatedPaimon::Animation::Wave);
    }

    constexpr float kBgW = 250.f;
    constexpr float kBgH = 90.f;
    m_responseBg = CCScale9Sprite::create("GJ_square01.png");
    m_responseBg->setColor({30, 35, 50});
    m_responseBg->setOpacity(220);
    m_responseBg->setContentSize({kBgW, kBgH});
    m_responseBg->setAnchorPoint({0.f, 0.5f});
    m_responseBg->setPosition({110.f, 155.f});
    m_mainLayer->addChild(m_responseBg, 4);

    m_responseLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_responseLabel->setScale(0.50f);
    m_responseLabel->setAnchorPoint({0.f, 1.f});
    m_responseLabel->setAlignment(kCCTextAlignmentLeft);
    m_responseLabel->setPosition({10.f, kBgH - 8.f});
    m_responseBg->addChild(m_responseLabel);

    constexpr float kInputW = 195.f;
    constexpr float kInputY = 52.f;

    m_input = AnimatedTextInput::create(kInputW,
        tr("pai.guide.placeholder", "Ask me anything..."));
    if (m_input) {
        m_input->setAnchorPoint({0.f, 0.5f});
        m_input->setPosition({88.f, kInputY});
        m_mainLayer->addChild(m_input, 5);
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
    sendMenu->setContentSize({60.f, 40.f});
    // sendBtn placed right of the input (which ends at x=283) at x=325.
    sendMenu->setPosition({325.f, kInputY});
    sendMenu->addChild(sendBtn);
    sendBtn->setPosition({0.f, 0.f});
    m_mainLayer->addChild(sendMenu, 5);

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
    // Centered below responseBg, above the input.
    m_takeMeMenu->setPosition({235.f, 95.f});
    m_takeMeMenu->addChild(m_takeMeBtn);
    m_takeMeBtn->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_takeMeMenu, 5);

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
    bool isReturning = false;
    if (mem.size() > 0) {
        if (auto last = mem.lastFunctionalTurn();
            last && (std::time(nullptr) - last->timestamp) < 120)
        {
            isReturning = true;
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
    (void)isReturning;

    if (m_paimon) {
        auto finalPos = m_paimon->getPosition();
        m_paimon->setPosition({finalPos.x - 80.f, finalPos.y});
        m_paimon->runAction(
            CCEaseBackOut::create(
                CCMoveTo::create(0.45f, finalPos)
            )
        );
    }
    if (m_responseBg) {
        m_responseBg->setOpacity(0);
        m_responseBg->runAction(CCFadeTo::create(0.35f, 220));
    }

    this->setID("paimon-guide-chat-popup"_spr);
    if (m_responseBg)    m_responseBg->setID("guide-response-bg"_spr);
    if (m_responseLabel) m_responseLabel->setID("guide-response-label"_spr);

    return true;
}

void PaimonGuideChatPopup::onExit() {
    this->unschedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick));
    Popup::onExit();
}

void PaimonGuideChatPopup::displayMessage(std::string const& message) {
    if (!m_responseLabel) return;

    this->unschedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick));

    // Strip GD tags (CCLabelBMFont can't render them), then wrap to ~36 chars/line.
    auto cleaned = stripGDColorTags(message);
    m_pendingMessage = wrapText(cleaned, 36);
    m_typewriterIndex = 0;
    m_responseLabel->setString("");

    this->schedule(schedule_selector(PaimonGuideChatPopup::onTypewriterTick), 0.04f);

    if (m_paimon) m_paimon->play(AnimatedPaimon::Animation::Talk);
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

} // namespace paimon::guide
