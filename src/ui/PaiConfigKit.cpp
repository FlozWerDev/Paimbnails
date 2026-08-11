#include "PaiConfigKit.hpp"
#include "../utils/SpriteHelper.hpp"

#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::configkit {

namespace {

// Touch priority for child controls.
int childTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

CCLabelBMFont* makeTitleLabel(char const* text, float maxW, float scale = 0.40f) {
    auto* l = CCLabelBMFont::create(text, "bigFont.fnt");
    l->setAnchorPoint({0.f, 1.f});
    l->setColor(kTitleColor);
    l->limitLabelWidth(maxW, scale, 0.1f);
    return l;
}

// Gray description with automatic wrapping.
CCLabelBMFont* makeDescLabel(char const* text, float wrapW) {
    constexpr float kScale = 0.46f;
    auto* l = CCLabelBMFont::create(text, "chatFont.fnt", wrapW / kScale, kCCTextAlignmentLeft);
    l->setScale(kScale);
    l->setAnchorPoint({0.f, 1.f});
    l->setColor(kDescColor);
    l->setOpacity(230);
    return l;
}

float scaledHeight(CCLabelBMFont* l) {
    return l ? l->getContentSize().height * l->getScale() : 0.f;
}

CCMenu* makeRowMenu(CCNode* row) {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu, 5);
    return menu;
}

// CCObject wrapper for toggle callbacks.
class KitToggleCallback : public CCObject {
public:
    std::function<void(bool)> m_callback;
    CCMenuItemToggler* m_toggler = nullptr;

    static KitToggleCallback* create(std::function<void(bool)> cb) {
        auto* ret = new KitToggleCallback();
        ret->m_callback = std::move(cb);
        ret->autorelease();
        return ret;
    }

    void onToggle(CCObject*) {
// isToggled() still reports the pre-click state here.
        if (m_callback && m_toggler) m_callback(!m_toggler->isToggled());
    }
};

// Slider callback wrapper; Slider::create requires a CCNode target.
class KitSliderCallback : public CCNode {
public:
    std::function<void(double)> m_callback;
    std::function<std::string(double)> m_format;
    double m_min = 0.0, m_max = 1.0;
    Slider* m_slider = nullptr;
    CCLabelBMFont* m_valueLabel = nullptr;

    static KitSliderCallback* create(std::function<void(double)> cb,
                                     std::function<std::string(double)> fmt,
                                     double mn, double mx) {
        auto* ret = new KitSliderCallback();
        ret->init();
        ret->m_callback = std::move(cb);
        ret->m_format = std::move(fmt);
        ret->m_min = mn;
        ret->m_max = mx;
        ret->autorelease();
        return ret;
    }

    void onChanged(CCObject*) {
        if (!m_slider || !m_slider->getThumb()) return;
        double norm = static_cast<double>(m_slider->getThumb()->getValue());
        double mapped = m_min + norm * (m_max - m_min);
        if (m_valueLabel && m_format) {
            m_valueLabel->setString(m_format(mapped).c_str());
        }
        if (m_callback) m_callback(mapped);
    }
};

float normFromValue(double val, double minV, double maxV) {
    if (maxV <= minV) return 0.f;
    return static_cast<float>(std::clamp((val - minV) / (maxV - minV), 0.0, 1.0));
}

CCMenuItemToggler* addStandardToggler(
    CCMenu* menu, bool value, float scale, CCPoint pos,
    std::function<void(bool)> onChange
) {
    auto* cb = KitToggleCallback::create(std::move(onChange));
    auto* tog = CCMenuItemToggler::createWithStandardSprites(
        cb, menu_selector(KitToggleCallback::onToggle), scale);
    cb->m_toggler = tog;
    tog->toggle(value);
    tog->setPosition(pos);
tog->setUserObject(cb); // Keep the wrapper alive with the toggler.
    menu->addChild(tog);
    return tog;
}

}

