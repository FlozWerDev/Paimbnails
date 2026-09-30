#include "IconMakerKit.hpp"

#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::icon_maker::gdkit {

namespace {

// child-control priority, compatible with geode popups' force-priority.
int childTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

CCMenu* rowMenu(CCNode* row) {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu, 5);
    return menu;
}

CCLabelBMFont* titleLabel(char const* text, float maxW, float scale = 0.42f) {
    auto* l = CCLabelBMFont::create(text, "bigFont.fnt");
    l->setAnchorPoint({0.f, 1.f});
    l->setColor(kTitleColor);
    l->limitLabelWidth(maxW, scale, 0.1f);
    return l;
}

CCLabelBMFont* descLabel(char const* text, float wrapW) {
    constexpr float kScale = 0.44f;
    auto* l = CCLabelBMFont::create(text, "chatFont.fnt", wrapW / kScale, kCCTextAlignmentLeft);
    l->setScale(kScale);
    l->setAnchorPoint({0.f, 1.f});
    l->setColor(kDescColor);
    return l;
}

float scaledHeight(CCLabelBMFont* l) {
    return l ? l->getContentSize().height * l->getScale() : 0.f;
}

// game-plated row, pre-sized.
CCNode* makeRow(float width, float height) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    if (auto* plate = makePlate(width, height)) {
        plate->setPosition({0.f, 0.f});
        row->addChild(plate, -1);
    }
    return row;
}

// ccobject wrapper for the vanilla toggle callback.
class ToggleCallback : public CCObject {
public:
    std::function<void(bool)> m_callback;
    CCMenuItemToggler* m_toggler = nullptr;

    static ToggleCallback* create(std::function<void(bool)> cb) {
        auto* ret = new ToggleCallback();
        ret->m_callback = std::move(cb);
        ret->autorelease();
        return ret;
    }

    void onToggle(CCObject*) {
        // istoggled() reports the state before the click.
        if (m_callback && m_toggler) m_callback(!m_toggler->isToggled());
    }
};

// slider::create requires a ccnode* target.
class SliderCallback : public CCNode {
public:
    std::function<void(double)> m_callback;
    std::function<std::string(double)> m_format;
    double m_min = 0.0, m_max = 1.0;
    Slider* m_slider = nullptr;
    CCLabelBMFont* m_valueLabel = nullptr;

    static SliderCallback* create(std::function<void(double)> cb,
                                  std::function<std::string(double)> fmt,
                                  double mn, double mx) {
        auto* ret = new SliderCallback();
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

std::string formatNumber(double value, int decimals) {
    if (decimals <= 0) return fmt::format("{}", static_cast<long long>(std::lround(value)));
    return fmt::format("{:.{}f}", value, decimals);
}

// makenumberrow shared state: slider, box and arrows write the same value.
struct NumberState {
    Slider* slider = nullptr;
    geode::TextInput* input = nullptr;
    double value = 0.0;
    double minV = 0.0;
    double maxV = 1.0;
    double step = 1.0;
    int decimals = 2;
    std::function<void(double)> onChange;

    void apply(double v, bool fromInput) {
        value = std::clamp(v, minV, maxV);
        if (slider) slider->setValue(normFromValue(value, minV, maxV));
        if (input && !fromInput) input->setString(formatNumber(value, decimals));
        if (onChange) onChange(value);
    }
};


}  // anonymous namespace


CCNode* makeWindow(CCSize size) {
    if (auto* s9 = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png")) {
        s9->setContentSize(size);
        s9->setAnchorPoint({0.f, 0.f});
        return s9;
    }
    return paimon::SpriteHelper::createColorPanel(
        size.width, size.height, {14, 24, 52}, 225, 8.f);
}

CCNode* makePlate(float width, float height, ccColor3B color, GLubyte opacity) {
    if (auto* s9 = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png")) {
        s9->setContentSize({width, height});
        s9->setAnchorPoint({0.f, 0.f});
        s9->setColor(color);
        s9->setOpacity(opacity);
        return s9;
    }
    auto* fallback = paimon::SpriteHelper::createColorPanel(
        width, height, {40, 58, 96}, opacity, 5.f);
    if (fallback) fallback->setAnchorPoint({0.f, 0.f});
    return fallback;
}

CCSprite* makeTabFace(char const* text, bool selected, float maxW, float maxH) {
    CCSprite* spr = TabButtonSprite::create(
        text, selected ? TabBaseColor::Selected : TabBaseColor::Unselected);
    if (!spr) {
        spr = ButtonSprite::create(text, "bigFont.fnt",
            selected ? "GJ_button_02.png" : "GJ_button_04.png", 0.8f);
    }
    if (!spr) return nullptr;

    auto const size = spr->getContentSize();
    float const scale = std::min(maxW / std::max(size.width, 1.f),
                                 maxH / std::max(size.height, 1.f));
    spr->setScale(std::clamp(scale, 0.2f, 1.f));
    return spr;
}

CCMenuItemSpriteExtra* makeButton(char const* text, char const* sprite,
                                  float scale, std::function<void()> onPress) {
    auto* spr = ButtonSprite::create(text, "goldFont.fnt", sprite, 0.8f);
    if (!spr) return nullptr;
    spr->setScale(scale);
    return CCMenuItemExt::createSpriteExtra(spr,
        [cb = std::move(onPress)](CCMenuItemSpriteExtra*) { if (cb) cb(); });
}


CCNode* makeToggleRow(
    float width,
    char const* title, char const* desc,
    bool value,
    std::function<void(bool)> onChange,
    CCMenuItemToggler** outToggle
) {
    constexpr float kPad = 7.f;
    constexpr float kTitleH = 15.f;
    float const textMaxW = width - 58.f;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = descLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }

