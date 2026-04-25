#include "SettingsControls.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::settings_ui {

// ── Helpers internos ──────────────────────────────────────────────────

// Prioridad dinamica para controles hijo dentro del panel.
// Se lee en tiempo de creacion para participar en el force priority system de GD.
static int childTouchPrio() {
    return CCDirector::sharedDirector()->getTouchDispatcher()->getTargetPrio() - 2;
}

static CCLabelBMFont* makeLabel(const char* text) {
    auto label = CCLabelBMFont::create(text, "chatFont.fnt");
    label->setScale(0.55f);
    label->setColor({200, 200, 200});
    label->setAnchorPoint({0.f, 0.5f});
    return label;
}

static CCLabelBMFont* makeValueLabel(const char* text) {
    auto label = CCLabelBMFont::create(text, "bigFont.fnt");
    label->setScale(0.32f);
    label->setColor({255, 255, 255});
    label->setAnchorPoint({0.f, 0.5f});
    return label;
}

// ── Toggle Row ─────────────────────────────────────────────────────────

// CCObject helper que captura el callback
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
    auto row = CCNode::create();
    row->setContentSize({width, ROW_HEIGHT});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = makeLabel(label);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    auto cb = ToggleCallback::create(std::move(onChange));
    auto toggler = CCMenuItemToggler::createWithStandardSprites(
        cb, menu_selector(ToggleCallback::onToggle), 0.55f
    );
    cb->m_toggler = toggler;
    toggler->toggle(initialValue);
    toggler->setPosition({width - 28.f, ROW_HEIGHT / 2.f});
    menu->addChild(toggler);
    // retener callback via user object
    toggler->setUserObject(cb);

    // separador sutil
    auto sep = paimon::SpriteHelper::createRoundedRect(width - 16.f, 1.f, 0.f, {1.f, 1.f, 1.f, 0.06f});
    if (sep) {
        sep->setPosition({8.f, 0.f});
        row->addChild(sep);
    }

    return row;
}

// ── Slider Row ─────────────────────────────────────────────────────────

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
        }
        m_callback(mapped);
    }
};

CCNode* createSliderRow(const char* label, float initialValue,
                        float minVal, float maxVal,
                        std::function<void(float)> onChange,
                        float width) {
    auto row = CCNode::create();
    row->setContentSize({width, ROW_HEIGHT});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = makeLabel(label);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    auto cb = SliderCallback::create(std::move(onChange), minVal, maxVal);

    float range = maxVal - minVal;
    float normalized = (range > 0.f) ? (initialValue - minVal) / range : 0.f;

    auto slider = Slider::create(cb, menu_selector(SliderCallback::onChanged), 0.6f);
    slider->setValue(normalized);
    slider->ignoreAnchorPointForPosition(false);
    slider->setAnchorPoint({0.5f, 0.5f});
    slider->setPosition({500.f, 105.f});
    slider->setScale(0.55f);
    slider->setTouchEnabled(true);
    if (slider->m_touchLogic) {
        slider->m_touchLogic->setTouchPriority(childTouchPrio());
    }
    row->addChild(slider);
    cb->m_slider = slider;

    auto valLabel = makeValueLabel(fmt::format("{:.2f}", initialValue).c_str());
    valLabel->setPosition({width - 42.f, ROW_HEIGHT / 2.f});
    row->addChild(valLabel);
    cb->m_valueLabel = valLabel;

    // retener callback
    slider->setUserObject(cb);

    auto sep = paimon::SpriteHelper::createRoundedRect(width - 16.f, 1.f, 0.f, {1.f, 1.f, 1.f, 0.06f});
    if (sep) {
        sep->setPosition({8.f, 0.f});
        row->addChild(sep);
    }

    return row;
}

// ── Int Slider Row ─────────────────────────────────────────────────────

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
        }
        m_callback(mapped);
    }
};