CCNode* makeToggleRow(
    float width,
    char const* title, char const* desc,
    bool value,
    std::function<void(bool)> onChange,
    CCMenuItemToggler** outToggle
) {
    constexpr float kPad = 6.f;
    constexpr float kTitleH = 14.f;
    float textMaxW = width - 60.f;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = makeDescLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }

    float rowH = kPad + kTitleH + descH + kPad;

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});

    auto* titleLbl = makeTitleLabel(title, textMaxW);
    titleLbl->setPosition({10.f, rowH - kPad});
    row->addChild(titleLbl);

    if (descLbl) {
        descLbl->setPosition({10.f, rowH - kPad - kTitleH - 1.f});
        row->addChild(descLbl);
    }

    auto* menu = makeRowMenu(row);
    auto* tog = addStandardToggler(menu, value, 0.55f, {width - 26.f, rowH / 2.f},
                                   std::move(onChange));
    if (outToggle) *outToggle = tog;
    return row;
}

CCNode* makeSliderRow(
    float width,
    char const* title, char const* desc,
    double value, double minV, double maxV,
    std::function<std::string(double)> format,
    std::function<void(double)> onChange,
    Slider** outSlider,
    cocos2d::CCLabelBMFont** outValue
) {
    constexpr float kPad = 6.f;
    constexpr float kTitleH = 14.f;

// Title/description on the left; value/slider on the right.
    float leftW = width * 0.50f;
    float textMaxW = leftW - 14.f;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = makeDescLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }

    float rowH = std::max(kPad + kTitleH + descH + kPad, 42.f);

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});

    auto* titleLbl = makeTitleLabel(title, textMaxW);
    titleLbl->setPosition({10.f, rowH - kPad});
    row->addChild(titleLbl);

    if (descLbl) {
        descLbl->setPosition({10.f, rowH - kPad - kTitleH - 1.f});
        row->addChild(descLbl);
    }

    float grooveW = width * 0.38f;
    float sliderScale = std::clamp(grooveW / 210.f, 0.35f, 0.85f);
    float sliderCX = width - 14.f - (210.f * sliderScale) / 2.f;
    float sliderCY = rowH / 2.f - 9.f;

    auto* cb = KitSliderCallback::create(std::move(onChange), format, minV, maxV);
    auto* slider = Slider::create(cb, menu_selector(KitSliderCallback::onChanged), sliderScale);
    cb->m_slider = slider;
    slider->setPosition({sliderCX, sliderCY});
    slider->setValue(normFromValue(value, minV, maxV));
slider->setUserObject(cb); // Keep the wrapper alive.
    row->addChild(slider);

    auto* chip = paimon::SpriteHelper::createColorPanel(54.f, 15.f, {0, 0, 0}, 110, 5.f);
    if (chip) {
        chip->setAnchorPoint({0.f, 0.f});
        chip->setPosition({sliderCX - 27.f, sliderCY + 10.f});
        row->addChild(chip);
    }

    auto* valLbl = CCLabelBMFont::create(
        format ? format(value).c_str() : "", "bigFont.fnt");
    valLbl->setAnchorPoint({0.5f, 0.5f});
    valLbl->setColor(kValueColor);
    valLbl->limitLabelWidth(48.f, 0.30f, 0.1f);
    valLbl->setPosition({sliderCX, sliderCY + 17.5f});
    row->addChild(valLbl, 2);
    cb->m_valueLabel = valLbl;

    if (outSlider) *outSlider = slider;
    if (outValue) *outValue = valLbl;
    return row;
}

CCNode* makeSelectRow(
    float width,
    char const* title, char const* desc,
    std::vector<std::string> options, int index,
    std::function<void(int)> onChange,
    cocos2d::CCLabelBMFont** outLabel,
    std::function<void()> onGear
) {
    constexpr float kPad = 6.f;
    constexpr float kTitleH = 14.f;
    constexpr float kZoneW = 150.f;
// Leave room for the gear without overlapping arrows.
    float gearW = onGear ? 24.f : 0.f;
    float textMaxW = width - kZoneW - 24.f - gearW;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = makeDescLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }

    float rowH = std::max(kPad + kTitleH + descH + kPad, 30.f);

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});

    auto* titleLbl = makeTitleLabel(title, textMaxW);
    titleLbl->setPosition({10.f, rowH - kPad});
    row->addChild(titleLbl);

    if (descLbl) {
        descLbl->setPosition({10.f, rowH - kPad - kTitleH - 1.f});
        row->addChild(descLbl);
    }

    float cy = rowH / 2.f;
    float valueCX = width - 14.f - kZoneW / 2.f + 8.f;

    auto* valLbl = CCLabelBMFont::create(
        (index >= 0 && index < static_cast<int>(options.size()))
            ? options[static_cast<size_t>(index)].c_str() : "",
        "bigFont.fnt");
    valLbl->setAnchorPoint({0.5f, 0.5f});
    valLbl->setColor(kValueColor);
    valLbl->limitLabelWidth(kZoneW - 46.f, 0.34f, 0.1f);
    valLbl->setPosition({valueCX, cy});
    row->addChild(valLbl);
    if (outLabel) *outLabel = valLbl;

    auto* menu = makeRowMenu(row);