    float const rowH = std::max(kPad + kTitleH + descH + kPad, 34.f);
    auto* row = makeRow(width, rowH);

    auto* titleLbl = titleLabel(title, textMaxW);
    titleLbl->setPosition({11.f, rowH - kPad});
    row->addChild(titleLbl);

    if (descLbl) {
        descLbl->setPosition({11.f, rowH - kPad - kTitleH});
        row->addChild(descLbl);
    }

    auto* menu = rowMenu(row);
    auto* cb = ToggleCallback::create(std::move(onChange));
    auto* tog = CCMenuItemToggler::createWithStandardSprites(
        cb, menu_selector(ToggleCallback::onToggle), 0.62f);
    cb->m_toggler = tog;
    tog->toggle(value);
    tog->setPosition({width - 24.f, rowH / 2.f});
    tog->setUserObject(cb);
    menu->addChild(tog);

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
    CCLabelBMFont** outValue
) {
    constexpr float kPad = 7.f;
    constexpr float kTitleH = 15.f;
    constexpr float kSliderH = 22.f;
    float const textMaxW = width - 90.f;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = descLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }

    float const rowH = kPad + kTitleH + descH + kSliderH + kPad;
    auto* row = makeRow(width, rowH);

    auto* titleLbl = titleLabel(title, textMaxW);
    titleLbl->setPosition({11.f, rowH - kPad});
    row->addChild(titleLbl);

    if (descLbl) {
        descLbl->setPosition({11.f, rowH - kPad - kTitleH});
        row->addChild(descLbl);
    }

    // value sits top-right in gold, always visible.
    auto* valLbl = CCLabelBMFont::create(
        format ? format(value).c_str() : "", "goldFont.fnt");
    valLbl->setAnchorPoint({1.f, 1.f});
    valLbl->setColor(kValueColor);
    valLbl->limitLabelWidth(66.f, 0.46f, 0.1f);
    valLbl->setPosition({width - 11.f, rowH - kPad + 1.f});
    row->addChild(valLbl);

    // full-row slider.
    float const grooveW = width - 26.f;
    float const sliderScale = std::clamp(grooveW / 210.f, 0.3f, 1.f);
    float const sliderCY = kPad + kSliderH / 2.f - 2.f;

    auto* cb = SliderCallback::create(std::move(onChange), format, minV, maxV);
    auto* slider = Slider::create(cb, menu_selector(SliderCallback::onChanged), sliderScale);
    slider->setPosition({width / 2.f, sliderCY});
    slider->setValue(normFromValue(value, minV, maxV));
    slider->setUserObject(cb);
    cb->m_slider = slider;
    cb->m_valueLabel = valLbl;
    row->addChild(slider);

    if (outSlider) *outSlider = slider;
    if (outValue) *outValue = valLbl;
    return row;
}