CCNode* createIntSliderRow(const char* label, int initialValue,
                           int minVal, int maxVal,
                           std::function<void(int)> onChange,
                           float width) {
    auto row = CCNode::create();
    row->setContentSize({width, ROW_HEIGHT});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = makeLabel(label);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    auto cb = IntSliderCallback::create(std::move(onChange), minVal, maxVal);

    float range = static_cast<float>(maxVal - minVal);
    float normalized = (range > 0.f) ? static_cast<float>(initialValue - minVal) / range : 0.f;

    auto slider = Slider::create(cb, menu_selector(IntSliderCallback::onChanged), 0.6f);
    slider->setValue(normalized);
    slider->ignoreAnchorPointForPosition(false);
    slider->setAnchorPoint({0.5f, 0.5f});
    slider->setPosition({500.f, 105.f});
    slider->setScale(0.55f);
    slider->setTouchEnabled(true);
    if (slider->m_touchLogic) {
        slider->m_touchLogic->setTouchPriority(childTouchPrio());
    }
    row->addChild(slider);
    cb->m_slider = slider;

    auto valLabel = makeValueLabel(fmt::format("{}", initialValue).c_str());
    valLabel->setPosition({width - 42.f, ROW_HEIGHT / 2.f});
    row->addChild(valLabel);
    cb->m_valueLabel = valLabel;

    slider->setUserObject(cb);

    auto sep = paimon::SpriteHelper::createRoundedRect(width - 16.f, 1.f, 0.f, {1.f, 1.f, 1.f, 0.06f});
    if (sep) {
        sep->setPosition({8.f, 0.f});
        row->addChild(sep);
    }

    return row;
}

// ── Dropdown Row ───────────────────────────────────────────────────────

class DropdownCallback : public CCObject {
public:
    std::function<void(std::string const&)> m_callback;
    std::vector<std::string> m_options;
    int m_currentIndex = 0;
    CCLabelBMFont* m_valueLabel = nullptr;

    static DropdownCallback* create(std::function<void(std::string const&)> cb,
                                     std::vector<std::string> opts, int initIdx) {
        auto ret = new DropdownCallback();
        ret->m_callback = std::move(cb);
        ret->m_options = std::move(opts);
        ret->m_currentIndex = initIdx;
        ret->autorelease();
        return ret;
    }

    void onNext(CCObject*) {
        if (m_options.empty()) return;
        m_currentIndex = (m_currentIndex + 1) % static_cast<int>(m_options.size());
        updateLabel();
        if (m_callback) m_callback(m_options[m_currentIndex]);
    }

    void onPrev(CCObject*) {
        if (m_options.empty()) return;
        m_currentIndex = (m_currentIndex - 1 + static_cast<int>(m_options.size())) % static_cast<int>(m_options.size());
        updateLabel();
        if (m_callback) m_callback(m_options[m_currentIndex]);
    }

    void updateLabel() {
        if (m_valueLabel && m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_options.size())) {
            m_valueLabel->setString(m_options[m_currentIndex].c_str());
        }
    }
};

CCNode* createDropdownRow(const char* label, std::string const& initialValue,
                          std::vector<std::string> const& options,
                          std::function<void(std::string const&)> onChange,
                          float width) {
    auto row = CCNode::create();
    row->setContentSize({width, ROW_HEIGHT});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = makeLabel(label);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    // encontrar indice inicial
    int initIdx = 0;
    for (size_t i = 0; i < options.size(); i++) {
        if (options[i] == initialValue) {
            initIdx = static_cast<int>(i);
            break;
        }
    }

    auto cb = DropdownCallback::create(std::move(onChange), options, initIdx);

    // flecha izquierda
    auto leftSpr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    if (!leftSpr) leftSpr = CCSprite::create();
    leftSpr->setScale(0.3f);
    leftSpr->setFlipX(true);
    auto leftBtn = CCMenuItemSpriteExtra::create(leftSpr, cb, menu_selector(DropdownCallback::onPrev));
    leftBtn->setPosition({width - 110.f, ROW_HEIGHT / 2.f});
    menu->addChild(leftBtn);

    // valor actual (centered between arrows)
    auto valLabel = makeValueLabel(initialValue.c_str());
    valLabel->setPosition({(width - 110.f + width - 22.f) / 2.f, ROW_HEIGHT / 2.f});
    valLabel->setAnchorPoint({0.5f, 0.5f});
    row->addChild(valLabel);
    cb->m_valueLabel = valLabel;

    // flecha derecha
    auto rightSpr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    if (!rightSpr) rightSpr = CCSprite::create();
    rightSpr->setScale(0.3f);
    auto rightBtn = CCMenuItemSpriteExtra::create(rightSpr, cb, menu_selector(DropdownCallback::onNext));
    rightBtn->setPosition({width - 22.f, ROW_HEIGHT / 2.f});
    menu->addChild(rightBtn);

    // retener callback
    leftBtn->setUserObject(cb);

    auto sep = paimon::SpriteHelper::createRoundedRect(width - 16.f, 1.f, 0.f, {1.f, 1.f, 1.f, 0.06f});
    if (sep) {
        sep->setPosition({8.f, 0.f});
        row->addChild(sep);
    }

    return row;
}

