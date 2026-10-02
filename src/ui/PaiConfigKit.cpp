#include "PaiConfigKit.hpp"
#include "PaimonPopup.hpp"
#include "../utils/Localization.hpp"
#include "../utils/FluidReveal.hpp"

#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::configkit {

namespace {

namespace ui = paimon::ui;

int childTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

CCLabelBMFont* makeTitleLabel(char const* text, float maxW, float scale = 0.38f) {
    auto* l = CCLabelBMFont::create(text, "bigFont.fnt");
    l->setAnchorPoint({0.f, 1.f});
    l->setColor(kTitleColor);
    l->limitLabelWidth(maxW, scale, 0.1f);
    return l;
}

CCLabelBMFont* makeDescLabel(char const* text, float wrapW) {
    constexpr float kScale = 0.48f;
    auto* l = CCLabelBMFont::create(text, "chatFont.fnt", wrapW / kScale, kCCTextAlignmentLeft);
    l->setScale(kScale);
    l->setAnchorPoint({0.f, 1.f});
    l->setColor(kDescColor);
    return l;
}

float scaledHeight(CCLabelBMFont* l) {
    return l ? l->getContentSize().height * l->getScale() : 0.f;
}

CCLabelBMFont* makeDescBlock(char const* text, float maxW, float& h) {
    if (!text || text[0] == '\0') return nullptr;
    auto* l = makeDescLabel(text, maxW);
    h = scaledHeight(l) + 2.f;
    return l;
}

CCMenu* makeRowMenu(CCNode* row) {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu, 5);
    return menu;
}

CCNode* makeRowNode(float width, float height) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    row->addChild(ui::makeInset({width, height}, kRowAlpha), -1);
    return row;
}

void addRowText(CCNode* row, CCLabelBMFont* title, CCLabelBMFont* desc, float top) {
    title->setPosition({10.f, top});
    row->addChild(title);
    if (desc) {
        desc->setPosition({10.f, top - 15.f});
        row->addChild(desc);
    }
}

// slider::create requires a ccnode target.
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
            m_valueLabel->limitLabelWidth(72.f, 0.34f, 0.12f);
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
    auto* tog = ui::makeToggle(value, std::move(onChange), scale);
    tog->setPosition(pos);
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
    float const textMaxW = std::max(40.f, width - 60.f);
    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, textMaxW, descH);
    float const rowH = std::max(34.f, 8.f + 14.f + descH + 8.f);

    auto* row = makeRowNode(width, rowH);
    addRowText(row, makeTitleLabel(title, textMaxW), descLbl, descLbl ? rowH - 8.f : rowH / 2.f + 6.f);

    auto* menu = makeRowMenu(row);
    auto* tog = addStandardToggler(menu, value, 0.6f, {width - 22.f, rowH / 2.f}, std::move(onChange));
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
    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, std::max(40.f, width - 24.f), descH);
    float const rowH = 48.f + descH;
    auto* row = makeRowNode(width, rowH);
    addRowText(row, makeTitleLabel(title, std::max(40.f, width - 100.f)), descLbl, rowH - 8.f);

    auto* valLbl = CCLabelBMFont::create(format ? format(value).c_str() : "", "bigFont.fnt");
    valLbl->setAnchorPoint({1.f, 1.f});
    valLbl->setColor(kValueColor);
    valLbl->limitLabelWidth(72.f, 0.34f, 0.12f);
    valLbl->setPosition({width - 10.f, rowH - 8.f});
    row->addChild(valLbl);

    float const sliderScale = std::clamp((width - 40.f) / 210.f, 0.3f, 0.85f);
    auto* cb = KitSliderCallback::create(std::move(onChange), format, minV, maxV);
    auto* slider = Slider::create(cb, menu_selector(KitSliderCallback::onChanged), sliderScale);
    cb->m_slider = slider;
    cb->m_valueLabel = valLbl;
    slider->setPosition({width / 2.f, 13.f});
    slider->setValue(normFromValue(value, minV, maxV));
    slider->setUserObject(cb);
    if (slider->m_touchLogic) slider->m_touchLogic->setTouchPriority(childTouchPrio());
    row->addChild(slider);

    if (outSlider) *outSlider = slider;
    if (outValue) *outValue = valLbl;
    return row;
}

