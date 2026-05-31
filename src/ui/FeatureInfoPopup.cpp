#include "FeatureInfoPopup.hpp"
#include "../utils/SpriteHelper.hpp"

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
    if (!Popup::init(380.f, 260.f)) return false;

    this->setTitle(mainTitle.c_str());
    buildContent(mainTitle, sections);
    return true;
}

void FeatureInfoPopup::buildContent(
    std::string const& /*mainTitle*/,
    std::vector<InfoSection> const& sections
) {
    auto winSize = m_mainLayer->getContentSize();
    float scrollW = winSize.width - 30.f;
    float scrollH = winSize.height - 50.f;
    float scrollX = 15.f;
    float scrollY = 10.f;

    m_scroll = ScrollLayer::create({scrollW, scrollH});
    m_scroll->setPosition({scrollX, scrollY});
    m_mainLayer->addChild(m_scroll);

    // Calcular altura total del contenido
    float lineH = 16.f;    // altura por línea de body
    float titleH = 24.f;   // altura del título de sección
    float gapH = 12.f;     // espacio entre secciones
    float padTop = 8.f;

    float contentH = padTop;
    for (auto const& sec : sections) {
        contentH += titleH;
        int lines = std::max(1, static_cast<int>(sec.body.size() / 50) + 1);
        contentH += lines * lineH;
        contentH += gapH;
    }
    contentH = std::max(contentH, scrollH);

    auto* content = m_scroll->m_contentLayer;
    content->setContentSize({scrollW, contentH});

    float y = contentH - padTop;

    for (auto const& sec : sections) {
        // Título de sección
        auto titleLbl = CCLabelBMFont::create(sec.title.c_str(), "goldFont.fnt");
        titleLbl->setScale(0.38f);
        titleLbl->setColor(sec.color);
        titleLbl->setAnchorPoint({0.f, 1.f});
        titleLbl->setPosition({8.f, y});
        content->addChild(titleLbl);
        y -= titleH;

        // Body — dividir en líneas de ~55 chars para wrapping manual
        std::string remaining = sec.body;
        while (!remaining.empty()) {
            std::string line;
            if (remaining.size() <= 55) {
                line = remaining;
                remaining.clear();
            } else {
                size_t breakAt = remaining.rfind(' ', 55);
                if (breakAt == std::string::npos || breakAt < 20) {
                    breakAt = 55;
                }
                line = remaining.substr(0, breakAt);
                remaining = remaining.substr(breakAt + 1);
            }

            auto lineLbl = CCLabelBMFont::create(line.c_str(), "chatFont.fnt");
            lineLbl->setScale(0.52f);
            lineLbl->setColor({210, 210, 220});
            lineLbl->setAnchorPoint({0.f, 1.f});
            lineLbl->setPosition({12.f, y});
            content->addChild(lineLbl);
            y -= lineH;
        }

        y -= gapH;
    }

    m_scroll->moveToTop();
}

} // namespace paimon::ui
