#include "VersusFriendlyPopup.hpp"
#include "VersusUIKit.hpp"
#include "../data/VersusModes.hpp"
#include "../services/VersusClient.hpp"
#include "../services/VersusSession.hpp"
#include "../services/VersusStore.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/utils/cocos.hpp>

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

constexpr float kPopupW = 420.f;
constexpr float kPopupH = 280.f;
constexpr float kPanelW = 190.f;
constexpr float kPanelH = 130.f;
constexpr int kStepTag = 8200;

constexpr char const* kTargetChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.";

std::string trimmed(std::string text) {
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

} // namespace

VersusFriendlyPopup* VersusFriendlyPopup::create(Mode mode) {
    auto ret = new VersusFriendlyPopup();
    if (ret && ret->init(mode)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VersusFriendlyPopup::init(Mode mode) {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    auto& loc = Localization::get();

    m_mode = mode;
    m_formats = friendlyFormats(mode);

    auto const preferred = VersusStore::get().preferredFormat(mode);
    for (size_t i = 0; i < m_formats.size(); i++) {
        if (m_formats[i]->id == preferred) m_formatIndex = i;
    }

    paimon::markDynamicPopup(this);
    this->setTitle(loc.getString("versus.friendly.title"));

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_menu, 5);

    auto* blurb = CCLabelBMFont::create(loc.getString("versus.friendly.blurb").c_str(),
                                        "chatFont.fnt", kPopupW - 70.f, kCCTextAlignmentCenter);
    blurb->setScale(0.44f);
    blurb->setOpacity(190);
    blurb->setPosition({kPopupW / 2.f, kPopupH - 52.f});
    m_mainLayer->addChild(blurb, 2);

    buildFormatRow();
    buildRoomPanel({14.f, 26.f, kPanelW, kPanelH});
    buildJoinPanel({kPopupW - 14.f - kPanelW, 26.f, kPanelW, kPanelH});

    m_status = ui::makeText("", "chatFont.fnt", 0.44f, {kPopupW / 2.f, 14.f});
    m_mainLayer->addChild(m_status, 3);

    refreshFormat();
    return true;
}

void VersusFriendlyPopup::buildFormatRow() {
    float const y = kPopupH - 86.f;

    for (int i = 0; i < 2; i++) {
        auto* face = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png");
        if (!face) continue;
        face->setScale(0.7f);
        face->setFlipX(i == 1);
        auto* btn = CCMenuItemSpriteExtra::create(face, this,
                                                  menu_selector(VersusFriendlyPopup::onFormatStep));
        btn->setTag(kStepTag + i);
        btn->setPosition({kPopupW / 2.f + (i == 0 ? -130.f : 130.f), y});
        m_menu->addChild(btn);
    }

    m_formatGlyph = paimon::SpriteHelper::safeCreate(formatSprite(*m_formats[m_formatIndex]).c_str());
    if (m_formatGlyph) {
        m_formatGlyph->setPosition({kPopupW / 2.f - 88.f, y});
        m_mainLayer->addChild(m_formatGlyph, 2);
    }

    m_formatName = ui::makeText("", "goldFont.fnt", 0.5f, {kPopupW / 2.f, y + 2.f});
    m_mainLayer->addChild(m_formatName, 2);

    m_formatRule = ui::makeText("", "chatFont.fnt", 0.42f, {kPopupW / 2.f, y - 20.f});
    m_formatRule->setOpacity(180);
    m_mainLayer->addChild(m_formatRule, 2);
}

void VersusFriendlyPopup::buildRoomPanel(CCRect const& area) {
    auto& loc = Localization::get();

    auto* panel = ui::makePanel(area.size, loc.getString("versus.friendly.room"));
    panel->setPosition({area.getMidX(), area.getMidY()});
    m_mainLayer->addChild(panel, 2);

    auto const body = ui::panelBody(area.size);

    auto* hint = CCLabelBMFont::create(loc.getString("versus.friendly.share").c_str(),
                                       "chatFont.fnt", body.size.width - 10.f,
                                       kCCTextAlignmentCenter);
    hint->setScale(0.4f);
    hint->setOpacity(170);
    hint->setPosition({area.size.width / 2.f, body.getMaxY() - 12.f});
    panel->addChild(hint, 1);

    m_codeLabel = ui::makeText("- - - - - -", "bigFont.fnt", 0.72f,
                               {area.size.width / 2.f, body.getMaxY() - 48.f});
    m_codeLabel->setColor(ui::kMuted);
    panel->addChild(m_codeLabel, 1);

    auto* create = ui::makeAction(loc.getString("versus.friendly.create"), 110,
                                  "GJ_button_01.png", 0.44f, this,
                                  menu_selector(VersusFriendlyPopup::onCreate));
    create->setPosition({area.getMidX() - 26.f, area.origin.y + body.origin.y + 16.f});
    m_menu->addChild(create);

    m_copyButton = ui::makeAction(loc.getString("versus.friendly.copy"), 54,
                                  "GJ_button_04.png", 0.44f, this,
                                  menu_selector(VersusFriendlyPopup::onCopy));
    m_copyButton->setPosition({area.getMidX() + 62.f, area.origin.y + body.origin.y + 16.f});
    m_copyButton->setVisible(false);
    m_menu->addChild(m_copyButton);
}

void VersusFriendlyPopup::buildJoinPanel(CCRect const& area) {
    auto& loc = Localization::get();

    auto* panel = ui::makePanel(area.size, loc.getString("versus.friendly.join"));
    panel->setPosition({area.getMidX(), area.getMidY()});
    m_mainLayer->addChild(panel, 2);

    auto const body = ui::panelBody(area.size);

    auto* hint = CCLabelBMFont::create(loc.getString("versus.friendly.target-note").c_str(),
                                       "chatFont.fnt", body.size.width - 10.f,
                                       kCCTextAlignmentCenter);
    hint->setScale(0.4f);
    hint->setOpacity(170);
    hint->setPosition({area.size.width / 2.f, body.getMaxY() - 12.f});
    panel->addChild(hint, 1);

    m_target = TextInput::create(body.size.width - 20.f,
                                 loc.getString("versus.friendly.target-hint").c_str());
    if (m_target) {
        m_target->setFilter(kTargetChars);
        m_target->setMaxCharCount(24);
        m_target->setPosition({area.size.width / 2.f, body.getMaxY() - 48.f});
        panel->addChild(m_target, 1);
    }

    auto* join = ui::makeAction(loc.getString("versus.friendly.enter"), 110, "GJ_button_02.png",
                                0.44f, this, menu_selector(VersusFriendlyPopup::onJoin));
    join->setPosition({area.getMidX(), area.origin.y + body.origin.y + 16.f});
    m_menu->addChild(join);
}

void VersusFriendlyPopup::refreshFormat() {
    auto const& def = *m_formats[m_formatIndex];

    m_formatName->setString(formatName(def).c_str());
    m_formatRule->setString(formatWinCondition(def).c_str());
    m_formatRule->setScale(std::min(0.42f, 220.f /
                                    std::max(1.f, m_formatRule->getContentSize().width)));

    if (m_formatGlyph) {
        if (auto* frame = paimon::SpriteHelper::safeCreate(formatSprite(def).c_str())) {
            m_formatGlyph->setDisplayFrame(frame->displayFrame());
            m_formatGlyph->setScale(30.f / std::max(1.f, m_formatGlyph->getContentSize().width));
        }
    }
}

void VersusFriendlyPopup::setStatus(std::string const& text, bool error) {
    m_status->setString(text.c_str());
    m_status->setColor(error ? ui::kBad : ui::kAccent);
}

void VersusFriendlyPopup::onFormatStep(CCObject* sender) {
    // stepping back is one short of a full lap, which keeps the index unsigned.
    size_t const step = sender->getTag() == kStepTag ? m_formats.size() - 1 : 1;
    m_formatIndex = (m_formatIndex + step) % m_formats.size();
    refreshFormat();
}

void VersusFriendlyPopup::send(std::string const& target) {
    if (m_busy) return;

    m_busy = true;
    setStatus(Localization::get().getString("versus.connecting"));

    auto self = Ref<VersusFriendlyPopup>(this);
    VersusClient::get().challenge(target, m_mode, m_formats[m_formatIndex]->id,
        [self, target](bool ok, ChallengeResult const& result, std::string const& message) {
            if (!self->isRunning()) return;
            self->m_busy = false;

            if (!ok) {
                self->setStatus(message.empty()
                    ? Localization::get().getString("versus.challenge-failed") : message, true);
                return;
            }

            if (!result.code.empty()) {
                self->m_code = result.code;
                self->m_codeLabel->setString(result.code.c_str());
                self->m_codeLabel->setColor(ui::kAccent);
                self->m_copyButton->setVisible(true);
                self->setStatus(Localization::get().getString("versus.friendly.waiting"));
                return;
            }

            // the duel is already open on the server; the hub's watch picks it
            // up and this closes out of the way of the lobby modal.
            self->setStatus(Localization::get().getString("versus.challenge-sent"));
        });
}

void VersusFriendlyPopup::onCreate(CCObject*) {
    send("");
}

void VersusFriendlyPopup::onCopy(CCObject*) {
    if (m_code.empty()) return;
    clipboard::write(m_code);
    PaimonNotify::show(Localization::get().getString("versus.friendly.copied").c_str(),
                       NotificationIcon::Success);
}

void VersusFriendlyPopup::onJoin(CCObject*) {
    auto const target = trimmed(m_target ? m_target->getString() : "");
    if (target.empty()) {
        setStatus(Localization::get().getString("versus.friendly.need-target"), true);
        return;
    }
    send(target);
}

void VersusFriendlyPopup::onEnter() {
    Popup::onEnter();

    // the lobby modal takes over the moment the duel exists, so this steps out
    // instead of sitting behind it.
    auto self = Ref<VersusFriendlyPopup>(this);
    VersusSession::get().addListener(this, [self]() {
        if (self->isRunning() && !VersusSession::get().idle()) self->onClose(nullptr);
    });
}

void VersusFriendlyPopup::onExit() {
    VersusSession::get().removeListener(this);
    Popup::onExit();
}

void VersusFriendlyPopup::onClose(CCObject* sender) {
    paimon::ui::detachGeodeTextInput(m_target);
    m_target = nullptr;
    Popup::onClose(sender);
}

} // namespace paimon::versus