namespace {

class OptionPickerPopup : public PaimonPopup {
    std::vector<std::string> m_options;
    std::function<void(int)> m_callback;
    ScrollLayer* m_scroll = nullptr;
    TextInput* m_search = nullptr;
    int m_selected = 0;

    void rebuild(std::string query) {
        if (m_scroll) m_scroll->removeFromParent();
        query = geode::utils::string::toLower(query);
        std::vector<CCNode*> rows;
        for (size_t i = 0; i < m_options.size(); ++i) {
            if (!query.empty() && geode::utils::string::toLower(m_options[i]).find(query) == std::string::npos) continue;
            auto* row = CCNode::create();
            row->setContentSize({310.f, 26.f});
            auto* menu = makeRowMenu(row);
            bool const current = static_cast<int>(i) == m_selected;
            auto* button = ui::makeButton(m_options[i].c_str(), [this, i] {
                Ref<OptionPickerPopup> guard = this;
                auto callback = m_callback;
                this->onClose(nullptr);
                if (callback) callback(static_cast<int>(i));
            }, current ? ui::Btn::Green : ui::Btn::Gray, 300.f, 0.7f, "bigFont.fnt");
            button->setPosition({155.f, 13.f});
            menu->addChild(button);
            rows.push_back(row);
        }
        if (rows.empty()) rows.push_back(makeHint(310.f,
            Localization::get().getLanguage() == Localization::Language::SPANISH ? "Sin resultados" : "No results"));
        m_scroll = makeScrollStack({310.f, 172.f}, rows, 3.f);
        m_scroll->setPosition({25.f, 18.f});
        m_mainLayer->addChild(m_scroll, 1);
    }

    bool init(std::vector<std::string> options, int selected, std::function<void(int)> callback) {
        if (!PaimonPopup::init(360.f, 270.f)) return false;
        m_options = std::move(options);
        m_selected = selected;
        m_callback = std::move(callback);
        bool const es = Localization::get().getLanguage() == Localization::Language::SPANISH;
        setTitle(es ? "Elegir opcion" : "Choose an option");

        auto* listBg = ui::makeInset({320.f, 180.f}, 75);
        listBg->setPosition({20.f, 14.f});
        m_mainLayer->addChild(listBg);

        m_search = TextInput::create(320.f, es ? "Buscar..." : "Search...", "chatFont.fnt");
        m_search->setCommonFilter(CommonFilter::Any);
        m_search->setMaxCharCount(64);
        m_search->setPosition({180.f, 214.f});
        m_search->setCallback([this](std::string const& query) { rebuild(query); });
        m_mainLayer->addChild(m_search);
        rebuild("");
        return true;
    }

public:
    static OptionPickerPopup* create(std::vector<std::string> options, int selected,
        std::function<void(int)> callback) {
        auto* popup = new OptionPickerPopup();
        if (popup->init(std::move(options), selected, std::move(callback))) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

}

CCNode* makeSelectRow(
    float width,
    char const* title, char const* desc,
    std::vector<std::string> options, int index,
    std::function<void(int)> onChange,
    CCLabelBMFont** outLabel,
    std::function<void()> onGear
) {
    constexpr float kArrowW = 20.f;
    bool const stacked = width < 330.f;
    float const zoneW = stacked ? width - 20.f : 170.f;
    float const textMaxW = std::max(40.f, stacked ? width - 24.f : width - zoneW - 28.f);
    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, textMaxW, descH);
    float const rowH = std::max(34.f, 30.f + descH) + (stacked ? 30.f : 0.f);
    auto* row = makeRowNode(width, rowH);
    addRowText(row, makeTitleLabel(title, textMaxW), descLbl,
        descLbl || stacked ? rowH - 8.f : rowH / 2.f + 6.f);