CCNode* makeNumberRow(
    float width,
    char const* title, char const* desc,
    double value, double minV, double maxV, double step, int decimals,
    std::function<void(double)> onChange
) {
    constexpr float kPad = 7.f;
    constexpr float kTitleH = 17.f;
    constexpr float kSliderH = 22.f;
    constexpr float kStepW = 15.f;
    constexpr float kInputW = 44.f;
    constexpr float kInputScale = 0.62f;

    float const clusterW = kStepW * 2.f + kInputW + 12.f;
    float const textMaxW = std::max(40.f, width - clusterW - 24.f);

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = descLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }

    float const rowH = kPad + kTitleH + descH + kSliderH + kPad;
    auto* row = makeRow(width, rowH);
    auto* menu = rowMenu(row);

    auto* titleLbl = titleLabel(title, textMaxW);
    titleLbl->setPosition({11.f, rowH - kPad});
    row->addChild(titleLbl);

    if (descLbl) {
        descLbl->setPosition({11.f, rowH - kPad - kTitleH});
        row->addChild(descLbl);
    }

    auto state = std::make_shared<NumberState>();
    state->value = std::clamp(value, minV, maxV);
    state->minV = minV;
    state->maxV = maxV;
    state->step = step;
    state->decimals = decimals;
    state->onChange = std::move(onChange);

    float const clusterCY = rowH - kPad - kTitleH / 2.f;
    float x = width - 11.f;

    auto stepButton = [&](char const* text, double direction) {
        auto* holder = CCNode::create();
        holder->setAnchorPoint({0.5f, 0.5f});
        holder->setContentSize({kStepW, kStepW});
        if (auto* plate = paimon::SpriteHelper::createColorPanel(
                kStepW, kStepW, {12, 20, 44}, 190, 3.f)) {
            plate->setAnchorPoint({0.f, 0.f});
            holder->addChild(plate, -1);
        }
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setAnchorPoint({0.5f, 0.5f});
        label->limitLabelWidth(kStepW - 5.f, 0.36f, 0.14f);
        label->setPosition({kStepW / 2.f, kStepW / 2.f});
        holder->addChild(label);

        auto* btn = CCMenuItemExt::createSpriteExtra(holder,
            [state, direction](CCMenuItemSpriteExtra*) {
                state->apply(state->value + direction * state->step, false);
            });
        x -= kStepW / 2.f;
        btn->setPosition({x, clusterCY});
        menu->addChild(btn);
        x -= kStepW / 2.f + 4.f;
    };

    stepButton("+", 1.0);

    if (auto* input = TextInput::create(kInputW / kInputScale, "0", "bigFont.fnt")) {
        input->setFilter("-0123456789.");
        input->setMaxCharCount(8);
        input->setScale(kInputScale);
        input->setString(formatNumber(state->value, decimals).c_str());
        input->setCallback([state](std::string const& text) {
            char* end = nullptr;
            double const parsed = std::strtod(text.c_str(), &end);
            // typing passes through text that isn't a number yet.
            if (end == text.c_str()) return;
            state->apply(parsed, true);
        });
        x -= kInputW / 2.f;
        input->setPosition({x, clusterCY});
        row->addChild(input, 6);
        x -= kInputW / 2.f + 4.f;
        state->input = input;
    }

    stepButton("-", -1.0);

    float const grooveW = width - 26.f;
    float const sliderScale = std::clamp(grooveW / 210.f, 0.3f, 1.f);

    auto* cb = SliderCallback::create(
        [state](double v) {
            state->value = v;
            if (state->input) {
                state->input->setString(formatNumber(v, state->decimals).c_str());
            }
            if (state->onChange) state->onChange(v);
        },
        nullptr, minV, maxV);

    auto* slider = Slider::create(cb, menu_selector(SliderCallback::onChanged), sliderScale);
    slider->setPosition({width / 2.f, kPad + kSliderH / 2.f - 2.f});
    slider->setValue(normFromValue(state->value, minV, maxV));
    slider->setUserObject(cb);
    cb->m_slider = slider;
    row->addChild(slider);
    state->slider = slider;

    return row;
}