// Shared state for both arrows.
    auto state = std::make_shared<int>(index);
    auto opts = std::make_shared<std::vector<std::string>>(std::move(options));
    auto cb = std::make_shared<std::function<void(int)>>(std::move(onChange));

    auto cycle = [state, opts, cb, valLbl, kZoneWCopy = kZoneW](int dir) {
        if (opts->empty()) return;
        int n = static_cast<int>(opts->size());
        *state = ((*state + dir) % n + n) % n;
        valLbl->setString((*opts)[static_cast<size_t>(*state)].c_str());
        valLbl->limitLabelWidth(kZoneWCopy - 46.f, 0.34f, 0.1f);
        if (*cb) (*cb)(*state);
    };

    auto* leftSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    if (leftSpr) leftSpr->setScale(0.42f);
    auto* leftBtn = CCMenuItemExt::createSpriteExtra(
        leftSpr, [cycle](CCMenuItemSpriteExtra*) { cycle(-1); });
    leftBtn->setPosition({valueCX - kZoneW / 2.f + 16.f, cy});
    menu->addChild(leftBtn);

    auto* rightSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    if (rightSpr) {
        rightSpr->setScale(0.42f);
        rightSpr->setFlipX(true);
    }
    auto* rightBtn = CCMenuItemExt::createSpriteExtra(
        rightSpr, [cycle](CCMenuItemSpriteExtra*) { cycle(1); });
    rightBtn->setPosition({valueCX + kZoneW / 2.f - 16.f, cy});
    menu->addChild(rightBtn);

    if (onGear) {
        auto* gearSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
        if (gearSpr) gearSpr->setScale(0.32f);
        auto gear = std::make_shared<std::function<void()>>(std::move(onGear));
        auto* gearBtn = CCMenuItemExt::createSpriteExtra(
            gearSpr, [gear](CCMenuItemSpriteExtra*) { if (*gear) (*gear)(); });
        gearBtn->setPosition({valueCX - kZoneW / 2.f - 10.f, cy});
        menu->addChild(gearBtn);
    }

    return row;
}

CCNode* makeButtonRow(
    float width,
    char const* title, char const* desc,
    char const* buttonText,
    std::function<void()> onPress
) {
    constexpr float kPad = 6.f;
    constexpr float kTitleH = 14.f;
    float textMaxW = width - 130.f;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = makeDescLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }

    float rowH = std::max(kPad + kTitleH + descH + kPad, 32.f);

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});

    auto* titleLbl = makeTitleLabel(title, textMaxW);
    titleLbl->setPosition({10.f, rowH - kPad});
    row->addChild(titleLbl);

    if (descLbl) {
        descLbl->setPosition({10.f, rowH - kPad - kTitleH - 1.f});
        row->addChild(descLbl);
    }

    auto* menu = makeRowMenu(row);
    auto* spr = ButtonSprite::create(buttonText, "goldFont.fnt", "GJ_button_04.png", 0.7f);
    if (spr) spr->setScale(0.55f);
    auto* btn = CCMenuItemExt::createSpriteExtra(
        spr, [cb = std::move(onPress)](CCMenuItemSpriteExtra*) { if (cb) cb(); });
    btn->setPosition({width - 14.f - btn->getScaledContentSize().width / 2.f, rowH / 2.f});
    menu->addChild(btn);

    return row;
}

