// Color-grid popup: every GD color index as a tappable swatch, in the game's own
// blocks. After zilko's "Icon Gradients" (independent implementation; idea credit zilko144, unlicensed).

#include "ColorSelectLayer.hpp"
#include "GradientLayer.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

namespace {

// The grid is four blocks of four columns: 24px steps inside a block,
// 36px across the gutter between blocks.
float columnX(int col) {
    return 17.f + 24.f * col + 12.f * (col / 4);
}

constexpr float kRowY[] = {
    217.f, 193.f, 169.f, 135.4f, 111.399994f, 87.399994f,
};

constexpr int kColorRows[][16] = {
    {51, 19, 48, 9, 62, 63, 10, 29, 70, 42, 11, 27, 72, 73, 0, 1},
    {37, 53, 54, 55, 26, 59, 60, 61, 71, 14, 31, 45, 105, 28, 32, 20},
    {25, 56, 57, 58, 30, 64, 65, 66, 46, 67, 68, 69, 2, 38, 79, 80},
    {74, 75, 44, 3, 83, 16, 4, 5, 52, 41, 6, 35, 98, 8, 36, 103},
    {40, 76, 77, 78, 22, 39, 84, 50, 47, 23, 92, 93, 7, 13, 24, 104},
    {33, 21, 81, 82, 34, 85, 86, 87, 49, 95, 96, 97, 43, 99, 100, 101},
};

// Ragged bottom row: four slots aligned with the second block, then a
// tail of seven shifted right.
constexpr int kBottomRow[] = {106, 88, 89, 90};
constexpr float kBottomRowY = 63.399994f;
constexpr int kBottomTail[] = {12, 91, 17, 102, 18, 94, 15};
constexpr float kBottomTailX = 269.f;
constexpr float kBottomTailY = 53.799988f;

} // namespace

ColorSelectLayer* ColorSelectLayer::create(GradientLayer* layer) {
    auto ret = new ColorSelectLayer();

    ret->m_layer = layer;

    if (!ret->init()) {
        delete ret;
        return nullptr;
    }

    ret->autorelease();
    return ret;
}

void ColorSelectLayer::onColor(CCObject* sender) {
    if (m_layer) {
        auto item = static_cast<CCMenuItemSpriteExtra*>(sender);
        auto image = static_cast<CCSprite*>(item->getNormalImage());
        m_layer->colorSelected(image->getColor());
    }

    onClose(nullptr);
}

void ColorSelectLayer::createButton(int color, const CCPoint& pos) {
    CCSprite* spr = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
    spr->setColor(GameManager::get()->colorForIdx(color));
    spr->setScale(0.65f);

    auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ColorSelectLayer::onColor));
    btn->setPosition(pos + ccp(10, -23));
    m_buttonMenu->addChild(btn);
}

bool ColorSelectLayer::init() {
    Popup::init(450, 245, "GJ_square05.png");

    setTitle("Select Color");

    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 16; ++col)
            createButton(kColorRows[row][col], ccp(columnX(col), kRowY[row]));
    }

    for (int col = 0; col < 4; ++col)
        createButton(kBottomRow[col], ccp(columnX(col + 4), kBottomRowY));

    for (int k = 0; k < 7; ++k)
        createButton(kBottomTail[k], ccp(kBottomTailX + 24.f * k, kBottomTailY));

    return true;
}
