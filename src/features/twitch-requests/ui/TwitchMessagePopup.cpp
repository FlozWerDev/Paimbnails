#include "TwitchMessagePopup.hpp"

#include "../TwitchRequestManager.hpp"
#include "../TwitchRequestNotify.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextArea.hpp>

#include <fmt/format.h>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

constexpr float kWidth = 380.f;
constexpr float kHeight = 200.f;
constexpr float kPad = 18.f;

constexpr ccColor3B kDesc = {171, 197, 232};

} // namespace

TwitchMessagePopup* TwitchMessagePopup::create(
    LevelRequest const& request,
    std::string levelName,
    std::string author
) {
    auto* ret = new TwitchMessagePopup();
    if (ret->init(request, std::move(levelName), std::move(author))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool TwitchMessagePopup::init(
    LevelRequest const& request,
    std::string levelName,
    std::string author
) {
    if (!Popup::init(kWidth, kHeight)) return false;
    setTitle("Mensaje del request");
    paimon::markDynamicPopup(this);

    auto const content = m_mainLayer->getContentSize();
    float const inner = content.width - kPad * 2.f;
    auto const accent = platformAccent(request.platform);

    // Quien lo mando manda en la ficha: el recado es suyo, no del nivel.
    auto requester = "@" + request.requester;
    if (request.platform == Platform::Web) {
        requester += request.requesterVerified ? " - verificado" : " - sin verificar";
    }
    auto* who = CCLabelBMFont::create(requester.c_str(), "bigFont.fnt");
    who->setAnchorPoint({0.f, 0.5f});
    who->setColor(accent);
    who->limitLabelWidth(inner * 0.62f, 0.6f, 0.28f);
    who->setPosition({kPad, content.height - 46.f});
    m_mainLayer->addChild(who);

    auto* id = CCLabelBMFont::create(
        fmt::format("ID {}", request.levelID).c_str(), "bigFont.fnt");
    id->setAnchorPoint({1.f, 0.5f});
    id->setColor(kDesc);
    id->setScale(0.34f);
    id->setPosition({content.width - kPad, content.height - 46.f});
    m_mainLayer->addChild(id);

    float const levelY = content.height - 68.f;
    auto* name = CCLabelBMFont::create(
        levelName.empty() ? "Nivel sin cargar" : levelName.c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 0.5f});
    name->limitLabelWidth(inner * 0.6f, 0.4f, 0.22f);
    name->setPosition({kPad, levelY});
    m_mainLayer->addChild(name);

    // El creador va en el dorado del juego, como en cualquier celda de nivel.
    if (!author.empty()) {
        float const authorX = kPad + name->getScaledContentSize().width + 7.f;
        auto* by = CCLabelBMFont::create(author.c_str(), "goldFont.fnt");
        by->setAnchorPoint({0.f, 0.5f});
        by->limitLabelWidth(std::max(20.f, content.width - kPad - authorX), 0.34f, 0.2f);
        by->setPosition({authorX, levelY});
        m_mainLayer->addChild(by);
    }

    float const panelTop = levelY - 16.f;
    float const panelHeight = panelTop - 14.f;
    if (auto* panel = paimon::SpriteHelper::createColorPanel(
            inner, panelHeight, {8, 12, 26}, 150, 6.f)) {
        panel->setAnchorPoint({0.f, 0.f});
        panel->setPosition({kPad, 14.f});
        m_mainLayer->addChild(panel);
    }

    // El recado puede traer saltos de linea, asi que va en un scroll: si no
    // cabe se lee arrastrando en vez de salirse del popup.
    auto const note = requestNote(request);
    auto* text = SimpleTextArea::create(note, "chatFont.fnt", 0.55f, inner - 24.f);
    text->setAnchorPoint({0.f, 1.f});
    text->setColor({255, 255, 255, 235});

    CCSize const viewSize = {inner - 12.f, panelHeight - 10.f};
    auto* scroll = ScrollLayer::create(viewSize);
    scroll->setPosition({kPad + 6.f, 19.f});
    m_mainLayer->addChild(scroll);

    float const textHeight = std::max(
        viewSize.height, text->getContentSize().height + 8.f);
    scroll->m_contentLayer->setContentSize({viewSize.width, textHeight});
    text->setPosition({6.f, textHeight - 4.f});
    scroll->m_contentLayer->addChild(text);
    scroll->moveToTop();
    scroll->enableScrollWheel();

    return true;
}

} // namespace paimon::twitch