CCNode* makeColorRow(
    float width,
    char const* title, char const* desc,
    ccColor3B value,
    std::function<void(ccColor3B)> onChange,
    CCSprite** outSwatch
) {
    constexpr float kPad = 6.f;
    constexpr float kTitleH = 14.f;
    constexpr float kSwatch = 26.f;
    float textMaxW = width - 90.f;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = makeDescLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }

    float rowH = std::max(kPad + kTitleH + descH + kPad, 34.f);

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});

    auto* titleLbl = makeTitleLabel(title, textMaxW);
    titleLbl->setPosition({10.f, rowH - kPad});
    row->addChild(titleLbl);

    if (descLbl) {
        descLbl->setPosition({10.f, rowH - kPad - kTitleH - 1.f});
        row->addChild(descLbl);
    }

    auto* menu = makeRowMenu(row);

// Tint a white swatch so every color previews accurately.
    auto* swatch = CCSprite::create("square02_001.png");
    if (!swatch) swatch = CCSprite::createWithSpriteFrameName("square02_001.png");
    if (swatch) {
        auto size = swatch->getContentSize();
        if (size.width > 0.f) swatch->setScale(kSwatch / size.width);
        swatch->setColor(value);
        if (outSwatch) *outSwatch = swatch;

// Keep a Ref because the modal callback may outlive a rebuilt row.
        geode::Ref<CCSprite> swatchRef = swatch;
        auto* btn = CCMenuItemExt::createSpriteExtra(
            swatch, [cb = std::move(onChange), swatchRef](CCMenuItemSpriteExtra*) {
                auto cur = swatchRef->getColor();
                auto* picker = geode::ColorPickPopup::create(ccc4(cur.r, cur.g, cur.b, 255));
                if (!picker) return;
                picker->setCallback([cb, swatchRef](ccColor4B const& col) {
                    ccColor3B rgb{col.r, col.g, col.b};
                    swatchRef->setColor(rgb);
                    if (cb) cb(rgb);
                });
                picker->show();
            });
        btn->setPosition({width - 14.f - kSwatch / 2.f, rowH / 2.f});
        menu->addChild(btn);
    }

    return row;
}

CCNode* makeHint(float width, char const* text) {
    auto* lbl = makeDescLabel(text, width - 24.f);
    float rowH = scaledHeight(lbl) + 8.f;

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});

    lbl->setPosition({12.f, rowH - 4.f});
    row->addChild(lbl);
    return row;
}

CCNode* makeCard(
    float width,
    char const* title, cocos2d::ccColor3B accent,
    std::vector<CCNode*> const& rows
) {
    constexpr float kPad = 8.f;
    constexpr float kHeaderH = 18.f;
    constexpr float kGap = 3.f;

    float contentH = 0.f;
    for (auto* r : rows) if (r) contentH += r->getContentSize().height + kGap;
    if (!rows.empty()) contentH -= kGap;

    bool hasTitle = title && title[0] != '\0';
    float cardH = kPad + (hasTitle ? kHeaderH + 4.f : 0.f) + contentH + kPad;

    auto* card = CCNode::create();
    card->setAnchorPoint({0.f, 0.f});
    card->setContentSize({width, cardH});

    auto* panel = paimon::SpriteHelper::createColorPanel(
        width, cardH, kCardColor, kCardAlpha, 7.f);
    if (panel) {
        panel->setAnchorPoint({0.f, 0.f});
        panel->setPosition({0.f, 0.f});
        card->addChild(panel, -1);
    }

    float y = cardH - kPad;
    if (hasTitle) {
// Accent bar and section title.
        auto* bar = paimon::SpriteHelper::createColorPanel(4.f, 13.f, accent, 255, 2.f);
        if (bar) {
            bar->setAnchorPoint({0.f, 0.f});
            bar->setPosition({10.f, y - 14.f});
            card->addChild(bar);
        }
        auto* titleLbl = CCLabelBMFont::create(title, "bigFont.fnt");
        titleLbl->setAnchorPoint({0.f, 1.f});
        titleLbl->setColor(accent);
        titleLbl->limitLabelWidth(width - 40.f, 0.42f, 0.1f);
        titleLbl->setPosition({19.f, y});
        card->addChild(titleLbl);
        y -= kHeaderH + 4.f;
    }

    for (auto* r : rows) {
        if (!r) continue;
        float h = r->getContentSize().height;
        y -= h;
        r->setAnchorPoint({0.f, 0.f});
        r->setPosition({(width - r->getContentSize().width) / 2.f, y});
        card->addChild(r);
        y -= kGap;
    }

    return card;
}