CCNode* makeSelectRow(
    float width,
    char const* title, char const* desc,
    std::vector<std::string> options, int index,
    std::function<void(int)> onChange,
    CCLabelBMFont** outLabel
) {
    constexpr float kPad = 7.f;
    constexpr float kTitleH = 15.f;
    constexpr float kZoneW = 132.f;
    float const textMaxW = width - kZoneW - 22.f;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = descLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }

    float const rowH = std::max(kPad + kTitleH + descH + kPad, 36.f);
    auto* row = makeRow(width, rowH);

    auto* titleLbl = titleLabel(title, textMaxW);
    titleLbl->setPosition({11.f, rowH - kPad});
    row->addChild(titleLbl);

    if (descLbl) {
        descLbl->setPosition({11.f, rowH - kPad - kTitleH});
        row->addChild(descLbl);
    }

    float const cy = rowH / 2.f;
    float const valueCX = width - 11.f - kZoneW / 2.f;

    auto* valLbl = CCLabelBMFont::create(
        (index >= 0 && index < static_cast<int>(options.size()))
            ? options[static_cast<std::size_t>(index)].c_str() : "",
        "goldFont.fnt");
    valLbl->setAnchorPoint({0.5f, 0.5f});
    valLbl->setColor(kValueColor);
    valLbl->limitLabelWidth(kZoneW - 48.f, 0.44f, 0.1f);
    valLbl->setPosition({valueCX, cy});
    row->addChild(valLbl);
    if (outLabel) *outLabel = valLbl;

    auto* menu = rowMenu(row);

    auto state = std::make_shared<int>(index);
    auto opts = std::make_shared<std::vector<std::string>>(std::move(options));
    auto cb = std::make_shared<std::function<void(int)>>(std::move(onChange));

    auto cycle = [state, opts, cb, valLbl](int dir) {
        if (opts->empty()) return;
        int n = static_cast<int>(opts->size());
        *state = ((*state + dir) % n + n) % n;
        valLbl->setString((*opts)[static_cast<std::size_t>(*state)].c_str());
        valLbl->limitLabelWidth(kZoneW - 48.f, 0.44f, 0.1f);
        if (*cb) (*cb)(*state);
    };

    auto addArrow = [&](float cx, bool flip, int dir) {
        auto* spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        if (!spr) return;
        spr->setScale(0.5f);
        spr->setFlipX(flip);
        auto* btn = CCMenuItemExt::createSpriteExtra(
            spr, [cycle, dir](CCMenuItemSpriteExtra*) { cycle(dir); });
        btn->setPosition({cx, cy});
        menu->addChild(btn);
    };
    addArrow(valueCX - kZoneW / 2.f + 14.f, false, -1);
    addArrow(valueCX + kZoneW / 2.f - 14.f, true, 1);

    return row;
}

CCNode* makeButtonRow(
    float width,
    char const* title, char const* desc,
    char const* buttonText,
    std::function<void()> onPress
) {
    constexpr float kPad = 7.f;
    constexpr float kTitleH = 15.f;
    float const textMaxW = width - 108.f;

    CCLabelBMFont* descLbl = nullptr;
    float descH = 0.f;
    if (desc && desc[0] != '\0') {
        descLbl = descLabel(desc, textMaxW);
        descH = scaledHeight(descLbl) + 2.f;
    }

    float const rowH = std::max(kPad + kTitleH + descH + kPad, 36.f);
    auto* row = makeRow(width, rowH);

    auto* titleLbl = titleLabel(title, textMaxW);
    titleLbl->setPosition({11.f, rowH - kPad});
    row->addChild(titleLbl);

    if (descLbl) {
        descLbl->setPosition({11.f, rowH - kPad - kTitleH});
        row->addChild(descLbl);
    }

    auto* menu = rowMenu(row);
    if (auto* btn = makeButton(buttonText, "GJ_button_04.png", 0.58f, std::move(onPress))) {
        btn->setPosition({width - 11.f - btn->getScaledContentSize().width / 2.f, rowH / 2.f});
        menu->addChild(btn);
    }

    return row;
}

CCNode* makeHint(float width, char const* text) {
    auto* lbl = descLabel(text, width - 20.f);
    float const rowH = scaledHeight(lbl) + 8.f;

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});

    lbl->setPosition({10.f, rowH - 3.f});
    row->addChild(lbl);
    return row;
}