    auto state = std::make_shared<int>(options.empty() ? 0 : std::clamp(index, 0, static_cast<int>(options.size()) - 1));
    auto opts = std::make_shared<std::vector<std::string>>(std::move(options));
    auto cb = std::make_shared<std::function<void(int)>>(std::move(onChange));
    float const gearW = onGear ? 26.f : 0.f;
    float const cy = stacked ? 17.f : rowH / 2.f;
    float const zoneLeft = stacked ? 10.f : width - zoneW - 10.f;
    float const faceW = zoneW - gearW - kArrowW * 2.f;
    float const faceX = zoneLeft + kArrowW + faceW / 2.f;
    float const labelW = faceW - 12.f;

    auto* face = CCNode::create();
    face->setContentSize({faceW, 24.f});
    face->setAnchorPoint({0.5f, 0.5f});
    face->addChild(ui::makeInset({faceW, 24.f}, 100), -1);
    auto* valLbl = CCLabelBMFont::create(opts->empty() ? "-" : (*opts)[*state].c_str(), "bigFont.fnt");
    valLbl->setColor(kValueColor);
    valLbl->limitLabelWidth(labelW, 0.34f, 0.12f);
    valLbl->setPosition({faceW / 2.f, 12.f});
    face->addChild(valLbl);
    if (outLabel) *outLabel = valLbl;

    WeakRef<CCNode> rowRef = row;
    auto select = [state, opts, cb, valLbl, labelW, rowRef](int selected) {
        auto owner = rowRef.lock();
        if (!owner || !owner->getParent() || selected < 0 || selected >= static_cast<int>(opts->size())) return;
        *state = selected;
        valLbl->setString((*opts)[selected].c_str());
        valLbl->limitLabelWidth(labelW, 0.34f, 0.12f);
        if (*cb) (*cb)(selected);
    };
    auto* menu = makeRowMenu(row);
    auto* valueBtn = CCMenuItemExt::createSpriteExtra(face, [opts, state, select](CCMenuItemSpriteExtra*) {
        if (opts->empty()) return;
        if (auto* picker = OptionPickerPopup::create(*opts, *state, select)) picker->show();
    });
    valueBtn->setPosition({faceX, cy});
    valueBtn->setEnabled(!opts->empty());
    valueBtn->m_scaleMultiplier = 1.03f;
    menu->addChild(valueBtn);
    for (int direction : {-1, 1}) {
        auto* arrow = ui::makeFrameButton("GJ_arrow_01_001.png", 0.4f, [state, opts, select, direction] {
            if (opts->empty()) return;
            int const n = static_cast<int>(opts->size());
            select((*state + direction + n) % n);
        });
        if (direction > 0) static_cast<CCSprite*>(arrow->getNormalImage())->setFlipX(true);
        arrow->setPosition({faceX + direction * (faceW / 2.f + kArrowW / 2.f), cy});
        arrow->setSizeMult(1.5f);
        arrow->setEnabled(opts->size() > 1);
        menu->addChild(arrow);
    }
    if (onGear) {
        auto* gear = ui::makeFrameButton("GJ_optionsBtn_001.png", 0.38f, std::move(onGear));
        gear->setPosition({zoneLeft + zoneW - gearW / 2.f, cy});
        menu->addChild(gear);
    }
    return row;
}

CCNode* makeButtonRow(
    float width,
    char const* title, char const* desc,
    char const* buttonText,
    std::function<void()> onPress
) {
    constexpr float kButtonW = 96.f;
    bool const stacked = width < 300.f;
    float const textMaxW = std::max(40.f, stacked ? width - 24.f : width - kButtonW - 28.f);
    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, textMaxW, descH);
    float const rowH = std::max(34.f, 30.f + descH) + (stacked ? 28.f : 0.f);
    auto* row = makeRowNode(width, rowH);
    addRowText(row, makeTitleLabel(title, textMaxW), descLbl,
        descLbl || stacked ? rowH - 8.f : rowH / 2.f + 6.f);
    auto* menu = makeRowMenu(row);
    auto* button = ui::makeButton(buttonText, std::move(onPress), ui::Btn::Green, kButtonW, 0.65f);
    button->setPosition({width - 10.f - kButtonW / 2.f, stacked ? 16.f : rowH / 2.f});
    menu->addChild(button);
    return row;
}