void setHeroStateLabel(CCLabelBMFont* label, bool on) {
    if (!label) return;
    label->setString(on ? "Activado" : "Desactivado");
    label->setColor(on ? kOnColor : kOffColor);
}

CCNode* makeHeroToggle(
    float width,
    char const* title, char const* desc,
    bool value,
    std::function<void(bool)> onChange,
    CCMenuItemToggler** outToggle,
    cocos2d::CCLabelBMFont** outStateLabel
) {
    constexpr float kPad = 7.f;
    constexpr float kTitleH = 17.f;
    float textMaxW = width - 150.f;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = makeDescLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }

    float rowH = std::max(kPad + kTitleH + descH + kPad, 40.f);

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});

    auto* panel = paimon::SpriteHelper::createColorPanel(
        width, rowH, kCardColor, kCardAlpha, 7.f);
    if (panel) {
        panel->setAnchorPoint({0.f, 0.f});
        panel->setPosition({0.f, 0.f});
        row->addChild(panel, -1);
    }

    auto* titleLbl = makeTitleLabel(title, textMaxW, 0.48f);
    titleLbl->setPosition({12.f, rowH - kPad});
    row->addChild(titleLbl);

    if (descLbl) {
        descLbl->setPosition({12.f, rowH - kPad - kTitleH - 1.f});
        row->addChild(descLbl);
    }

    auto* stateLbl = CCLabelBMFont::create(value ? "Activado" : "Desactivado", "bigFont.fnt");
    stateLbl->setAnchorPoint({1.f, 0.5f});
    stateLbl->setScale(0.28f);
    stateLbl->setColor(value ? kOnColor : kOffColor);
    stateLbl->setPosition({width - 52.f, rowH / 2.f});
    row->addChild(stateLbl);

    auto wrapped = [cb = std::move(onChange), stateLbl](bool v) {
        setHeroStateLabel(stateLbl, v);
        if (cb) cb(v);
    };

    auto* menu = makeRowMenu(row);
    auto* tog = addStandardToggler(menu, value, 0.72f, {width - 28.f, rowH / 2.f},
                                   std::move(wrapped));
    if (outToggle) *outToggle = tog;
    if (outStateLabel) *outStateLabel = stateLbl;
    return row;
}

geode::ScrollLayer* makeScrollStack(
    CCSize size,
    std::vector<CCNode*> const& items,
    float gap
) {
    auto* scroll = geode::ScrollLayer::create(size);

    float totalH = 0.f;
    for (auto* n : items) if (n) totalH += n->getContentSize().height + gap;
    if (!items.empty()) totalH -= gap;

    float contentH = std::max(size.height, totalH + 4.f);
    auto* content = scroll->m_contentLayer;
    content->setContentSize({size.width, contentH});

    float y = contentH - 2.f;
    for (auto* n : items) {
        if (!n) continue;
        float h = n->getContentSize().height;
        y -= h;
        n->setAnchorPoint({0.f, 0.f});
        n->setPosition({(size.width - n->getContentSize().width) / 2.f, y});
        content->addChild(n);
        y -= gap;
    }

    scroll->moveToTop();
    return scroll;
}

namespace {

// Shared tab-bar state for restyling after selection changes.
struct TabBarState {
    std::vector<cocos2d::CCNodeRGBA*> panels;
    std::vector<CCLabelBMFont*> labels;
    int selected = 0;

    void restyle() {
        constexpr ccColor3B kSelPanel   = {66, 132, 245};
        constexpr ccColor3B kUnselPanel = {10, 13, 24};
        for (size_t i = 0; i < panels.size(); ++i) {
            bool sel = static_cast<int>(i) == selected;
            if (panels[i]) {
                panels[i]->setColor(sel ? kSelPanel : kUnselPanel);
                panels[i]->setOpacity(sel ? 235 : 120);
            }
            if (i < labels.size() && labels[i]) {
                labels[i]->setColor(sel ? ccColor3B{255, 255, 255} : kDescColor);
                labels[i]->setOpacity(sel ? 255 : 210);
            }
        }
    }
};

}

