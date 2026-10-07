#include "CommentTextSelector.hpp"
#include "CommentTextLayout.hpp"
#include "Localization.hpp"
#include "SpriteHelper.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon;

namespace {
CommentTextSelector* g_activeSelector = nullptr;
constexpr float kMinSelectionDistance = 6.f;
constexpr float kLongPressDelay = 0.45f;
constexpr float kTouchPadX = 4.f;
constexpr float kTouchPadY = 3.f;

class SelectionMenu : public CCMenu {
public:
    std::function<bool(CCPoint const&)> acceptsTouch;

    static SelectionMenu* create() {
        auto* menu = new SelectionMenu();
        if (menu->init()) {
            menu->autorelease();
            return menu;
        }
        delete menu;
        return nullptr;
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
        if (acceptsTouch && !acceptsTouch(touch->getLocation())) return false;
        return CCMenu::ccTouchBegan(touch, event);
    }
};

int characterClass(std::string const& text, size_t index) {
    auto ch = static_cast<unsigned char>(text[index]);
    if (std::isspace(ch)) return 0;
    return ch >= 0x80 || std::isalnum(ch) || ch == '_' ? 1 : 2;
}
}

CommentTextSelector* CommentTextSelector::create(
    std::string const& text, CCNode* textNode, CCSize const& cellSize) {
    auto* ret = new CommentTextSelector();
    if (ret->init(text, textNode, cellSize)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CommentTextSelector::init(
    std::string const& text, CCNode* textNode, CCSize const& cellSize) {
    if (!CCLayer::init()) return false;
    setAnchorPoint({0.f, 0.f});
    setPosition({0.f, 0.f});
    setID("paimon-text-selector"_spr);
    setTouchMode(kCCTouchesOneByOne);
    setTouchPriority(-130);
    setTouchEnabled(true);

    m_highlight = PaimonDrawNode::create();
    m_highlight->setVisible(false);
    addChild(m_highlight);
    auto* copyMenu = SelectionMenu::create();
    if (!copyMenu) return false;
    copyMenu->acceptsTouch = [this](CCPoint const& point) { return isVisibleAt(point); };
    m_copyMenu = copyMenu;
    m_copyMenu->setPosition({0.f, 0.f});
    m_copyMenu->setTouchPriority(-131);
    m_copyMenu->setVisible(false);
    addChild(m_copyMenu, 1);
    bool spanish = Localization::get().getLanguage() == Localization::Language::SPANISH;
    auto makeButton = [&](char const* labelText, SEL_MenuHandler handler, float x,
                          CCLabelBMFont*& labelOut) {
        auto* background = SpriteHelper::createColorPanel(56.f, 22.f, {45, 90, 175}, 240, 5.f);
        labelOut = CCLabelBMFont::create(labelText, "bigFont.fnt");
        labelOut->limitLabelWidth(48.f, 0.26f, 0.15f);
        labelOut->setPosition({28.f, 11.f});
        background->addChild(labelOut);
        auto* button = CCMenuItemSpriteExtra::create(background, this, handler);
        button->setPosition({x, 0.f});
        m_copyMenu->addChild(button);
    };
    makeButton(spanish ? "Copiar" : "Copy", menu_selector(CommentTextSelector::onCopy), -30.f, m_copyLabel);
    makeButton(spanish ? "Todo" : "All", menu_selector(CommentTextSelector::onSelectAll), 30.f, m_allLabel);
    refresh(text, textNode, cellSize);
    return true;
}

void CommentTextSelector::onExit() {
    unschedule(schedule_selector(CommentTextSelector::onLongPress));
    unlockParentScroll();
    m_touch = nullptr;
    m_selecting = false;
    dismissSelection();
    CCLayer::onExit();
}

void CommentTextSelector::refresh(
    std::string const& text, CCNode* textNode, CCSize const& cellSize) {
    unschedule(schedule_selector(CommentTextSelector::onLongPress));
    unlockParentScroll();
    dismissSelection();
    m_touch = nullptr;
    m_fullText = paimon::text::stripColorCodes(text);
    m_textNode = textNode;
    m_selecting = false;
    m_gestureCancelled = false;
    m_tapCount = 0;
    setContentSize(cellSize);
    m_copyMenu->setContentSize(cellSize);
    m_lines.clear();
    m_textRect = CCRectZero;
}

void CommentTextSelector::registerWithTouchDispatcher() {
    // Mobile must keep receiving list scroll gestures until the long press activates.
#ifdef GEODE_IS_DESKTOP
    constexpr bool swallowTouches = true;
#else
    constexpr bool swallowTouches = false;
#endif
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, getTouchPriority(), swallowTouches);
}

void CommentTextSelector::lockParentScroll() {
    unlockParentScroll();
    for (auto* parent = getParent(); parent; parent = parent->getParent()) {
        auto* scroll = typeinfo_cast<CCScrollLayerExt*>(parent);
        if (!scroll) continue;
        m_parentScroll = scroll;
        m_parentScrollWasDisabled = scroll->m_disableMovement;
        scroll->m_disableMovement = true;
        return;
    }
}

void CommentTextSelector::unlockParentScroll() {
    if (auto scroll = m_parentScroll.lock()) {
        scroll->m_disableMovement = m_parentScrollWasDisabled;
    }
    m_parentScroll = {};
}

bool CommentTextSelector::isVisibleAt(CCPoint const& worldPoint) {
    for (auto* node = static_cast<CCNode*>(this); node; node = node->getParent()) {
        if (!node->isVisible()) return false;
        if (auto* scroll = typeinfo_cast<CCScrollLayerExt*>(node)) {
            auto size = scroll->getContentSize();
            if (scroll->m_cutContent && !CCRect(0.f, 0.f, size.width, size.height).containsPoint(
                    scroll->convertToNodeSpace(worldPoint))) return false;
        }
    }
    CCNode* topAlert = nullptr;
    if (auto* scene = CCDirector::get()->getRunningScene()) {
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (!child->isVisible() || !typeinfo_cast<FLAlertLayer*>(child)) continue;
            if (!topAlert || child->getZOrder() > topAlert->getZOrder() ||
                (child->getZOrder() == topAlert->getZOrder() &&
                 child->getOrderOfArrival() > topAlert->getOrderOfArrival())) topAlert = child;
        }
    }
    if (!topAlert) return true;
    for (auto* node = static_cast<CCNode*>(this); node; node = node->getParent()) {
        if (node == topAlert) return true;
    }
    return false;
}