CCNode* makeColorRow(
    float width,
    char const* title, char const* desc,
    ccColor3B value,
    std::function<void(ccColor3B)> onChange,
    CCSprite** outSwatch
) {
    constexpr float kSwatch = 24.f;
    float const textMaxW = width - 60.f;
    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, textMaxW, descH);
    float const rowH = std::max(8.f + 14.f + descH + 8.f, 34.f);

    auto* row = makeRowNode(width, rowH);
    addRowText(row, makeTitleLabel(title, textMaxW), descLbl, descLbl ? rowH - 8.f : rowH / 2.f + 6.f);

    auto* menu = makeRowMenu(row);
    // GD's color channel button is white, so tinting shows the exact RGB.
    auto* swatch = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
    auto size = swatch->getContentSize();
    if (size.width > 0.f) swatch->setScale(kSwatch / size.width);
    swatch->setColor(value);
    if (outSwatch) *outSwatch = swatch;

    // The picker can outlive a rebuilt row.
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
    btn->setPosition({width - 12.f - kSwatch / 2.f, rowH / 2.f});
    menu->addChild(btn);
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
    constexpr float kHeaderH = 20.f;
    constexpr float kGap = 4.f;

    float contentH = 0.f;
    for (auto* r : rows) if (r) contentH += r->getContentSize().height + kGap;
    if (!rows.empty()) contentH -= kGap;

    bool const hasTitle = title && title[0] != '\0';
    float const cardH = kPad + (hasTitle ? kHeaderH + 4.f : 0.f) + contentH + kPad;

    auto* card = CCNode::create();
    card->setAnchorPoint({0.f, 0.f});
    card->setContentSize({width, cardH});
    card->addChild(ui::makeInset({width, cardH}, kCardAlpha), -1);

    float y = cardH - kPad;
    if (hasTitle) {
        auto* titleLbl = ui::makeTitle(title, width - 24.f, 0.5f);
        titleLbl->setAnchorPoint({0.f, 1.f});
        titleLbl->setPosition({11.f, y + 1.f});
        card->addChild(titleLbl);
        auto* line = ui::makeDivider(width - 16.f, accent, 120);
        line->setPosition({width / 2.f, y - kHeaderH + 1.f});
        card->addChild(line);
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
    bool const es = Localization::get().getLanguage() == Localization::Language::SPANISH;
    label->setString(on ? (es ? "Activado" : "Enabled") : (es ? "Desactivado" : "Disabled"));
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
    constexpr float kPad = 8.f;
    constexpr float kTitleH = 17.f;
    float const textMaxW = std::max(40.f, width - 140.f);

    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, textMaxW, descH);
    float const rowH = std::max(kPad + kTitleH + descH + kPad, 42.f);

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});
    row->addChild(ui::makeInset({width, rowH}, kCardAlpha), -1);

    auto* titleLbl = makeTitleLabel(title, textMaxW, 0.46f);
    titleLbl->setPosition({12.f, descLbl ? rowH - kPad : rowH / 2.f + 8.f});
    row->addChild(titleLbl);
    if (descLbl) {
        descLbl->setPosition({12.f, rowH - kPad - kTitleH - 1.f});
        row->addChild(descLbl);
    }

    auto* stateLbl = CCLabelBMFont::create("", "bigFont.fnt");
    stateLbl->setAnchorPoint({1.f, 0.5f});
    stateLbl->setScale(0.3f);
    setHeroStateLabel(stateLbl, value);
    stateLbl->setPosition({width - 46.f, rowH / 2.f});
    row->addChild(stateLbl);

    auto wrapped = [cb = std::move(onChange), stateLbl](bool v) {
        setHeroStateLabel(stateLbl, v);
        if (cb) cb(v);
    };

    auto* menu = makeRowMenu(row);
    auto* tog = addStandardToggler(menu, value, 0.72f, {width - 24.f, rowH / 2.f}, std::move(wrapped));
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

    int const n = std::max<int>(1, static_cast<int>(labels.size()));
    float const tabW = (width - kGap * static_cast<float>(n - 1)) / static_cast<float>(n);

    struct TabState {
        std::vector<CCMenuItemSpriteExtra*> buttons;
        int selected = 0;
        void restyle() {
            for (size_t i = 0; i < buttons.size(); ++i) {
                ui::setButtonSkin(buttons[i], static_cast<int>(i) == selected ? ui::Btn::Green : ui::Btn::Gray);
            }
        }
    };
    auto state = std::make_shared<TabState>();
    state->selected = std::clamp(selected, 0, n - 1);
    auto cb = std::make_shared<std::function<void(int)>>(std::move(onSelect));

    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        auto* btn = ui::makeButton(labels[static_cast<size_t>(i)].c_str(), [state, cb, i] {
            if (state->selected == i) return;
            state->selected = i;
            state->restyle();
            if (*cb) (*cb)(i);
        }, ui::Btn::Gray, tabW, 0.75f);
        btn->setPosition({static_cast<float>(i) * (tabW + kGap) + tabW / 2.f, barH / 2.f});
        menu->addChild(btn);
        state->buttons.push_back(btn);
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

    CCPoint mousePos = scrollLayer->convertToNodeSpace(geode::cocos::getMousePos());
    CCRect scrollRect{{0.f, 0.f}, scrollLayer->getContentSize()};
    if (!scrollRect.containsPoint(mousePos)) return false;

    auto* contentLayer = scrollLayer->m_contentLayer;
    if (!contentLayer) return false;
    if (scrollLayer->m_touchDown) {
        targetSet = false;
        return true;
    }

    float amount = y;
    if (std::abs(amount) < 0.001f) amount = -x;

    scrollLayer->enableScrollWheel(false);
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
    if (scrollLayer->m_touchDown) { targetSet = false; return; }
    if (!std::isfinite(dt) || dt <= 0.f) return;
    auto* contentLayer = scrollLayer->m_contentLayer;
    if (!contentLayer) { targetSet = false; return; }

    float const minY = std::min(0.f, scrollLayer->getContentSize().height - contentLayer->getContentSize().height);
    targetY = std::clamp(targetY, minY, 0.f);
    float cur = contentLayer->getPositionY();
    if (!paimon::ui::motionEnabled()) {
        contentLayer->setPositionY(targetY);
        targetSet = false;
        return;
    }
    float diff = targetY - cur;
    if (std::abs(diff) < 0.5f) {
        contentLayer->setPositionY(targetY);
        targetSet = false;
        return;
    }
    float t = 1.f - std::exp(-14.f * std::min(dt, 0.1f));
    contentLayer->setPositionY(cur + diff * t);
}

void showAbove(FLAlertLayer* alert, CCNode* owner) {
    if (!alert) return;
    int const above = owner ? owner->getZOrder() + 1 : 100;
    alert->m_ZOrder = above;
    alert->show();
    // Some alert implementations ignore m_ZOrder during show().
    if (auto* parent = alert->getParent()) parent->reorderChild(alert, above);
}

}