CCNode* makeTabBar(
    float width,
    std::vector<std::string> const& labels,
    int selected,
    std::function<void(int)> onSelect
) {
    constexpr float kGap = 6.f;
    float const barH = kTabBarHeight;

    auto* bar = CCNode::create();
    bar->setAnchorPoint({0.f, 0.f});
    bar->setContentSize({width, barH});

    auto* menu = makeRowMenu(bar);

    int n = std::max<int>(1, static_cast<int>(labels.size()));
    float tabW = (width - kGap * static_cast<float>(n - 1)) / static_cast<float>(n);

    auto state = std::make_shared<TabBarState>();
    state->selected = std::clamp(selected, 0, n - 1);
    auto cb = std::make_shared<std::function<void(int)>>(std::move(onSelect));

    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        float x0 = static_cast<float>(i) * (tabW + kGap);

// Tab visual: panel plus label.
        auto* holder = CCNode::create();
        holder->setAnchorPoint({0.5f, 0.5f});
        holder->setContentSize({tabW, barH - 4.f});

        auto* panel = paimon::SpriteHelper::createColorPanel(
            tabW, barH - 4.f, kCardColor, 120, 6.f);
        if (panel) {
            panel->setAnchorPoint({0.f, 0.f});
            panel->setPosition({0.f, 0.f});
            holder->addChild(panel, -1);
        }
        state->panels.push_back(panel);

        auto* lbl = CCLabelBMFont::create(labels[static_cast<size_t>(i)].c_str(), "bigFont.fnt");
        lbl->setAnchorPoint({0.5f, 0.5f});
        lbl->limitLabelWidth(tabW - 14.f, 0.38f, 0.1f);
        lbl->setPosition({tabW / 2.f, (barH - 4.f) / 2.f});
        holder->addChild(lbl);
        state->labels.push_back(lbl);

        auto* btn = CCMenuItemExt::createSpriteExtra(
            holder, [state, cb, i](CCMenuItemSpriteExtra*) {
                if (state->selected == i) return;
                state->selected = i;
                state->restyle();
                if (*cb) (*cb)(i);
            });
        btn->setPosition({x0 + tabW / 2.f, barH / 2.f});
        menu->addChild(btn);
    }

    state->restyle();
    return bar;
}

bool queueWheelScroll(geode::ScrollLayer* scrollLayer, float x, float y,
    float& targetY, bool& targetSet, float speed
) {
#if !defined(GEODE_IS_WINDOWS) && !defined(GEODE_IS_MACOS)
    return false;
#else
    if (!scrollLayer || !scrollLayer->getParent()) return false;

    CCPoint mousePos = geode::cocos::getMousePos();
    CCRect scrollRect = scrollLayer->boundingBox();
    scrollRect.origin = scrollLayer->getParent()->convertToWorldSpace(scrollRect.origin);
    if (!scrollRect.containsPoint(mousePos)) return false;

    auto* contentLayer = scrollLayer->m_contentLayer;
    if (!contentLayer) return false;

    float amount = y;
    if (std::abs(amount) < 0.001f) amount = -x;

    float minY = scrollLayer->getContentSize().height - contentLayer->getContentSize().height;
    float maxY = 0.f;
    if (minY > maxY) minY = maxY;

    if (!targetSet) {
        targetY = contentLayer->getPositionY();
        targetSet = true;
    }
    targetY = std::max(minY, std::min(maxY, targetY - amount * speed));
    return true;
#endif
}

void stepWheelScroll(geode::ScrollLayer* scrollLayer,
    float& targetY, bool& targetSet, float dt
) {
    if (!targetSet || !scrollLayer) return;
    auto* contentLayer = scrollLayer->m_contentLayer;
    if (!contentLayer) { targetSet = false; return; }

    float cur = contentLayer->getPositionY();
    float diff = targetY - cur;
    if (std::abs(diff) < 0.5f) {
        contentLayer->setPositionY(targetY);
        targetSet = false;
        return;
    }
    float t = 1.f - std::pow(0.001f, dt);
    contentLayer->setPositionY(cur + diff * t);
}

}
