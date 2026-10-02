#include "PaiConfigKit.hpp"
#include "PaimonPopup.hpp"
#include "../utils/Localization.hpp"
#include "../utils/FluidReveal.hpp"
#include "../utils/SpriteHelper.hpp"

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

CCLabelBMFont* makeDescLabel(char const* text, float wrapW) {
    constexpr float kScale = 0.46f;
    auto* l = CCLabelBMFont::create(text, "chatFont.fnt", wrapW / kScale, kCCTextAlignmentLeft);
    l->setScale(kScale);
    l->setAnchorPoint({0.f, 1.f});
    l->setColor(kDescColor);
    l->setOpacity(255);
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
        // Geode invokes the callback before updating the toggle state.
        if (m_callback && m_toggler) m_callback(!m_toggler->isToggled());
    }
};

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
    auto* tog = paimon::ui::makeSwitch(
        cb, menu_selector(KitToggleCallback::onToggle), value, scale / 0.62f);
    cb->m_toggler = tog;
    tog->toggle(value);
    tog->setPosition(pos);
    tog->setUserObject(cb);
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
    constexpr float kPad = 8.f;
    constexpr float kTitleH = 14.f;
    float textMaxW = std::max(40.f, width - 72.f);

    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, textMaxW, descH);

    float rowH = std::max(36.f, kPad + kTitleH + descH + kPad);

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
    auto* tog = addStandardToggler(menu, value, 0.55f, {width - 30.f, rowH / 2.f},
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
    CCLabelBMFont** outValue
) {
    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, std::max(40.f, width - 24.f), descH);
    float const rowH = 52.f + descH;
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});

    auto* titleLbl = makeTitleLabel(title, std::max(40.f, width - 96.f));
    titleLbl->setPosition({10.f, rowH - 8.f});
    row->addChild(titleLbl);
    if (descLbl) {
        descLbl->setPosition({10.f, rowH - 24.f});
        row->addChild(descLbl);
    }

    auto* valLbl = CCLabelBMFont::create(format ? format(value).c_str() : "", "bigFont.fnt");
    valLbl->setAnchorPoint({1.f, 1.f});
    valLbl->setColor(kValueColor);
    valLbl->limitLabelWidth(72.f, 0.32f, 0.12f);
    valLbl->setPosition({width - 10.f, rowH - 8.f});
    row->addChild(valLbl);

    float const sliderScale = std::clamp((width - 38.f) / 210.f, 0.25f, 1.6f);
    auto* cb = KitSliderCallback::create(std::move(onChange), format, minV, maxV);
    auto* slider = Slider::create(cb, menu_selector(KitSliderCallback::onChanged), sliderScale);
    cb->m_slider = slider;
    cb->m_valueLabel = valLbl;
    slider->setPosition({width / 2.f, 15.f});
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
            row->setContentSize({320.f, 32.f});
            auto* menu = makeRowMenu(row);
            auto* button = paimon::ui::makeButton(m_options[i].c_str(), {320.f, 30.f}, [this, i] {
                Ref<OptionPickerPopup> guard = this;
                auto callback = m_callback;
                this->onClose(nullptr);
                if (callback) callback(static_cast<int>(i));
            }, static_cast<int>(i) == m_selected ? ccColor3B{36, 75, 106} : paimon::ui::palette::raised);
            button->setPosition({160.f, 16.f});
            menu->addChild(button);
            rows.push_back(row);
        }
        if (rows.empty()) rows.push_back(makeHint(320.f,
            Localization::get().getLanguage() == Localization::Language::SPANISH ? "Sin resultados" : "No results"));
        m_scroll = makeScrollStack({320.f, 190.f}, rows, 4.f);
        m_scroll->setPosition({20.f, 14.f});
        m_mainLayer->addChild(m_scroll);
    }

    bool init(std::vector<std::string> options, int selected, std::function<void(int)> callback) {
        if (!PaimonPopup::init(360.f, 270.f)) return false;
        m_options = std::move(options);
        m_selected = selected;
        m_callback = std::move(callback);
        bool const es = Localization::get().getLanguage() == Localization::Language::SPANISH;
        setTitle(es ? "Elegir opcion" : "Choose an option");
        m_search = TextInput::create(320.f, es ? "Buscar..." : "Search...", "chatFont.fnt");
        m_search->setCommonFilter(CommonFilter::Any);
        m_search->setMaxCharCount(64);
        m_search->setPosition({180.f, 225.f});
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
    bool const stacked = width < 330.f;
    float const zoneW = stacked ? width - 24.f : 160.f;
    float const textMaxW = std::max(40.f, stacked ? width - 24.f : width - zoneW - 28.f);
    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, textMaxW, descH);
    float const rowH = std::max(36.f, 30.f + descH) + (stacked ? 30.f : 0.f);
    auto* row = CCNode::create();
    row->setContentSize({width, rowH});
    row->setAnchorPoint({0.f, 0.f});
    auto* titleLbl = makeTitleLabel(title, textMaxW);
    titleLbl->setPosition({10.f, rowH - 8.f});
    row->addChild(titleLbl);
    if (descLbl) {
        descLbl->setPosition({10.f, rowH - 24.f});
        row->addChild(descLbl);
    }

    auto state = std::make_shared<int>(options.empty() ? 0 : std::clamp(index, 0, static_cast<int>(options.size()) - 1));
    auto opts = std::make_shared<std::vector<std::string>>(std::move(options));
    auto cb = std::make_shared<std::function<void(int)>>(std::move(onChange));
    float const cy = stacked ? 17.f : rowH / 2.f;
    float const cx = stacked ? width / 2.f : width - zoneW / 2.f - 10.f;
    float const gearW = onGear ? 28.f : 0.f;
    float const labelW = zoneW - 62.f - gearW;
    auto* face = paimon::ui::makeButtonFace("", {zoneW - gearW, 26.f});
    auto* valLbl = CCLabelBMFont::create(opts->empty() ? "-" : (*opts)[*state].c_str(), "bigFont.fnt");
    valLbl->setColor(kValueColor);
    valLbl->limitLabelWidth(labelW, 0.32f, 0.12f);
    valLbl->setPosition(face->getContentSize() / 2.f);
    face->addChild(valLbl);
    if (outLabel) *outLabel = valLbl;
    WeakRef<CCNode> rowRef = row;
    auto select = [state, opts, cb, valLbl, labelW, rowRef](int selected) {
        auto owner = rowRef.lock();
        if (!owner || !owner->getParent() || selected < 0 || selected >= static_cast<int>(opts->size())) return;
        *state = selected;
        valLbl->setString((*opts)[selected].c_str());
        valLbl->limitLabelWidth(labelW, 0.32f, 0.12f);
        if (*cb) (*cb)(selected);
    };
    auto* menu = makeRowMenu(row);
    auto* valueBtn = CCMenuItemExt::createSpriteExtra(face, [opts, state, select](CCMenuItemSpriteExtra*) {
        if (opts->empty()) return;
        if (auto* picker = OptionPickerPopup::create(*opts, *state, select)) picker->show();
    });
    valueBtn->setPosition({cx - gearW / 2.f, cy});
    valueBtn->setEnabled(!opts->empty());
    valueBtn->m_scaleMultiplier = 1.02f;
    menu->addChild(valueBtn);
    for (int direction : {-1, 1}) {
        auto* arrow = paimon::ui::makeButton(direction < 0 ? "<" : ">", {24.f, 26.f}, [state, opts, select, direction] {
            if (opts->empty()) return;
            int const n = static_cast<int>(opts->size());
            select((*state + direction + n) % n);
        });
        arrow->setPosition({cx - gearW / 2.f + direction * (zoneW - gearW - 24.f) / 2.f, cy});
        arrow->setEnabled(opts->size() > 1);
        menu->addChild(arrow, 2);
    }
    if (onGear) {
        auto* gear = paimon::ui::makeButton("...", {24.f, 26.f}, std::move(onGear));
        gear->setPosition({cx + zoneW / 2.f - 12.f, cy});
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
    bool const stacked = width < 300.f;
    float const textMaxW = std::max(40.f, stacked ? width - 24.f : width - 124.f);
    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, textMaxW, descH);
    float const rowH = std::max(36.f, 30.f + descH) + (stacked ? 30.f : 0.f);
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});
    auto* titleLbl = makeTitleLabel(title, textMaxW);
    titleLbl->setPosition({10.f, rowH - 8.f});
    row->addChild(titleLbl);
    if (descLbl) {
        descLbl->setPosition({10.f, rowH - 24.f});
        row->addChild(descLbl);
    }
    auto* menu = makeRowMenu(row);
    auto* button = paimon::ui::makeButton(buttonText, {100.f, 27.f}, std::move(onPress), {36, 75, 106});
    button->setPosition({width - 62.f, stacked ? 17.f : rowH / 2.f});
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
    constexpr float kPad = 8.f;
    constexpr float kTitleH = 14.f;
    constexpr float kSwatch = 26.f;
    float textMaxW = width - 90.f;

    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, textMaxW, descH);

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

    // A white texture preserves the chosen RGB color.
    auto* swatch = CCSprite::create("square02_001.png");
    if (!swatch) swatch = CCSprite::createWithSpriteFrameName("square02_001.png");
    if (swatch) {
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
    constexpr float kPad = 10.f;
    constexpr float kHeaderH = 20.f;
    constexpr float kGap = 4.f;

    float contentH = 0.f;
    for (auto* r : rows) if (r) contentH += r->getContentSize().height + kGap;
    if (!rows.empty()) contentH -= kGap;

    bool hasTitle = title && title[0] != '\0';
    float cardH = kPad + (hasTitle ? kHeaderH + 4.f : 0.f) + contentH + kPad;

    auto* card = CCNode::create();
    card->setAnchorPoint({0.f, 0.f});
    card->setContentSize({width, cardH});

    auto* panel = paimon::ui::makeSurface({width, cardH}, kCardColor, kCardAlpha);
    if (panel) {
        panel->setAnchorPoint({0.f, 0.f});
        panel->setPosition({0.f, 0.f});
        card->addChild(panel, -1);
    }

    float y = cardH - kPad;
    if (hasTitle) {
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
    constexpr float kPad = 7.f;
    constexpr float kTitleH = 17.f;
    float textMaxW = std::max(40.f, width - 144.f);

    float descH = 0.f;
    auto* descLbl = makeDescBlock(desc, textMaxW, descH);

    float rowH = std::max(kPad + kTitleH + descH + kPad, 40.f);

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowH});

    auto* panel = paimon::ui::makeSurface({width, rowH}, kCardColor, kCardAlpha);
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
    setHeroStateLabel(stateLbl, value);
    stateLbl->setPosition({width - 52.f, rowH / 2.f});
    row->addChild(stateLbl);

    auto wrapped = [cb = std::move(onChange), stateLbl](bool v) {
        setHeroStateLabel(stateLbl, v);
        if (cb) cb(v);
    };

    auto* menu = makeRowMenu(row);
    auto* tog = addStandardToggler(menu, value, 0.62f, {width - 28.f, rowH / 2.f},
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

struct TabBarState {
    std::vector<cocos2d::CCNodeRGBA*> panels;
    std::vector<CCLabelBMFont*> labels;
    int selected = 0;
    CCNode* indicator = nullptr;
    float step = 0.f;

    void restyle(bool animate = false) {
        constexpr ccColor3B kSelPanel   = {36, 75, 106};
        constexpr auto kUnselPanel = paimon::ui::palette::raised;
        for (size_t i = 0; i < panels.size(); ++i) {
            bool sel = static_cast<int>(i) == selected;
            if (panels[i]) {
                panels[i]->setColor(sel ? kSelPanel : kUnselPanel);
                panels[i]->setOpacity(255);
            }
            if (i < labels.size() && labels[i]) {
                labels[i]->setColor(sel ? ccColor3B{255, 255, 255} : kDescColor);
                labels[i]->setOpacity(sel ? 255 : 210);
            }
        }
        if (indicator) {
            CCPoint const position{6.f + selected * step, 1.f};
            indicator->stopAllActions();
            if (animate && paimon::ui::motionEnabled()) {
                indicator->runAction(CCEaseSineOut::create(CCMoveTo::create(
                    paimon::ui::motionDuration(0.16f), position)));
            } else indicator->setPosition(position);
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
    state->step = tabW + kGap;
    state->indicator = CCLayerColor::create({116, 204, 255, 255}, std::max(1.f, tabW - 12.f), 2.f);
    bar->addChild(state->indicator, 6);
    auto cb = std::make_shared<std::function<void(int)>>(std::move(onSelect));

    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        float x0 = static_cast<float>(i) * (tabW + kGap);

        auto* holder = CCNode::create();
        holder->setAnchorPoint({0.5f, 0.5f});
        holder->setContentSize({tabW, barH - 4.f});

        auto* panel = paimon::ui::makeSurface({tabW, barH - 4.f});
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
                state->restyle(true);
                if (*cb) (*cb)(i);
            });
        btn->setPosition({x0 + tabW / 2.f, barH / 2.f});
        btn->m_scaleMultiplier = 1.02f;
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