bool CommentTextSelector::ccTouchBegan(CCTouch* touch, CCEvent*) {
    auto textNode = m_textNode.lock();
    if (m_touch || !textNode || !textNode->getParent() || !textNode->isVisible() ||
        !isVisibleAt(touch->getLocation())) return false;
    if (m_lines.empty()) rebuildLayoutCache();
    auto point = convertTouchToNodeSpace(touch);
    if (m_lines.empty() || !getExpandedTextRect().containsPoint(point)) {
        if (g_activeSelector == this) dismissSelection();
        return false;
    }
    if (g_activeSelector && g_activeSelector != this) {
        if (g_activeSelector->m_touch) return false;
        g_activeSelector->dismissSelection();
    }
    dismissSelection();
    m_touch = touch;
    m_startPos = m_endPos = point;
    m_startIndex = m_endIndex = pointToTextIndex(point);
    m_gestureCancelled = false;
#ifdef GEODE_IS_DESKTOP
    g_activeSelector = this;
    m_selecting = true;
    lockParentScroll();
#else
    m_selecting = false;
    scheduleOnce(schedule_selector(CommentTextSelector::onLongPress), kLongPressDelay);
#endif
    return true;
}

void CommentTextSelector::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_touch || touch->getID() != m_touch->getID() || m_gestureCancelled) return;
    auto point = convertTouchToNodeSpace(touch);
    if (!m_selecting) {
        if (ccpDistance(touch->getStartLocation(), touch->getLocation()) >= kMinSelectionDistance) {
            unschedule(schedule_selector(CommentTextSelector::onLongPress));
            m_gestureCancelled = true;
        }
        return;
    }
    if (ccpDistance(m_startPos, point) >= kMinSelectionDistance) cancelMentionTouches();
    updateSelection(point);
}