// ── Button Row ─────────────────────────────────────────────────────────

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
    auto row = CCNode::create();
    row->setContentSize({width, ROW_HEIGHT});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = makeLabel(label);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    auto cb = ButtonCallback::create(std::move(onPress));

    auto btnSpr = ButtonSprite::create(buttonText, 70, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.5f);
    auto btn = CCMenuItemSpriteExtra::create(btnSpr, cb, menu_selector(ButtonCallback::onPress));
    btn->setPosition({width - 55.f, ROW_HEIGHT / 2.f});
    btn->setUserObject(cb);
    menu->addChild(btn);

    auto sep = paimon::SpriteHelper::createRoundedRect(width - 16.f, 1.f, 0.f, {1.f, 1.f, 1.f, 0.06f});
    if (sep) {
        sep->setPosition({8.f, 0.f});
        row->addChild(sep);
    }

    return row;
}

// ── Link Row ───────────────────────────────────────────────────────────

CCNode* createLinkRow(const char* label, std::function<void()> onOpen,
                      float width) {
    auto row = CCNode::create();
    row->setContentSize({width, ROW_HEIGHT});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = makeLabel(label);
    lbl->setPosition({LABEL_X, ROW_HEIGHT / 2.f});
    row->addChild(lbl);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    auto cb = ButtonCallback::create(std::move(onOpen));

    auto arrowSpr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    if (!arrowSpr) arrowSpr = CCSprite::create();
    arrowSpr->setScale(0.3f);
    auto arrowBtn = CCMenuItemSpriteExtra::create(arrowSpr, cb, menu_selector(ButtonCallback::onPress));
    arrowBtn->setPosition({width - 22.f, ROW_HEIGHT / 2.f});
    arrowBtn->setUserObject(cb);
    menu->addChild(arrowBtn);

    auto sep = paimon::SpriteHelper::createRoundedRect(width - 16.f, 1.f, 0.f, {1.f, 1.f, 1.f, 0.06f});
    if (sep) {
        sep->setPosition({8.f, 0.f});
        row->addChild(sep);
    }

    return row;
}

// ── Section Header ─────────────────────────────────────────────────────

CCNode* createSectionHeader(const char* title, float width) {
    auto row = CCNode::create();
    row->setContentSize({width, HEADER_HEIGHT});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = CCLabelBMFont::create(title, "goldFont.fnt");
    lbl->setScale(0.35f);
    lbl->setAnchorPoint({0.f, 0.5f});
    lbl->setPosition({LABEL_X, HEADER_HEIGHT / 2.f});
    row->addChild(lbl);

    auto sep = paimon::SpriteHelper::createRoundedRect(width - 16.f, 1.f, 0.f, {0.94f, 0.76f, 0.22f, 0.3f});
    if (sep) {
        sep->setPosition({8.f, 2.f});
        row->addChild(sep);
    }

    return row;
}

// ── Collapsible Header ─────────────────────────────────────────────────

class CollapsibleCallback : public CCObject {
public:
    bool m_expanded;
    CCNode* m_contentContainer;
    CCLabelBMFont* m_caret;
    std::function<void()> m_onToggle;

    static CollapsibleCallback* create(CCNode* content, CCLabelBMFont* caret,
                                        bool expanded, std::function<void()> onToggle) {
        auto ret = new CollapsibleCallback();
        ret->m_expanded = expanded;
        ret->m_contentContainer = content;
        ret->m_caret = caret;
        ret->m_onToggle = std::move(onToggle);
        ret->autorelease();
        return ret;
    }

    void onToggle(CCObject*) {
        m_expanded = !m_expanded;
        m_contentContainer->setVisible(m_expanded);
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

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    // caret indicator
    auto caret = CCLabelBMFont::create(initiallyExpanded ? "v" : ">", "bigFont.fnt");
    caret->setScale(0.25f);
    caret->setColor({240, 194, 56});
    caret->setAnchorPoint({0.5f, 0.5f});

    // title label
    auto lbl = CCLabelBMFont::create(title, "goldFont.fnt");
    lbl->setScale(0.32f);
    lbl->setAnchorPoint({0.f, 0.5f});

    // compound button content
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

    // gold separator line
    auto sep = paimon::SpriteHelper::createRoundedRect(width - 16.f, 1.f, 0.f,
        {0.94f, 0.76f, 0.22f, 0.3f});
    if (sep) {
        sep->setPosition({8.f, 2.f});
        row->addChild(sep);
    }

    contentContainer->setVisible(initiallyExpanded);
    return row;
}

} // namespace paimon::settings_ui
