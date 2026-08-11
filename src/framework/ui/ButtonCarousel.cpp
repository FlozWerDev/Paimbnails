#include "ButtonCarousel.hpp"
#include "../../utils/ScissorClipNode.hpp"
#include "../../utils/SpriteHelper.hpp"
#include <Geode/ui/Layout.hpp>
#include <algorithm>
#include <cmath>
#include <new>

using namespace geode::prelude;

namespace paimon::ui {

namespace {
constexpr float kScrollDuration = 0.28f;
constexpr char const* kInnerMenuID = "paimon-carousel-inner";
constexpr char const* kArrowMenuID = "paimon-carousel-arrows";
}

void ButtonCarousel::scaleToFit(cocos2d::CCNode* node, float target) {
    if (!node) return;
    float maxDim = std::max(node->getContentWidth(), node->getContentHeight());
    if (maxDim > 0.f) node->setScale(target / maxDim);
}

ButtonCarousel* ButtonCarousel::create(
    Orientation orientation, int visibleCount,
    float itemSize, float crossSize, float gap, float arrowSize, int arrowThreshold
) {
    auto ret = new (std::nothrow) ButtonCarousel();
    if (ret && ret->init(orientation, visibleCount, itemSize, crossSize, gap, arrowSize, arrowThreshold)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

ButtonCarousel* ButtonCarousel::wrapMenu(
    cocos2d::CCMenu* source, Orientation orientation, int visibleCount,
    float itemSize, float crossSize, float gap, float arrowSize, int arrowThreshold
) {
    auto carousel = create(orientation, visibleCount, itemSize, crossSize, gap, arrowSize, arrowThreshold);
    if (!carousel) return nullptr;
    carousel->absorbMenuItems(source);
    carousel->rebuild();
    return carousel;
}

bool ButtonCarousel::init(
    Orientation orientation, int visibleCount,
    float itemSize, float crossSize, float gap, float arrowSize, int arrowThreshold
) {
    if (!CCNode::init()) return false;

    m_orientation   = orientation;
    m_visibleCount  = std::max(1, visibleCount);
    m_itemSize      = itemSize;
    m_crossSize     = crossSize;
    m_gap           = gap;
    m_arrowSize     = arrowSize;
    m_arrowThreshold = std::max(arrowThreshold, m_visibleCount + 1);

    this->setAnchorPoint({0.5f, 0.5f});
    this->ignoreAnchorPointForPosition(false);

    // Rebuild determines final size after the item count is known.
    float winCross = m_crossSize;

    auto stencil = CCLayerColor::create({0, 0, 0, 255});
    stencil->setContentSize({winCross, winCross});
    m_clip = ScissorClipNode::create(stencil);
    if (!m_clip) return false;
    m_clip->setContentSize({winCross, winCross});
    this->addChild(m_clip);

    m_innerMenu = CCMenu::create();
    m_innerMenu->setID(kInnerMenuID);
    m_innerMenu->ignoreAnchorPointForPosition(true);
    m_innerMenu->setPosition({0.f, 0.f});
    m_innerMenu->setContentSize(m_clip->getContentSize());
    m_clip->addChild(m_innerMenu);

    m_arrowMenu = CCMenu::create();
    m_arrowMenu->setID(kArrowMenuID);
    m_arrowMenu->ignoreAnchorPointForPosition(true);
    m_arrowMenu->setPosition({0.f, 0.f});
    this->addChild(m_arrowMenu);

    auto makeArrow = [&](bool pointsToStart, cocos2d::SEL_MenuHandler sel) -> CCMenuItemSpriteExtra* {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("navArrowBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png");
        if (!spr) spr = CCSprite::create();
        if (m_orientation == Orientation::Horizontal) {
            spr->setFlipX(!pointsToStart); // next points right
        } else {
            spr->setRotation(pointsToStart ? 90.f : -90.f);
        }
        scaleToFit(spr, m_arrowSize);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
        return btn;
    };

    m_prevArrow = makeArrow(true,  menu_selector(ButtonCarousel::onPrev));
    m_nextArrow = makeArrow(false, menu_selector(ButtonCarousel::onNext));
    if (m_prevArrow) {
        m_prevArrow->setID("paimon-carousel-prev"_spr);
        m_arrowMenu->addChild(m_prevArrow);
    }
    if (m_nextArrow) {
        m_nextArrow->setID("paimon-carousel-next"_spr);
        m_arrowMenu->addChild(m_nextArrow);
    }

    relayout();
    return true;
}

int ButtonCarousel::effectiveSlots() const {
    int n = static_cast<int>(m_items.size());
    if (n <= 0) return m_visibleCount;
    if (n < m_arrowThreshold) return n;
    return m_visibleCount;
}

bool ButtonCarousel::needsArrows() const {
    return static_cast<int>(m_items.size()) >= m_arrowThreshold;
}

void ButtonCarousel::relayout() {
    int slots = effectiveSlots();
    bool arrows = needsArrows();

    float winMain  = slots > 0 ? (slots * m_itemSize + (slots - 1) * m_gap) : m_itemSize;
    float winCross = m_crossSize;
    float arrowReserve = arrows ? (m_arrowSize + m_arrowGap) : 0.f;
    float totalMain = winMain + 2.f * arrowReserve;

    if (m_orientation == Orientation::Horizontal) {
        this->setContentSize({totalMain, winCross});
        m_clip->setContentSize({winMain, winCross});
        m_clip->setPosition({arrowReserve, 0.f});
    } else {
        this->setContentSize({winCross, totalMain});
        m_clip->setContentSize({winCross, winMain});
        m_clip->setPosition({0.f, arrowReserve});
    }

    // Keep the stencil size synchronized with the clip.
    if (auto* stencil = m_clip->getStencil()) {
        stencil->setContentSize(m_clip->getContentSize());
    }

    m_arrowMenu->setContentSize(this->getContentSize());

    auto cs = this->getContentSize();
    if (m_orientation == Orientation::Horizontal) {
        m_prevArrow->setPosition({m_arrowSize * 0.5f, cs.height * 0.5f});
        m_nextArrow->setPosition({cs.width - m_arrowSize * 0.5f, cs.height * 0.5f});
    } else {
        m_prevArrow->setPosition({cs.width * 0.5f, cs.height - m_arrowSize * 0.5f});
        m_nextArrow->setPosition({cs.width * 0.5f, m_arrowSize * 0.5f});
    }

    if (m_prevArrow) m_prevArrow->setVisible(arrows);
    if (m_nextArrow) m_nextArrow->setVisible(arrows);
}

float ButtonCarousel::windowMain() const {
    int slots = effectiveSlots();
    if (slots <= 0) return 0.f;
    return slots * m_itemSize + (slots - 1) * m_gap;
}

void ButtonCarousel::addButton(cocos2d::CCMenuItem* item) {
    if (!item || !m_innerMenu) return;
    // Reparent to the inner menu without losing ownership.
    item->retain();
    item->removeFromParent();
    m_innerMenu->addChild(item);
    item->release();
    m_items.push_back(item);
}

void ButtonCarousel::addButtons(std::vector<cocos2d::CCMenuItem*> const& items) {
    for (auto* it : items) addButton(it);
}

void ButtonCarousel::absorbMenuItems(cocos2d::CCMenu* source) {
    if (!source) return;
    auto children = source->getChildren();
    if (!children) return;

    // Copy before removing; removal mutates the source list.
    std::vector<CCMenuItem*> items;
    for (auto* node : CCArrayExt<CCNode*>(children)) {
        if (auto* item = typeinfo_cast<CCMenuItem*>(node)) {
            items.push_back(item);
        }
    }
    addButtons(items);
}

int ButtonCarousel::maxOffset() const {
    int extra = static_cast<int>(m_items.size()) - effectiveSlots();
    return extra > 0 ? extra : 0;
}

cocos2d::CCPoint ButtonCarousel::itemLocalPos(int index) const {
    float along = m_itemSize * 0.5f + index * strideLen();
    if (m_orientation == Orientation::Horizontal) {
        return {along, m_crossSize * 0.5f};
    }
    float winMain = windowMain();
    return {m_crossSize * 0.5f, winMain - along};
}

cocos2d::CCPoint ButtonCarousel::innerPosForOffset(int offset) const {
    float shift = offset * strideLen();
    if (m_orientation == Orientation::Horizontal) {
        return {-shift, 0.f};
    }
    return {0.f, shift};
}

void ButtonCarousel::rebuild() {
    if (!m_innerMenu) return;

    // Recompute window and arrow size from the current item count.
    relayout();

    // Manual positioning keeps the inner menu's full scroll range.
    int n = static_cast<int>(m_items.size());
    float totalMain = n > 0 ? (n * m_itemSize + (n - 1) * m_gap) : 0.f;

    if (m_orientation == Orientation::Horizontal) {
        m_innerMenu->setContentSize({std::max(totalMain, windowMain()), m_crossSize});
    } else {
        m_innerMenu->setContentSize({m_crossSize, std::max(totalMain, windowMain())});
    }

    for (int i = 0; i < n; ++i) {
        auto* item = m_items[i];
        if (!item) continue;
        item->setPosition(itemLocalPos(i));
    }

    m_offset = std::clamp(m_offset, 0, maxOffset());
    m_innerMenu->setPosition(innerPosForOffset(m_offset));

    updateButtonVisibility(m_offset, 0);
    updateArrowState();
}

void ButtonCarousel::onPrev(cocos2d::CCObject*) {
    if (m_animating) return;
    if (m_offset <= 0) return;
    animateTo(m_offset - 1);
}

void ButtonCarousel::onNext(cocos2d::CCObject*) {
    if (m_animating) return;
    if (m_offset >= maxOffset()) return;
    animateTo(m_offset + 1);
}

void ButtonCarousel::animateTo(int newOffset) {
    newOffset = std::clamp(newOffset, 0, maxOffset());
    if (newOffset == m_offset || !m_innerMenu) return;

    m_offset = newOffset;
    m_animating = true;

    // Include one item beyond each edge for smooth transitions.
    updateButtonVisibility(m_offset, 1);

    auto dest = innerPosForOffset(m_offset);
    m_innerMenu->stopAllActions();
    m_innerMenu->runAction(CCSequence::create(
        CCEaseInOut::create(CCMoveTo::create(kScrollDuration, dest), 2.0f),
        CCCallFunc::create(this, callfunc_selector(ButtonCarousel::onScrollComplete)),
        nullptr
    ));
    updateArrowState();
}

void ButtonCarousel::onScrollComplete() {
    m_animating = false;
    updateButtonVisibility(m_offset, 0);
    updateArrowState();
}

void ButtonCarousel::scrollToIndex(int offset, bool animated) {
    if (animated) {
        animateTo(offset);
    } else {
        m_offset = std::clamp(offset, 0, maxOffset());
        if (m_innerMenu) {
            m_innerMenu->stopAllActions();
            m_innerMenu->setPosition(innerPosForOffset(m_offset));
        }
        m_animating = false;
        updateButtonVisibility(m_offset, 0);
        updateArrowState();
    }
}

void ButtonCarousel::updateButtonVisibility(int offset, int margin) {
    // Only the visible range (plus margin) stays visible and tappable; scissor
    // clipping alone would not prevent off-screen touches.
    int lo = offset - margin;
    int hi = offset + effectiveSlots() + margin;
    int n = static_cast<int>(m_items.size());
    for (int i = 0; i < n; ++i) {
        auto* item = m_items[i];
        if (!item) continue;
        bool inWindow = (i >= lo && i < hi);
        item->setVisible(inWindow);
        item->setEnabled(inWindow);
    }
}

void ButtonCarousel::updateArrowState() {
    auto dim = [](CCMenuItemSpriteExtra* arrow, bool enabled) {
        if (!arrow) return;
        arrow->setEnabled(enabled);
        if (auto* spr = typeinfo_cast<CCSprite*>(arrow->getNormalImage())) {
            spr->setOpacity(enabled ? 255 : 90);
        }
    };
    dim(m_prevArrow, m_offset > 0);
    dim(m_nextArrow, m_offset < maxOffset());
}

}
