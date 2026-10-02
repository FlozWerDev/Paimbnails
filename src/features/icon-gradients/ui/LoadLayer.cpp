#include "LoadLayer.hpp"
#include "GradientLayer.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

LoadLayer* LoadLayer::create(GradientLayer* layer) {
    auto ret = new LoadLayer();

    ret->m_layer = layer;

    if (!ret->init()) {
        delete ret;
        return nullptr;
    }

    ret->autorelease();
    return ret;
}

void LoadLayer::updateGradient(float) {
    if (m_updatedIndex >= m_toggles.size()) {
        unscheduleAllSelectors();
        return;
    }

    for (int i = 0; i < 10; i++) {
        if (m_updatedIndex >= m_toggles.size()) {
            unscheduleAllSelectors();
            return;
        }

        ColorToggle* toggle = m_toggles[m_updatedIndex++];

        // single lookup instead of contains() + at().
        auto it = m_toggleGradients.find(toggle);
        if (it != m_toggleGradients.end())
            toggle->applyGradient(it->second, false, false);
    }
}

void LoadLayer::onSelect(CCObject* sender) {
    auto toggle = static_cast<ColorToggle*>(sender);

    if (toggle == m_selected) return;

    if (m_selected) m_selected->setSelected(false);

    m_selected = toggle;
    m_selected->setSelected(true);
}

void LoadLayer::onLoad(CCObject*) {
    auto it = m_toggleGradients.find(m_selected);

    if (it != m_toggleGradients.end() && m_selected)
        m_layer->load(it->second);

    onClose(nullptr);

    auto toast = Notification::create("Gradient Loaded", NotificationIcon::Success, 0.1f);
    toast->show();
}

void LoadLayer::onDelete(CCObject*) {
    auto it = m_toggleGradients.find(m_selected);

    if (it == m_toggleGradients.end()) return;

    GradientUtils::removeSavedGradient(it->second);

    ColorToggle* doomed = m_selected;
    doomed->getParent()->removeFromParent();
    m_scrollLayer->m_contentLayer->updateLayout();

    m_toggles.erase(std::remove(m_toggles.begin(), m_toggles.end(), doomed), m_toggles.end());
    m_selected = nullptr;

    if (!m_toggles.empty()) {
        onSelect(m_toggles.front());
        return;
    }

    onClose(nullptr);

    auto fresh = create(m_layer);
    fresh->m_noElasticity = true;

    fresh->show();
}

// bottom-bar action button, dimmed when there is nothing to load.
CCMenuItemSpriteExtra* LoadLayer::makeActionButton(const char* label, SEL_MenuHandler callback, const CCPoint& pos, bool usable, const char* texture) {
    auto sprite = ButtonSprite::create(label, "goldFont.fnt", texture, 0.8f);
    sprite->setScale(0.55f);
    sprite->setCascadeOpacityEnabled(true);
    sprite->setOpacity(usable ? 255 : 120);

    auto button = CCMenuItemSpriteExtra::create(sprite, this, callback);
    button->setPosition(pos);
    button->setCascadeOpacityEnabled(true);
    button->setEnabled(usable);

    m_buttonMenu->addChild(button);
    return button;
}

bool LoadLayer::init() {
    PaimonPopup::init(246, 233);

    std::vector<GradientConfig> gradients = GradientUtils::getSavedGradients();
    bool usable = !gradients.empty();

    setTitle("Load Gradient");
    addInfoButton("Load Gradient",
        "Tap a saved gradient to select it, then press <cg>Load</c> to apply it to your "
        "icon. <cr>Delete</c> removes the selected gradient for good.");

    auto bg = NineSlice::create("square02b_001.png");
    bg->setColor({0, 0, 0});
    bg->setOpacity(49);
    bg->setContentSize({204, 159});

    auto border = Border::create(bg, {0, 0, 0}, {204, 159}, {0, 0});
    border->setPosition(m_size / 2.f + ccp(0, 1.5f) - bg->getContentSize() / 2.f);

    m_mainLayer->addChild(border);

    makeActionButton("Load", menu_selector(LoadLayer::onLoad), {161, 21}, usable);
    makeActionButton("Delete", menu_selector(LoadLayer::onDelete), {85, 21}, usable, "GJ_button_06.png");

    auto lbl = CCLabelBMFont::create("No Gradients", "bigFont.fnt");
    lbl->setPosition(border->getPosition() + bg->getContentSize() / 2.f);
    lbl->setScale(0.6f);
    lbl->setOpacity(usable ? 0 : 140);

    m_mainLayer->addChild(lbl);

    m_scrollLayer = ScrollLayer::create({204, 159, 204, 159}, true, true);
    m_scrollLayer->setPosition(border->getPosition());

    auto rows = RowLayout::create();
    rows->setGrowCrossAxis(true);
    rows->setAxisAlignment(AxisAlignment::Start);
    rows->setGap(1.1f);
    m_scrollLayer->m_contentLayer->setLayout(rows);

    m_mainLayer->addChild(m_scrollLayer);

    CCSize size = {30, 30};
    float scale = 1.1f;

    // the first entries paint eagerly; the rest follow lazily on a timer.
    int eager = 0;
    for (auto const& gradient : gradients) {
        auto toggle = ColorToggle::create(this, menu_selector(LoadLayer::onSelect), ColorType::Main, m_layer, false, scale, false);
        toggle->setPosition(size * scale * 0.5f);

        if (eager++ < 100) {
            toggle->applyGradient(gradient, false, false);
        }

        auto container = CCMenu::create();
        container->addChild(toggle);
        container->setContentSize(size * scale);

        m_scrollLayer->m_contentLayer->addChild(container);

        if (m_selected == nullptr) {
            toggle->setSelected(true);
            m_selected = toggle;
        }

        m_toggles.emplace_back(toggle);
        m_toggleGradients[toggle] = gradient;
    }

    m_scrollLayer->m_contentLayer->updateLayout();
    m_scrollLayer->moveToTop();

    if (usable) {
        Scrollbar* scrollbar = Scrollbar::create(m_scrollLayer);
        scrollbar->setPosition({233, m_size.height / 2.f});
        scrollbar->setVisible(usable);

        m_mainLayer->addChild(scrollbar);
    }

    // beyond the eager batch, entries paint a few per frame.
    bool lazy = m_toggles.size() > 100;
    if (lazy)
        schedule(schedule_selector(LoadLayer::updateGradient), 0, kCCRepeatForever, 0);

    return true;
}
