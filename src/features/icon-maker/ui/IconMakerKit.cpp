#include "IconMakerKit.hpp"

#include "../../../ui/PaiConfigKit.hpp"

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

CCNode* makeRow(float width, float height) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    if (auto* plate = makePlate(width, height)) {
        row->addChild(plate, -1);
    }
    return row;
}

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
    auto* window = NineSlice::create("GJ_square01.png");
    window->setContentSize(size);
    window->setAnchorPoint({0.f, 0.f});
    return window;
}

CCNode* makePlate(float width, float height, ccColor3B color, GLubyte opacity) {
    return paimon::ui::makeInset({width, height}, opacity, color);
}

CCSprite* makeTabFace(char const* text, bool selected, float maxW, float maxH) {
    // leaves a few units of air inside the slot instead of filling it edge to edge.
    return paimon::ui::makeButtonSprite(text, selected ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray,
        maxW, std::clamp((maxH - 6.f) / 30.f, 0.45f, 0.62f));
}

CCMenuItemSpriteExtra* makeButton(char const* text, char const* sprite,
                                  float scale, std::function<void()> onPress) {
    return paimon::ui::makeButton(text, std::move(onPress), sprite, 0.f, scale);
}


CCNode* makeToggleRow(
    float width,
    char const* title, char const* desc,
    bool value,
    std::function<void(bool)> onChange,
    CCMenuItemToggler** outToggle
) {
    return paimon::configkit::makeToggleRow(width, title, desc, value, std::move(onChange), outToggle);
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
        format ? format(value).c_str() : "", "bigFont.fnt");
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
        auto* face = paimon::ui::makeButtonSprite(text, paimon::ui::Btn::Gray, kStepW, kStepW / 30.f, "bigFont.fnt");
        face->setPosition({kStepW / 2.f, kStepW / 2.f});
        holder->addChild(face);

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
    return paimon::configkit::makeSelectRow(width, title, desc, std::move(options), index, std::move(onChange), outLabel);
}

CCNode* makeButtonRow(
    float width,
    char const* title, char const* desc,
    char const* buttonText,
    std::function<void()> onPress
) {
    return paimon::configkit::makeButtonRow(width, title, desc, buttonText, std::move(onPress));
}

CCNode* makeHint(float width, char const* text) {
    return paimon::configkit::makeHint(width, text);
}

CCNode* makeCard(
    float width,
    char const* title, ccColor3B accent,
    std::vector<CCNode*> const& rows
) {
    return paimon::configkit::makeCard(width, title, accent, rows);
}

geode::ScrollLayer* makeScrollStack(
    CCSize size,
    std::vector<CCNode*> const& items,
    float gap
) {
    return paimon::configkit::makeScrollStack(size, items, gap);
}

CCNode* makeTabBar(
    float width,
    std::vector<std::string> const& labels,
    int selected,
    std::function<void(int)> onSelect
) {
    return paimon::configkit::makeTabBar(width, labels, selected, std::move(onSelect));
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