void CommentTextSelector::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (!m_touch || touch->getID() != m_touch->getID()) return;
    unschedule(schedule_selector(CommentTextSelector::onLongPress));
    auto point = convertTouchToNodeSpace(touch);
    bool dragged = ccpDistance(touch->getStartLocation(), touch->getLocation()) >= kMinSelectionDistance;
    bool hasSelection = m_selecting && m_startIndex != m_endIndex;
    if (m_selecting && dragged) {
        cancelMentionTouches();
        updateSelection(point);
        hasSelection = m_startIndex != m_endIndex;
    }
    unlockParentScroll();
    m_selecting = false;
    m_touch = nullptr;
    if (m_gestureCancelled) {
        dismissSelection();
        return;
    }
    if (hasSelection) {
        m_tapCount = 0;
        updateHighlight();
        showCopyMenu();
        return;
    }
    if (dragged) {
        dismissSelection();
        m_tapCount = 0;
        return;
    }
    auto now = std::chrono::steady_clock::now();
    bool repeated = std::chrono::duration<float>(now - m_lastTap).count() < 0.35f &&
                    ccpDistance(point, m_lastTapPosition) < 10.f;
    m_tapCount = repeated ? m_tapCount + 1 : 1;
    m_lastTap = now;
    m_lastTapPosition = point;
    if (m_tapCount == 2) {
        selectWord(point);
        showCopyMenu();
    } else if (m_tapCount >= 3) {
        onSelectAll(nullptr);
        m_tapCount = 0;
    } else {
        dismissSelection();
        g_activeSelector = this;
#ifdef GEODE_IS_DESKTOP
        activateMention(point);
#endif
    }
}

void CommentTextSelector::ccTouchCancelled(CCTouch* touch, CCEvent*) {
    if (!m_touch || touch->getID() != m_touch->getID()) return;
    unschedule(schedule_selector(CommentTextSelector::onLongPress));
    unlockParentScroll();
    m_touch = nullptr;
    m_selecting = false;
    dismissSelection();
}

void CommentTextSelector::onLongPress(float) {
    if (!m_touch || m_gestureCancelled || !isVisibleAt(m_touch->getLocation())) return;
    g_activeSelector = this;
    m_selecting = true;
    lockParentScroll();
    cancelMentionTouches();
    selectWord(m_startPos);
}

void CommentTextSelector::cancelMentionTouches() {
    auto textNode = m_textNode.lock();
    if (!textNode || !m_touch) return;
    for (auto* child : CCArrayExt<CCNode*>(textNode->getChildren())) {
        if (auto* menu = typeinfo_cast<CCMenu*>(child);
            menu && menu->m_eState == kCCMenuStateTrackingTouch) {
            if (menu->m_pSelectedItem) menu->m_pSelectedItem->unselected();
            // The dispatcher still owes the menu its touch-ended callback.
            menu->m_pSelectedItem = nullptr;
        }
    }
}

void CommentTextSelector::activateMention(CCPoint const& point) {
    auto textNode = m_textNode.lock();
    if (!textNode) return;
    auto world = convertToWorldSpace(point);
    for (auto* child : CCArrayExt<CCNode*>(textNode->getChildren())) {
        auto* menu = typeinfo_cast<CCMenu*>(child);
        if (!menu) continue;
        for (auto* node : CCArrayExt<CCNode*>(menu->getChildren())) {
            auto* item = typeinfo_cast<CCMenuItem*>(node);
            if (!item || !item->isEnabled()) continue;
            auto size = item->getContentSize();
            if (CCRect(0.f, 0.f, size.width, size.height).containsPoint(item->convertToNodeSpace(world))) {
                item->activate();
                return;
            }
        }
    }
}

