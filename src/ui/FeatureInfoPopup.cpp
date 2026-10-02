#include "FeatureInfoPopup.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include "PaiConfigKit.hpp"

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::ui {

FeatureInfoPopup* FeatureInfoPopup::create(
    std::string const& mainTitle,
    std::vector<InfoSection> const& sections
) {
    auto ret = new FeatureInfoPopup();
    if (ret && ret->init(mainTitle, sections)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool FeatureInfoPopup::init(
    std::string const& mainTitle,
    std::vector<InfoSection> const& sections
) {
    if (!PaimonPopup::init(380.f, 260.f)) return false;
    paimon::markDynamicPopup(this);

    this->setTitle(mainTitle.c_str());
    buildContent(mainTitle, sections);
    return true;
}

void FeatureInfoPopup::buildContent(
    std::string const& /*maintitle*/,
    std::vector<InfoSection> const& sections
) {
    auto winSize = m_mainLayer->getContentSize();
    float scrollW = winSize.width - 30.f;
    float scrollH = winSize.height - 50.f;
    float scrollX = 15.f;
    float scrollY = 10.f;

    std::vector<CCNode*> cards;
    for (auto const& sec : sections) {
        cards.push_back(configkit::makeCard(scrollW, sec.title.c_str(), sec.color, {
            configkit::makeHint(configkit::cardInnerWidth(scrollW), sec.body.c_str())
        }));
    }
    m_scroll = configkit::makeScrollStack({scrollW, scrollH}, cards);
    m_scroll->setPosition({scrollX, scrollY});
    m_mainLayer->addChild(m_scroll);
}

} // namespace paimon::ui
