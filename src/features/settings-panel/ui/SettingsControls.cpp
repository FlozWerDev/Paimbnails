#include "SettingsControls.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include <algorithm>
#include <cmath>
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::settings_ui {

static int childTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

static CCLabelBMFont* makeLabel(const char* text) {
    auto label = CCLabelBMFont::create(text, "chatFont.fnt");
    label->setScale(0.5f);
    label->setColor(paimon::ui::palette::text);
    label->setAnchorPoint({0.f, 0.5f});
    return label;
}

static CCLabelBMFont* makeValueLabel(const char* text) {
    auto label = CCLabelBMFont::create(text, "bigFont.fnt");
    label->setScale(0.3f);
    label->setColor(paimon::ui::palette::accent);
    label->setAnchorPoint({0.f, 0.5f});
    return label;
}

static void addRowBackground(CCNode* row, float width, float height) {
    auto bg = paimon::ui::makeSurface({width - 4.f, height - 4.f},
        paimon::ui::palette::raised, 245, 5.f);
    if (bg) {
        bg->setPosition({2.f, 2.f});
        row->addChild(bg, -1);
    }
}

static CCNode* makeRow(float width, float height = ROW_HEIGHT, bool withBg = true) {
    auto row = CCNode::create();
    row->setContentSize({width, height});
    row->setAnchorPoint({0.f, 0.f});
    if (withBg) addRowBackground(row, width, height);
    return row;
}


class ToggleCallback : public CCObject {
public:
    std::function<void(bool)> m_callback;
    CCMenuItemToggler* m_toggler = nullptr;

    static ToggleCallback* create(std::function<void(bool)> cb) {
        auto ret = new ToggleCallback();
        ret->m_callback = std::move(cb);
        ret->autorelease();
        return ret;
    }

    void onToggle(CCObject*) {
        if (m_callback && m_toggler) {
            m_callback(!m_toggler->isToggled());
        }
    }
};

CCNode* createToggleRow(const char* label, bool initialValue,
                        std::function<void(bool)> onChange,
                        float width) {
    auto row = makeRow(width);

    auto lbl = makeLabel(label);
    lbl->limitLabelWidth(std::max(36.f, width - 72.f), 0.5f, 0.23f);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    auto cb = ToggleCallback::create(std::move(onChange));
    auto toggler = paimon::ui::makeSwitch(
        cb, menu_selector(ToggleCallback::onToggle), initialValue, 0.85f);
    cb->m_toggler = toggler;
    toggler->toggle(initialValue);
    toggler->setPosition({width - 28.f, ROW_HEIGHT / 2.f});
    menu->addChild(toggler);
    toggler->setUserObject(cb);

    return row;
}


class SliderCallback : public CCNode {
public:
    std::function<void(float)> m_callback;
    float m_min = 0.f;
    float m_max = 1.f;
    CCLabelBMFont* m_valueLabel = nullptr;
    Slider* m_slider = nullptr;

    static SliderCallback* create(std::function<void(float)> cb, float mn, float mx) {
        auto ret = new SliderCallback();
        ret->init();
        ret->m_callback = std::move(cb);
        ret->m_min = mn;
        ret->m_max = mx;
        ret->autorelease();
        return ret;
    }

    void onChanged(CCObject*) {
        if (!m_slider || !m_callback) return;
        float val = m_slider->getThumb()->getValue();
        float mapped = m_min + val * (m_max - m_min);
        if (m_valueLabel) {
            m_valueLabel->setString(fmt::format("{:.2f}", mapped).c_str());
            m_valueLabel->limitLabelWidth(48.f, 0.3f, 0.12f);
        }
        m_callback(mapped);
    }
};

CCNode* createSliderRow(const char* label, float initialValue,
                        float minVal, float maxVal,
                        std::function<void(float)> onChange,
                        float width) {
    auto row = makeRow(width);

    auto lbl = makeLabel(label);
    lbl->limitLabelWidth(std::max(36.f, width - 208.f), 0.5f, 0.23f);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    auto cb = SliderCallback::create(std::move(onChange), minVal, maxVal);

    float range = maxVal - minVal;
    float normalized = (range > 0.f) ? (initialValue - minVal) / range : 0.f;

    auto slider = Slider::create(cb, menu_selector(SliderCallback::onChanged), 0.55f);
    slider->setValue(std::clamp(normalized, 0.f, 1.f));
    slider->setTouchEnabled(true);
    if (slider->m_touchLogic) {
        slider->m_touchLogic->setTouchPriority(childTouchPrio());
    }

    float sliderW = slider->m_width * 0.55f;
    auto sliderWrapper = cocos2d::CCNode::create();
    sliderWrapper->setContentSize(CCSize{sliderW, slider->m_height * 0.55f});
    sliderWrapper->setAnchorPoint({0.f, 0.5f});
    sliderWrapper->setPosition({width - sliderW - 62.f, ROW_HEIGHT / 2.f});
    sliderWrapper->addChildAtPosition(slider, Anchor::Center, ccp(0, 0));
    row->addChild(sliderWrapper);
    cb->m_slider = slider;

    auto valLabel = makeValueLabel(fmt::format("{:.2f}", initialValue).c_str());
    valLabel->setAnchorPoint({1.f, 0.5f});
    valLabel->setPosition({width - 12.f, ROW_HEIGHT / 2.f});
    row->addChild(valLabel);
    cb->m_valueLabel = valLabel;

    slider->setUserObject(cb);

    return row;
}


class IntSliderCallback : public CCNode {
public:
    std::function<void(int)> m_callback;
    int m_min = 0;
    int m_max = 10;
    CCLabelBMFont* m_valueLabel = nullptr;
    Slider* m_slider = nullptr;

    static IntSliderCallback* create(std::function<void(int)> cb, int mn, int mx) {
        auto ret = new IntSliderCallback();
        ret->init();
        ret->m_callback = std::move(cb);
        ret->m_min = mn;
        ret->m_max = mx;
        ret->autorelease();
        return ret;
    }

    void onChanged(CCObject*) {
        if (!m_slider || !m_callback) return;
        float val = m_slider->getThumb()->getValue();
        int mapped = m_min + static_cast<int>(std::round(val * (m_max - m_min)));
        if (m_valueLabel) {
            m_valueLabel->setString(fmt::format("{}", mapped).c_str());
            m_valueLabel->limitLabelWidth(48.f, 0.3f, 0.12f);
        }
        m_callback(mapped);
    }
};

CCNode* createIntSliderRow(const char* label, int initialValue,
                           int minVal, int maxVal,
                           std::function<void(int)> onChange,
                           float width) {
    auto row = makeRow(width);

    auto lbl = makeLabel(label);
    lbl->limitLabelWidth(std::max(36.f, width - 208.f), 0.5f, 0.23f);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    auto cb = IntSliderCallback::create(std::move(onChange), minVal, maxVal);

    float range = static_cast<float>(maxVal - minVal);
    float normalized = (range > 0.f) ? static_cast<float>(initialValue - minVal) / range : 0.f;

    auto slider = Slider::create(cb, menu_selector(IntSliderCallback::onChanged), 0.55f);
    slider->setValue(std::clamp(normalized, 0.f, 1.f));
    slider->setTouchEnabled(true);
    if (slider->m_touchLogic) {
        slider->m_touchLogic->setTouchPriority(childTouchPrio());
    }

    float sliderW = slider->m_width * 0.55f;
    auto sliderWrapper = cocos2d::CCNode::create();
    sliderWrapper->setContentSize(CCSize{sliderW, slider->m_height * 0.55f});
    sliderWrapper->setAnchorPoint({0.f, 0.5f});
    sliderWrapper->setPosition({width - sliderW - 62.f, ROW_HEIGHT / 2.f});
    sliderWrapper->addChildAtPosition(slider, Anchor::Center, ccp(0, 0));
    row->addChild(sliderWrapper);
    cb->m_slider = slider;

    auto valLabel = makeValueLabel(fmt::format("{}", initialValue).c_str());
    valLabel->setAnchorPoint({1.f, 0.5f});
    valLabel->setPosition({width - 12.f, ROW_HEIGHT / 2.f});
    row->addChild(valLabel);
    cb->m_valueLabel = valLabel;

    slider->setUserObject(cb);

    return row;
}


CCNode* createDropdownRow(const char* label, std::string const& initialValue,
                          std::vector<std::string> const& options,
                          std::function<void(std::string const&)> onChange,
                          float width) {
    auto it = std::find(options.begin(), options.end(), initialValue);
    int const index = it == options.end() ? 0 : static_cast<int>(it - options.begin());
    return paimon::configkit::makeSelectRow(width, label, "", options, index,
        [options, callback = std::move(onChange)](int selected) {
            if (callback && selected >= 0 && selected < static_cast<int>(options.size())) callback(options[selected]);
        });
}

class ButtonCallback : public CCObject {
public:
    std::function<void()> m_callback;

    static ButtonCallback* create(std::function<void()> cb) {
        auto ret = new ButtonCallback();
        ret->m_callback = std::move(cb);
        ret->autorelease();
        return ret;
    }

    void onPress(CCObject*) {
        if (m_callback) m_callback();
    }
};

CCNode* createButtonRow(const char* label, const char* buttonText,
                        std::function<void()> onPress,
                        float width) {
    auto row = makeRow(width);

    auto lbl = makeLabel(label);
    lbl->limitLabelWidth(std::max(36.f, width - 122.f), 0.5f, 0.23f);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    auto cb = ButtonCallback::create(std::move(onPress));

    auto btnSpr = paimon::ui::makeButtonFace(buttonText, {90.f, 27.f}, {36, 75, 106});
    auto btn = CCMenuItemSpriteExtra::create(btnSpr, cb, menu_selector(ButtonCallback::onPress));
    btn->setPosition({width - 57.f, ROW_HEIGHT / 2.f});
    btn->setUserObject(cb);
    menu->addChild(btn);

    return row;
}


CCNode* createLinkRow(const char* label, std::function<void()> onOpen,
                      float width) {
    auto row = makeRow(width);

    auto lbl = makeLabel(label);
    lbl->limitLabelWidth(std::max(36.f, width - 48.f), 0.5f, 0.23f);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    auto cb = ButtonCallback::create(std::move(onOpen));

    auto hitArea = CCSprite::create();
    hitArea->setContentSize({width, ROW_HEIGHT});
    auto caret = CCLabelBMFont::create(">", "bigFont.fnt");
    caret->setScale(0.4f);
    caret->setColor(paimon::ui::palette::accent);
    caret->setPosition({width - 20.f, ROW_HEIGHT / 2.f});
    hitArea->addChild(caret);
    auto button = CCMenuItemSpriteExtra::create(hitArea, cb, menu_selector(ButtonCallback::onPress));
    button->setPosition({width / 2.f, ROW_HEIGHT / 2.f});
    button->m_scaleMultiplier = 1.01f;
    button->setUserObject(cb);
    menu->addChild(button);

    return row;
}


CCNode* createTextInputRow(const char* label, std::string const& initialValue,
                           const char* placeholder, int maxChars,
                           std::function<void(std::string const&)> onChange,
                           float width) {
    auto row = makeRow(width);

    auto lbl = makeLabel(label);
    lbl->limitLabelWidth(std::max(36.f, width - std::max(width * 0.5f, 140.f) - 24.f), 0.5f, 0.23f);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    float inputW = std::max(width * 0.5f, 140.f);
    auto input = geode::TextInput::create(inputW / 0.72f, placeholder ? placeholder : "", "chatFont.fnt");
    input->setCommonFilter(geode::CommonFilter::Any);
    if (maxChars > 0) input->setMaxCharCount(maxChars);
    input->setString(initialValue);
    input->setPosition({width - inputW / 2.f - 10.f, ROW_HEIGHT / 2.f});
    input->setScale(0.72f);
    input->setCallback(std::move(onChange));
    row->addChild(input);

    return row;
}


CCNode* createHintRow(const char* text, float width) {
    auto label = CCLabelBMFont::create(text ? text : "", "chatFont.fnt",
        std::max(1.f, width - 28.f) / 0.44f, kCCTextAlignmentLeft);
    label->setScale(0.44f);
    label->setColor(paimon::ui::palette::muted);
    float const height = label->getScaledContentSize().height + 12.f;
    auto row = CCNode::create();
    row->setContentSize({width, height});
    row->setAnchorPoint({0.f, 0.f});
    label->setAnchorPoint({0.f, 1.f});
    label->setPosition({14.f, height - 6.f});
    row->addChild(label);
    return row;
}

CCNode* createSectionHeader(const char* title, float width) {
    auto row = CCNode::create();
    row->setContentSize({width, HEADER_HEIGHT});
    row->setAnchorPoint({0.f, 0.f});

    auto accent = paimon::SpriteHelper::createRoundedRect(
        3.f, 11.f, 1.5f, {116.f / 255.f, 204.f / 255.f, 1.f, 1.f}
    );
    if (accent) {
        accent->setPosition({4.f, HEADER_HEIGHT / 2.f - 5.5f - 1.f});
        row->addChild(accent);
    }

    auto lbl = CCLabelBMFont::create(title, "bigFont.fnt");
    lbl->setColor(paimon::ui::palette::accent);
    lbl->limitLabelWidth(width - 34.f, 0.38f, 0.16f);
    lbl->setScale(0.38f);
    lbl->setAnchorPoint({0.f, 0.5f});
    lbl->setPosition({LABEL_X + 2.f, HEADER_HEIGHT / 2.f - 1.f});
    row->addChild(lbl);

    float lblW = lbl->getContentSize().width * lbl->getScale();
    float sepX = LABEL_X + 2.f + lblW + 8.f;
    if (sepX < width - 8.f) {
        auto sep = CCLayerColor::create({255, 255, 255, 35},
                                        width - 8.f - sepX, 0.6f);
        sep->setPosition({sepX, HEADER_HEIGHT / 2.f - 1.f});
        row->addChild(sep);
    }

    return row;
}


class CollapsibleCallback : public CCObject {
public:
    bool m_expanded;
    CCNode* m_contentContainer;
    CCLabelBMFont* m_caret;
    std::function<void()> m_onToggle;
    float m_expandedHeight = 0.f;

    static CollapsibleCallback* create(CCNode* content, CCLabelBMFont* caret,
                                        bool expanded, std::function<void()> onToggle) {
        auto ret = new CollapsibleCallback();
        ret->m_expanded = expanded;
        ret->m_contentContainer = content;
        ret->m_caret = caret;
        ret->m_onToggle = std::move(onToggle);
        ret->m_expandedHeight = content->getContentSize().height;
        ret->autorelease();
        return ret;
    }

    void applyExpandedState() {
        auto size = m_contentContainer->getContentSize();
        size.height = m_expanded ? m_expandedHeight : 0.f;
        m_contentContainer->setContentSize(size);

        if (auto* children = m_contentContainer->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                child->setVisible(m_expanded);
            }
        }
    }

    void onToggle(CCObject*) {
        m_expanded = !m_expanded;
        applyExpandedState();
        m_caret->setString(m_expanded ? "v" : ">");
        if (m_onToggle) m_onToggle();
    }
};

