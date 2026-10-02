#include "PaimonInfoPopup.hpp"
#include "../features/emotes/EmoteRenderer.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include <Geode/ui/MDTextArea.hpp>

using namespace geode::prelude;
using namespace cocos2d;

PaimonInfoPopup* PaimonInfoPopup::create(std::string const& title, std::string const& desc) {
    auto ret = new PaimonInfoPopup();
    if (ret && ret->init(title, desc)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool PaimonInfoPopup::init(std::string const& title, std::string const& desc) {
    if (!PaimonPopup::init(340.f, 240.f)) return false;

    m_infoTitle = title;
    m_infoDesc = desc;
    this->setTitle(title.c_str());

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    auto descLabel = geode::MDTextArea::create(desc, {300.f, 160.f});
    if (descLabel) {
        descLabel->setPosition({cx, content.height / 2.f + 10.f});
        descLabel->setZOrder(10);
        m_mainLayer->addChild(descLabel);

        if (paimon::emotes::EmoteRenderer::hasEmoteSyntax(desc)) {
            if (auto emoteNode = paimon::emotes::EmoteRenderer::renderComment(
                    desc, 18.f, 300.f, "chatFont.fnt", 1.0f)) {
                emoteNode->setAnchorPoint({0.5f, 0.5f});
                emoteNode->setPosition({cx, content.height / 2.f + 10.f});
                emoteNode->setZOrder(11);
                descLabel->setVisible(false);
                m_mainLayer->addChild(emoteNode);
            }
        }
    }

    paimon::markDynamicPopup(this);
    return true;
}