void CommentTextSelector::rebuildLayoutCache() {
    m_lines.clear();
    m_textRect = CCRectZero;
    auto textNode = m_textNode.lock();
    if (!textNode || !textNode->getParent()) return;

    auto addSegment = [&](size_t start, size_t end, CCRect const& bounds) {
        float center = bounds.getMidY();
        auto line = std::find_if(m_lines.begin(), m_lines.end(), [&](DisplayLine const& entry) {
            return std::abs(entry.bounds.getMidY() - center) <
                   std::max(2.f, std::min(entry.bounds.size.height, bounds.size.height) * 0.25f);
        });
        DisplaySegment segment{start, end, bounds};
        if (line == m_lines.end()) m_lines.push_back({bounds, {segment}});
        else {
            line->bounds = paimon::text::unionRect(line->bounds, bounds);
            line->segments.push_back(segment);
        }
    };
    auto addLabel = [&](CCLabelBMFont* label, size_t start, size_t end) {
        for (auto const& glyph : paimon::text::labelGlyphs(label, m_fullText, start, end)) {
            addSegment(glyph.start, glyph.end, paimon::text::convertRect(label, this, glyph.bounds));
        }
    };

    if (auto* area = typeinfo_cast<TextArea*>(textNode.data())) {
        size_t offset = 0;
        if (area->m_label && area->m_label->m_lines) {
            for (auto* line : CCArrayExt<CCLabelBMFont*>(area->m_label->m_lines)) {
                std::string lineText = paimon::text::stripColorCodes(line->getString());
                if (lineText.empty()) continue;
                auto start = m_fullText.find(lineText, offset);
                if (start == std::string::npos) continue;
                offset = start + lineText.size();
                DisplayLine display;
                bool haveBounds = false;
                for (auto const& glyph : paimon::text::labelGlyphs(line, m_fullText, start, offset)) {
                    auto bounds = paimon::text::convertRect(line, this, glyph.bounds);
                    display.bounds = haveBounds ? paimon::text::unionRect(display.bounds, bounds) : bounds;
                    display.segments.push_back({glyph.start, glyph.end, bounds});
                    haveBounds = true;
                }
                if (haveBounds) m_lines.push_back(std::move(display));
            }
        }
    } else if (auto* label = typeinfo_cast<CCLabelBMFont*>(textNode.data())) {
        addLabel(label, 0, m_fullText.size());
    } else {
        auto visit = [&](auto const& self, CCNode* node) -> void {
            if (auto* range = typeinfo_cast<paimon::text::CommentTextRange*>(
                    node->getUserObject("paimon-comment-text-range"))) {
                if (range->atomic) {
                    auto size = node->getContentSize();
                    addSegment(range->start, range->end, paimon::text::convertRect(
                        node, this, {0.f, 0.f, size.width, size.height}));
                } else if (auto* label = typeinfo_cast<CCLabelBMFont*>(node)) {
                    addLabel(label, range->start, range->end);
                }
                return;
            }
            for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) self(self, child);
        };
        visit(visit, textNode.data());
    }
    std::sort(m_lines.begin(), m_lines.end(), [](DisplayLine const& first, DisplayLine const& second) {
        return first.bounds.getMidY() > second.bounds.getMidY();
    });
    for (auto& line : m_lines) {
        std::sort(line.segments.begin(), line.segments.end(),
            [](DisplaySegment const& first, DisplaySegment const& second) {
                return first.bounds.getMinX() < second.bounds.getMinX();
            });
    }
    if (!m_lines.empty()) {
        m_textRect = m_lines.front().bounds;
        for (auto const& line : m_lines) m_textRect = paimon::text::unionRect(m_textRect, line.bounds);
    }
}

CCRect CommentTextSelector::getExpandedTextRect() const {
    return {m_textRect.origin.x - kTouchPadX, m_textRect.origin.y - kTouchPadY,
            m_textRect.size.width + kTouchPadX * 2.f, m_textRect.size.height + kTouchPadY * 2.f};
}

size_t CommentTextSelector::pointToTextIndex(CCPoint const& point) const {
    if (m_lines.empty()) return 0;
    if (point.y > m_textRect.getMaxY() + kTouchPadY) return 0;
    if (point.y < m_textRect.getMinY() - kTouchPadY) return m_fullText.size();
    auto line = std::min_element(m_lines.begin(), m_lines.end(),
        [&](DisplayLine const& first, DisplayLine const& second) {
            return std::abs(first.bounds.getMidY() - point.y) < std::abs(second.bounds.getMidY() - point.y);
        });
    for (auto const& segment : line->segments) {
        if (point.x < segment.bounds.getMidX()) return segment.rawStart;
    }
    return line->segments.back().rawEnd;
}

void CommentTextSelector::updateSelection(CCPoint const& point) {
    m_endPos = point;
    m_endIndex = pointToTextIndex(point);
    updateHighlight();
}

void CommentTextSelector::selectWord(CCPoint const& point) {
    if (m_fullText.empty()) return;
    auto index = pointToTextIndex(point);
    for (auto const& line : m_lines) {
        for (auto const& segment : line.segments) {
            if (segment.bounds.containsPoint(point)) {
                index = segment.rawStart;
                break;
            }
        }
    }
    if (index == m_fullText.size()) index = paimon::text::prevCharacter(m_fullText, index);
    for (auto const& line : m_lines) {
        for (auto const& segment : line.segments) {
            if (index >= segment.rawStart && index < segment.rawEnd &&
                segment.rawEnd > paimon::text::nextCharacter(m_fullText, segment.rawStart)) {
                g_activeSelector = this;
                m_startIndex = segment.rawStart;
                m_endIndex = segment.rawEnd;
                updateHighlight();
                return;
            }
        }
    }
    int kind = characterClass(m_fullText, index);
    size_t start = index;
    size_t end = paimon::text::nextCharacter(m_fullText, index);
    while (start > 0) {
        size_t previous = paimon::text::prevCharacter(m_fullText, start);
        if (characterClass(m_fullText, previous) != kind) break;
        start = previous;
    }
    while (end < m_fullText.size() && characterClass(m_fullText, end) == kind) {
        end = paimon::text::nextCharacter(m_fullText, end);
    }
    g_activeSelector = this;
    m_startIndex = start;
    m_endIndex = end;
    m_endPos = point;
    updateHighlight();
}