CCNode* makeCard(
    float width,
    char const* title, ccColor3B accent,
    std::vector<CCNode*> const& rows
) {
    constexpr float kHeaderH = 24.f;
    constexpr float kGap = 5.f;

    float contentH = 0.f;
    for (auto* r : rows) if (r) contentH += r->getContentSize().height + kGap;
    if (!rows.empty()) contentH -= kGap;

    bool const hasTitle = title && title[0] != '\0';
    float const cardH = (hasTitle ? kHeaderH : 0.f) + contentH;

    auto* card = CCNode::create();
    card->setAnchorPoint({0.f, 0.f});
    card->setContentSize({width, cardH});

    float y = cardH;
    if (hasTitle) {
        // section-color dot + gold title, like the game list headers.
        if (auto* dot = paimon::SpriteHelper::createColorPanel(7.f, 7.f, accent, 255, 3.f)) {
            dot->setAnchorPoint({0.f, 0.f});
            dot->setPosition({4.f, y - 15.f});
            card->addChild(dot);
        }

        auto* titleLbl = CCLabelBMFont::create(title, "goldFont.fnt");
        titleLbl->setAnchorPoint({0.f, 1.f});
        titleLbl->limitLabelWidth(width - 26.f, 0.5f, 0.1f);
        titleLbl->setPosition({16.f, y - 2.f});
        card->addChild(titleLbl);

        if (auto* line = paimon::SpriteHelper::createColorPanel(
                width, 1.f, {255, 255, 255}, 55, 0.f)) {
            line->setAnchorPoint({0.f, 0.f});
            line->setPosition({0.f, y - kHeaderH + 3.f});
            card->addChild(line);
        }
        y -= kHeaderH;
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

geode::ScrollLayer* makeScrollStack(
    CCSize size,
    std::vector<CCNode*> const& items,
    float gap
) {
    auto* scroll = geode::ScrollLayer::create(size);

    float totalH = 0.f;
    for (auto* n : items) if (n) totalH += n->getContentSize().height + gap;
    if (!items.empty()) totalH -= gap;

    float const contentH = std::max(size.height, totalH + 6.f);
    auto* content = scroll->m_contentLayer;
    content->setContentSize({size.width, contentH});

    float y = contentH - 3.f;
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

// each tab holds both faces (green on / gray off) and swaps visibility:
// buttonsprite can't re-tint live.
struct TabBarState {
    std::vector<CCNode*> onSprites;
    std::vector<CCNode*> offSprites;
    int selected = 0;

    void restyle() {
        for (std::size_t i = 0; i < onSprites.size(); ++i) {
            bool sel = static_cast<int>(i) == selected;
            if (onSprites[i]) onSprites[i]->setVisible(sel);
            if (i < offSprites.size() && offSprites[i]) offSprites[i]->setVisible(!sel);
        }
    }
};

}  // anonymous namespace

CCNode* makeTabBar(
    float width,
    std::vector<std::string> const& labels,
    int selected,
    std::function<void(int)> onSelect
) {
    constexpr float kGap = 5.f;
    float const barH = kTabBarHeight;

    auto* bar = CCNode::create();
    bar->setAnchorPoint({0.f, 0.f});
    bar->setContentSize({width, barH});

    auto* menu = rowMenu(bar);

    int const n = std::max<int>(1, static_cast<int>(labels.size()));
    float const tabW = (width - kGap * static_cast<float>(n - 1)) / static_cast<float>(n);

    auto state = std::make_shared<TabBarState>();
    state->selected = std::clamp(selected, 0, n - 1);
    auto cb = std::make_shared<std::function<void(int)>>(std::move(onSelect));

    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        auto const& text = labels[static_cast<std::size_t>(i)];

        auto* holder = CCNode::create();
        holder->setAnchorPoint({0.5f, 0.5f});
        holder->setContentSize({tabW, barH});

        auto addFace = [&](bool selectedFace, bool visible) -> CCNode* {
            auto* spr = makeTabFace(text.c_str(), selectedFace, tabW, barH);
            if (!spr) return nullptr;
            spr->setPosition({tabW / 2.f, barH / 2.f});
            spr->setVisible(visible);
            holder->addChild(spr);
            return spr;
        };

        bool const sel = i == state->selected;
        state->onSprites.push_back(addFace(true, sel));
        state->offSprites.push_back(addFace(false, !sel));

        auto* btn = CCMenuItemExt::createSpriteExtra(holder,
            [state, cb, i](CCMenuItemSpriteExtra*) {
                if (state->selected == i) return;
                state->selected = i;
                state->restyle();
                if (*cb) (*cb)(i);
            });
        btn->setPosition({tabW / 2.f + static_cast<float>(i) * (tabW + kGap), barH / 2.f});
        menu->addChild(btn);
    }

    state->restyle();
    return bar;
}

bool queueWheelScroll(geode::ScrollLayer* scrollLayer, float x, float y,
                      float& targetY, bool& targetSet, float speed) {
    return paimon::configkit::queueWheelScroll(scrollLayer, x, y, targetY, targetSet, speed);
}

void stepWheelScroll(geode::ScrollLayer* scrollLayer,
                     float& targetY, bool& targetSet, float dt) {
    paimon::configkit::stepWheelScroll(scrollLayer, targetY, targetSet, dt);
}

}  // namespace paimon::icon_maker::gdkit