CCNode* createCollapsibleHeader(const char* title, float width,
                                 CCNode* contentContainer, bool initiallyExpanded,
                                 std::function<void()> onToggle) {
    auto row = CCNode::create();
    row->setContentSize({width, HEADER_HEIGHT});
    row->setAnchorPoint({0.f, 0.f});

    auto bg = paimon::SpriteHelper::createRoundedRect(
        width - 4.f, HEADER_HEIGHT - 4.f, 5.f, {1.f, 1.f, 1.f, 0.06f}
    );
    if (bg) {
        bg->setPosition({2.f, 2.f});
        row->addChild(bg, -1);
    }

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    auto caret = CCLabelBMFont::create(initiallyExpanded ? "v" : ">", "bigFont.fnt");
    caret->setScale(0.25f);
    caret->setColor({240, 194, 56});
    caret->setAnchorPoint({0.5f, 0.5f});

    auto lbl = CCLabelBMFont::create(title, "bigFont.fnt");
    lbl->setColor(paimon::ui::palette::accent);
    lbl->limitLabelWidth(width - 34.f, 0.38f, 0.16f);
    lbl->setScale(0.34f);
    lbl->setAnchorPoint({0.f, 0.5f});

    auto btnContent = CCNode::create();
    btnContent->setContentSize({width - 16.f, HEADER_HEIGHT});
    btnContent->setAnchorPoint({0.5f, 0.5f});
    caret->setPosition({10.f, HEADER_HEIGHT / 2.f});
    lbl->setPosition({22.f, HEADER_HEIGHT / 2.f});
    btnContent->addChild(caret);
    btnContent->addChild(lbl);

    auto cb = CollapsibleCallback::create(contentContainer, caret,
                                           initiallyExpanded, std::move(onToggle));
    auto btn = CCMenuItemSpriteExtra::create(btnContent, cb,
        menu_selector(CollapsibleCallback::onToggle));
    btn->setPosition({width / 2.f, HEADER_HEIGHT / 2.f});
    btn->setUserObject(cb);
    menu->addChild(btn);

    cb->applyExpandedState();
    return row;
}

}