void CommentTextSelector::updateHighlight() {
    m_highlight->clear();
    size_t start = std::min(m_startIndex, m_endIndex);
    size_t end = std::max(m_startIndex, m_endIndex);
    for (auto const& line : m_lines) {
        bool found = false;
        CCRect bounds;
        for (auto const& segment : line.segments) {
            if (segment.rawEnd <= start || segment.rawStart >= end) continue;
            bounds = found ? paimon::text::unionRect(bounds, segment.bounds) : segment.bounds;
            found = true;
        }
        if (!found) continue;
        CCPoint points[] = {{bounds.getMinX(), bounds.getMinY()}, {bounds.getMaxX(), bounds.getMinY()},
                            {bounds.getMaxX(), bounds.getMaxY()}, {bounds.getMinX(), bounds.getMaxY()}};
        m_highlight->drawPolygon(points, 4, {0.25f, 0.5f, 1.f, 0.22f}, 0.5f, {0.4f, 0.65f, 1.f, 0.55f});
    }
    m_highlight->setVisible(start != end);
}

void CommentTextSelector::showCopyMenu() {
    if (m_startIndex == m_endIndex) return;
    g_activeSelector = this;
    bool spanish = Localization::get().getLanguage() == Localization::Language::SPANISH;
    m_copyLabel->setString(spanish ? "Copiar" : "Copy");
    m_allLabel->setString(spanish ? "Todo" : "All");
    auto size = getContentSize();
    float x = std::clamp(m_endPos.x, 60.f, std::max(60.f, size.width - 60.f));
    float y = m_endPos.y + 20.f;
    if (y > size.height - 12.f) y = m_endPos.y - 20.f;
    y = std::clamp(y, 12.f, std::max(12.f, size.height - 12.f));
    m_copyMenu->setPosition({x, y});
    m_copyMenu->setVisible(true);
}

std::string CommentTextSelector::getSelectedText() const {
    auto start = std::min(m_startIndex, m_endIndex);
    auto end = std::min(std::max(m_startIndex, m_endIndex), m_fullText.size());
    return start < end ? m_fullText.substr(start, end - start) : "";
}

void CommentTextSelector::onCopy(CCObject*) {
    auto selected = getSelectedText();
    if (!selected.empty()) geode::utils::clipboard::write(selected);
    dismissSelection();
}

void CommentTextSelector::onSelectAll(CCObject*) {
    m_startIndex = 0;
    m_endIndex = m_fullText.size();
    updateHighlight();
    showCopyMenu();
}

void CommentTextSelector::dismissSelection() {
    if (g_activeSelector == this) g_activeSelector = nullptr;
    m_startIndex = m_endIndex = 0;
    if (m_copyMenu) m_copyMenu->setVisible(false);
    if (m_highlight) {
        m_highlight->clear();
        m_highlight->setVisible(false);
    }
}

void CommentTextSelector::attach(CCNode* parent, std::string const& text, CCNode* textNode) {
    if (!parent) return;
    auto* existing = typeinfo_cast<CommentTextSelector*>(parent->getChildByID("paimon-text-selector"_spr));
    if (!textNode || text.empty()) {
        if (existing) existing->removeFromParent();
        return;
    }
    auto size = parent->getContentSize();
    if ((size.width <= 0.f || size.height <= 0.f) && parent->getParent()) size = parent->getParent()->getContentSize();
    if (existing) {
        existing->refresh(text, textNode, size);
        return;
    }
    if (auto* selector = create(text, textNode, size)) parent->addChild(selector, 200);
}

bool CommentTextSelector::handleKeyboard(KeyboardInputData& data) {
    if (!g_activeSelector || data.action != KeyboardInputData::Action::Press) return false;
    auto* selector = g_activeSelector;
    if (!selector->isRunning() || !selector->isVisibleAt(selector->convertToWorldSpace(selector->m_endPos))) return false;
    bool shortcut = (data.modifiers.value & (KeyboardModifier::Control | KeyboardModifier::Super)) != 0;
    if (shortcut && data.key == KEY_C) selector->onCopy(nullptr);
    else if (shortcut && data.key == KEY_A) selector->onSelectAll(nullptr);
    else if (data.key == KEY_Escape) selector->dismissSelection();
    else return false;
    return true;
}

$execute {
    KeyboardInputEvent().listen(&CommentTextSelector::handleKeyboard).leak();
}
