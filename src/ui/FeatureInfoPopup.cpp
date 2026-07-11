#include "FeatureInfoPopup.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../framework/ui/declarative/DeclarativeUI.hpp"

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::ui {

// builds a top-left-anchored CCLabelBMFont through the declarative engine
static cocos2d::CCNode* decLabel(cocos2d::CCNode* parent, std::string const& text,
                                 char const* font, float scale,
                                 cocos2d::ccColor3B color, cocos2d::CCPoint pos) {
    namespace dec = paimon::ui::dec;

    auto attrs = matjson::Value::object();
    attrs["text"]  = text;
    attrs["font"]  = std::string(font);
    attrs["scale"] = scale;

    auto col = matjson::Value::object();
    col["r"] = static_cast<int>(color.r);
    col["g"] = static_cast<int>(color.g);
    col["b"] = static_cast<int>(color.b);
    attrs["color"] = col;

    auto anchor = matjson::Value::object();
    anchor["x"] = 0.f;
    anchor["y"] = 1.f;
    attrs["anchor-point"] = anchor;

    auto position = matjson::Value::object();
    position["x"] = pos.x;
    position["y"] = pos.y;
    attrs["position"] = position;

    return dec::build(dec::Spec{"CCLabelBMFont", "", attrs, {}}, parent);
}

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
    paimon::markDynamicPopup(this);

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

    // compute total content height up-front so the ScrollLayer can size itself
    float lineH = 16.f;
    float titleH = 24.f;
    float gapH = 12.f;
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
        decLabel(content, sec.title, "goldFont.fnt", 0.38f, sec.color, {8.f, y});
        y -= titleH;

        // split into ~55-char lines for manual word wrapping
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

            decLabel(content, line, "chatFont.fnt", 0.52f, {210, 210, 220}, {12.f, y});
            y -= lineH;
        }

        y -= gapH;
    }

    m_scroll->moveToTop();
}

} // namespace paimon::ui
