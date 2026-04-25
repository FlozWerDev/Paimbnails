#include "CommentTextSelector.hpp"
#include "SpriteHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/TextArea.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_map>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon;

namespace {
CommentTextSelector* g_activeSelector = nullptr;

constexpr float kTouchPaddingX = 8.f;
constexpr float kTouchPaddingY = 6.f;
constexpr float kMinSelectionDistance = 6.f;
constexpr float kLineGap = 3.f;

struct LayoutToken {
    enum class Kind {
        Text,
        Space,
        Newline,
        Emote,
    };

    Kind kind = Kind::Text;
    size_t start = 0;
    size_t end = 0;
    std::string text;
};

std::string stripGDColorCodes(std::string const& text) {
    std::string result;
    result.reserve(text.size());

    size_t index = 0;
    while (index < text.size()) {
        if (text[index] == '<' && index + 1 < text.size()) {
            if (text[index + 1] == 'c' && index + 3 < text.size() && text[index + 3] == '>') {
                index += 4;
                continue;
            }
            if (index + 3 < text.size() && text[index + 1] == '/' && text[index + 2] == 'c' && text[index + 3] == '>') {
                index += 4;
                continue;
            }
        }

        result += text[index];
        ++index;
    }

    return result;
}

bool isValidEmoteName(std::string const& name) {
    if (name.size() < 2) return false;

    for (char ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-') {
            return false;
        }
    }

    return true;
}

bool isGDColorCode(std::string const& inner) {
    if (inner.size() == 2 && inner[0] == 'c') return true;
    return inner == "/c";
}

bool tryConsumeEmoteToken(std::string const& text, size_t start, size_t& end) {
    if (start >= text.size()) return false;

    if (text[start] == ':') {
        auto close = text.find(':', start + 1);
        if (close != std::string::npos && close > start + 1) {
            auto name = text.substr(start + 1, close - start - 1);
            if (isValidEmoteName(name)) {
                end = close + 1;
                return true;
            }
        }
    }

    if (text[start] == '<') {
        auto close = text.find('>', start + 1);
        if (close != std::string::npos && close > start + 1) {
            auto name = text.substr(start + 1, close - start - 1);
            if (!isGDColorCode(name) && isValidEmoteName(name)) {
                end = close + 1;
                return true;
            }
        }
    }

    return false;
}

std::vector<LayoutToken> tokenizeForLayout(std::string const& text) {
    std::vector<LayoutToken> tokens;
    size_t index = 0;

    while (index < text.size()) {
        if (text[index] == '\n') {
            tokens.push_back({LayoutToken::Kind::Newline, index, index + 1, "\n"});
            ++index;
            continue;
        }

        size_t emoteEnd = 0;
        if (tryConsumeEmoteToken(text, index, emoteEnd)) {
            tokens.push_back({
                LayoutToken::Kind::Emote,
                index,
                emoteEnd,
                text.substr(index, emoteEnd - index)
            });
            index = emoteEnd;
            continue;
        }

        bool isSpace = std::isspace(static_cast<unsigned char>(text[index])) != 0;
        auto kind = isSpace ? LayoutToken::Kind::Space : LayoutToken::Kind::Text;
        size_t start = index;

        while (index < text.size()) {
            if (text[index] == '\n') break;
            if (tryConsumeEmoteToken(text, index, emoteEnd)) break;

            bool sameSpaceClass = (std::isspace(static_cast<unsigned char>(text[index])) != 0) == isSpace;
            if (!sameSpaceClass) break;
            ++index;
        }

        tokens.push_back({kind, start, index, text.substr(start, index - start)});
    }

    return tokens;
}
} // namespace

// ─── Factory ───

CommentTextSelector* CommentTextSelector::create(
    std::string const& text, CCNode* textNode, CCSize const& cellSize,
    std::string const& fontFile)
{
    auto ret = new CommentTextSelector();
    if (ret && ret->init(text, textNode, cellSize, fontFile)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// ─── Init ───

bool CommentTextSelector::init(
    std::string const& text, CCNode* textNode, CCSize const& cellSize,
    std::string const& fontFile)
{
    if (!CCLayer::init()) return false;

    this->setContentSize(cellSize);
    this->setAnchorPoint({0.f, 0.f});
    this->setPosition({0.f, 0.f});
    this->setID("paimon-text-selector"_spr);

    // Touch handling — must NOT swallow touches (see registerWithTouchDispatcher)
    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(-90);

    // Highlight overlay (drawn dynamically)
    m_highlight = PaimonDrawNode::create();
    m_highlight->setVisible(false);
    this->addChild(m_highlight, 50);

    // Copy button menu (hidden until selection is made)
    m_copyMenu = CCMenu::create();
    m_copyMenu->setPosition({0.f, 0.f});
    m_copyMenu->setContentSize(cellSize);
    m_copyMenu->setVisible(false);
    this->addChild(m_copyMenu, 100);

    refresh(text, textNode, cellSize, fontFile);

    return true;
}

void CommentTextSelector::onExit() {
    if (g_activeSelector == this) {
        g_activeSelector = nullptr;
    }
    CCLayer::onExit();
}

void CommentTextSelector::refresh(
    std::string const& text,
    CCNode* textNode,
    CCSize const& cellSize,
    std::string const& fontFile)
{
    if (g_activeSelector == this) {
        g_activeSelector = nullptr;
    }

    m_fullText = stripGDColorCodes(text);
    m_fontFile = fontFile.empty() ? "chatFont.fnt" : fontFile;
    m_textNode = textNode;
    m_selecting = false;
    m_startPos = ccp(0.f, 0.f);
    m_endPos = ccp(0.f, 0.f);
    m_startIndex = 0;
    m_endIndex = 0;

    this->setContentSize(cellSize);
    if (m_copyMenu) {
        m_copyMenu->setContentSize(cellSize);
    }

    dismissCopyButton();
    rebuildLayoutCache();
}

// ─── Touch handlers ───

void CommentTextSelector::registerWithTouchDispatcher() {
    // Register WITHOUT swallowing so scroll views & buttons still receive touches
    CCDirector::sharedDirector()->getTouchDispatcher()
        ->addTargetedDelegate(this, getTouchPriority(), false);
}

bool CommentTextSelector::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!m_textNode || !m_textNode->getParent()) return false;

    rebuildLayoutCache();
    if (m_textRect.size.width <= 0.f || m_textRect.size.height <= 0.f) return false;

    if (g_activeSelector && g_activeSelector != this) return false;

    auto touchLocal = this->convertTouchToNodeSpace(touch);
    if (!getExpandedTextRect().containsPoint(touchLocal)) {
        return false;
    }

    dismissCopyButton();

    g_activeSelector = this;
    m_selecting = true;
    updateSelection(touchLocal, true);

    return true;
}

void CommentTextSelector::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_selecting) return;

    updateSelection(this->convertTouchToNodeSpace(touch));
}

void CommentTextSelector::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (!m_selecting) return;

    updateSelection(this->convertTouchToNodeSpace(touch));
    m_selecting = false;
    if (g_activeSelector == this) {
        g_activeSelector = nullptr;
    }

    float dragDist = ccpDistance(m_startPos, m_endPos);
    if (dragDist < kMinSelectionDistance || m_startIndex == m_endIndex) {
        dismissCopyButton();
        return;
    }

    updateHighlight();

    m_copyMenu->removeAllChildren();
    m_copyMenu->setVisible(true);

    auto copyBg = SpriteHelper::createColorPanel(
        50.f, 22.f, {60, 120, 220}, 210, 6.f);
    auto copyLabel = CCLabelBMFont::create("Copy", "bigFont.fnt");
    copyLabel->setScale(0.3f);
    copyLabel->setPosition({25.f, 11.f});
    copyBg->addChild(copyLabel, 1);
    copyBg->setContentSize({50.f, 22.f});

    auto copyBtn = CCMenuItemSpriteExtra::create(
        copyBg, this, menu_selector(CommentTextSelector::onCopy));

    float minBtnX = m_textRect.origin.x + std::min(24.f, m_textRect.size.width * 0.5f);
    float maxBtnX = std::max(minBtnX, m_textRect.getMaxX() - std::min(24.f, m_textRect.size.width * 0.5f));
    float btnX = std::clamp(m_endPos.x, minBtnX, maxBtnX);
    float btnY = std::min(m_textRect.getMaxY() + 14.f, this->getContentSize().height - 15.f);
    copyBtn->setPosition({btnX, btnY});
    m_copyMenu->addChild(copyBtn);
}

void CommentTextSelector::ccTouchCancelled(CCTouch*, CCEvent*) {
    m_selecting = false;
    if (g_activeSelector == this) {
        g_activeSelector = nullptr;
    }
    dismissCopyButton();
}

void CommentTextSelector::rebuildLayoutCache() {
    m_lines.clear();
    m_textRect = CCRectZero;
    m_availableWidth = 0.f;
    m_lineHeight = 0.f;
    m_effectiveFontScale = 1.f;

    if (!m_textNode || !m_textNode->getParent()) return;

    auto nodeSize = m_textNode->getContentSize();
    auto anchor = m_textNode->getAnchorPoint();
    auto position = m_textNode->getPosition();
    float scaleX = m_textNode->getScaleX();
    float scaleY = m_textNode->getScaleY();
    float leftX = position.x - nodeSize.width * anchor.x * scaleX;
    float topY = position.y + nodeSize.height * (1.f - anchor.y) * scaleY;
    auto topLeftWorld = m_textNode->getParent()->convertToWorldSpace({leftX, topY});
    auto topLeftLocal = this->convertToNodeSpace(topLeftWorld);

    float baseScale = std::max(m_textNode->getScale(), 0.01f);
    float baseWidth = nodeSize.width * std::max(scaleX, 0.01f);
    float baseHeight = nodeSize.height * std::max(scaleY, 0.01f);

    if (auto* textArea = typeinfo_cast<TextArea*>(m_textNode)) {
        baseScale = std::max(textArea->getScale(), 0.01f);
        if (textArea->m_width > 0.f) {
            baseWidth = textArea->m_width * std::max(textArea->getScaleX(), 0.01f);
        }
        if (textArea->m_height > 0.f) {
            baseHeight = textArea->m_height * std::max(textArea->getScaleY(), 0.01f);
        }
    }

    m_availableWidth = std::max(baseWidth, 8.f);

    float adjustedScale = baseScale;
    if (m_fullText.size() > 80) {
        float reduction = std::min(static_cast<float>(m_fullText.size() - 80) * 0.004f, 0.25f);
        adjustedScale = baseScale * (1.f - reduction);
    }

    auto measureFontHeight = [](std::string const& fontFile, float scale) {
        auto probe = CCLabelBMFont::create("Ag", fontFile.c_str());
        if (!probe) {
            probe = CCLabelBMFont::create("Ag", "chatFont.fnt");
        }
        if (!probe) return 16.f * std::max(scale, 0.01f);
        return probe->getContentSize().height * std::max(scale, 0.01f);
    };

    bool hasOverlay = this->getParent() && this->getParent()->getChildByID("paimon-emote-overlay"_spr);
    if (hasOverlay || m_fontFile != "chatFont.fnt") {
        m_effectiveFontScale = adjustedScale;
        float refHeight = measureFontHeight("chatFont.fnt", adjustedScale);
        float emoteHeight = refHeight * 1.2f;
        m_lineHeight = std::max(emoteHeight, refHeight) + kLineGap;
    } else {
        m_effectiveFontScale = adjustedScale;
        float fontHeight = measureFontHeight(m_fontFile, m_effectiveFontScale);
        m_lineHeight = std::max(fontHeight + 2.f, 12.f);

        if (auto* textArea = typeinfo_cast<TextArea*>(m_textNode)) {
            if (textArea->m_label && textArea->m_label->m_lines && textArea->m_label->m_lines->count() > 0) {
                float lineCount = static_cast<float>(textArea->m_label->m_lines->count());
                if (lineCount > 0.f) {
                    m_lineHeight = std::max(baseHeight / lineCount, m_lineHeight);
                }
            } else {
                m_lineHeight = std::max(baseHeight, m_lineHeight);
            }
        } else {
            m_lineHeight = std::max(baseHeight, m_lineHeight);
        }
    }

    std::unordered_map<unsigned char, float> charWidthCache;
    auto measureTextWidth = [&](std::string const& text) {
        if (text.empty()) return 0.f;
        auto label = CCLabelBMFont::create(text.c_str(), m_fontFile.c_str());
        if (!label) {
            label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
        }
        if (!label) {
            return static_cast<float>(text.size()) * 6.f * std::max(m_effectiveFontScale, 0.01f);
        }
        return label->getContentSize().width * std::max(m_effectiveFontScale, 0.01f);
    };

    auto measureCharWidth = [&](char ch) {
        auto key = static_cast<unsigned char>(ch);
        if (auto found = charWidthCache.find(key); found != charWidthCache.end()) {
            return found->second;
        }

        float width = measureTextWidth(std::string(1, ch));
        charWidthCache.emplace(key, width);
        return width;
    };

    auto tokens = tokenizeForLayout(m_fullText);
    DisplayLine currentLine;
    currentLine.rawStart = 0;
    float cursorX = 0.f;
    float emoteWidth = std::max(m_lineHeight - kLineGap, 10.f);
    float emoteAdvance = emoteWidth + 2.f;

    auto finalizeLine = [&](size_t nextRawStart) {
        currentLine.width = cursorX;
        if (!currentLine.segments.empty()) {
            currentLine.rawEnd = currentLine.segments.back().rawEnd;
        } else {
            currentLine.rawEnd = currentLine.rawStart;
        }
        m_lines.push_back(std::move(currentLine));
        currentLine = DisplayLine();
        currentLine.rawStart = nextRawStart;
        cursorX = 0.f;
    };

    for (auto const& token : tokens) {
        if (token.kind == LayoutToken::Kind::Newline) {
            finalizeLine(token.end);
            continue;
        }

        float tokenWidth = token.kind == LayoutToken::Kind::Emote
            ? emoteAdvance
            : measureTextWidth(token.text);

        if (cursorX + tokenWidth > m_availableWidth && cursorX > 0.f) {
            finalizeLine(token.start);
            if (token.kind == LayoutToken::Kind::Space) {
                currentLine.rawStart = token.end;
                continue;
            }
        }

        if (token.kind == LayoutToken::Kind::Emote) {
            currentLine.segments.push_back({token.start, token.end, cursorX, cursorX + emoteWidth});
            cursorX += emoteAdvance;
            continue;
        }

        for (size_t index = token.start; index < token.end; ++index) {
            float charWidth = measureCharWidth(m_fullText[index]);
            currentLine.segments.push_back({index, index + 1, cursorX, cursorX + charWidth});
            cursorX += charWidth;
        }
    }

    if (!tokens.empty() || m_fullText.empty()) {
        finalizeLine(m_fullText.size());
    }

    if (m_lines.empty()) {
        m_lines.push_back(DisplayLine());
    }

    float layoutHeight = std::max(baseHeight, m_lineHeight * static_cast<float>(m_lines.size()));
    m_textRect = CCRect(topLeftLocal.x, topLeftLocal.y - layoutHeight, m_availableWidth, layoutHeight);
}

cocos2d::CCRect CommentTextSelector::getExpandedTextRect() const {
    return CCRect(
        m_textRect.origin.x - kTouchPaddingX,
        m_textRect.origin.y - kTouchPaddingY,
        m_textRect.size.width + kTouchPaddingX * 2.f,
        m_textRect.size.height + kTouchPaddingY * 2.f
    );
}

cocos2d::CCPoint CommentTextSelector::clampToTextRect(CCPoint const& point) const {
    if (m_textRect.size.width <= 0.f || m_textRect.size.height <= 0.f) return point;

    return {
        std::clamp(point.x, m_textRect.origin.x, m_textRect.getMaxX()),
        std::clamp(point.y, m_textRect.origin.y, m_textRect.getMaxY())
    };
}

size_t CommentTextSelector::pointToTextIndex(CCPoint const& point) const {
    if (m_lines.empty()) return 0;

    auto clamped = clampToTextRect(point);
    float relativeY = m_textRect.getMaxY() - clamped.y;
    int lineIndex = static_cast<int>(relativeY / std::max(m_lineHeight, 1.f));
    lineIndex = std::clamp(lineIndex, 0, static_cast<int>(m_lines.size()) - 1);

    auto const& line = m_lines[static_cast<size_t>(lineIndex)];
    if (line.segments.empty()) return line.rawStart;

    float lineX = clamped.x - m_textRect.origin.x;
    if (lineX <= line.segments.front().startX) {
        return line.segments.front().rawStart;
    }

    for (auto const& segment : line.segments) {
        float midpoint = (segment.startX + segment.endX) * 0.5f;
        if (lineX < midpoint) return segment.rawStart;
        if (lineX <= segment.endX) return segment.rawEnd;
    }

    return line.rawEnd;
}

void CommentTextSelector::updateSelection(CCPoint const& point, bool resetStart) {
    auto clampedPoint = clampToTextRect(point);
    size_t index = pointToTextIndex(clampedPoint);

    if (resetStart) {
        m_startPos = clampedPoint;
        m_startIndex = index;
    }

    m_endPos = clampedPoint;
    m_endIndex = index;
    updateHighlight();
}

// ─── Highlight ───

void CommentTextSelector::updateHighlight() {
    if (!m_highlight || m_lines.empty()) return;
    m_highlight->clear();

    size_t rawStart = std::min(m_startIndex, m_endIndex);
    size_t rawEnd = std::max(m_startIndex, m_endIndex);
    if (rawStart == rawEnd) {
        m_highlight->setVisible(false);
        return;
    }

    struct Cursor {
        size_t lineIndex = 0;
        float x = 0.f;
    };

    auto locateIndex = [&](size_t rawIndex) {
        Cursor cursor;

        for (size_t lineIndex = 0; lineIndex < m_lines.size(); ++lineIndex) {
            auto const& line = m_lines[lineIndex];
            cursor.lineIndex = lineIndex;

            if (line.segments.empty()) {
                cursor.x = 0.f;
                if (rawIndex <= line.rawEnd || lineIndex + 1 == m_lines.size()) {
                    return cursor;
                }
                continue;
            }

            if (rawIndex <= line.segments.front().rawStart) {
                cursor.x = line.segments.front().startX;
                return cursor;
            }

            for (auto const& segment : line.segments) {
                if (rawIndex <= segment.rawStart) {
                    cursor.x = segment.startX;
                    return cursor;
                }
                if (rawIndex <= segment.rawEnd) {
                    cursor.x = segment.endX;
                    return cursor;
                }
            }

            cursor.x = line.width;
            if (rawIndex <= line.rawEnd || lineIndex + 1 == m_lines.size()) {
                return cursor;
            }
        }

        return cursor;
    };

    auto startCursor = locateIndex(rawStart);
    auto endCursor = locateIndex(rawEnd);

    for (size_t lineIndex = startCursor.lineIndex; lineIndex <= endCursor.lineIndex; ++lineIndex) {
        auto const& line = m_lines[lineIndex];
        float left = 0.f;
        float right = line.width;

        if (lineIndex == startCursor.lineIndex) {
            left = startCursor.x;
        }
        if (lineIndex == endCursor.lineIndex) {
            right = endCursor.x;
        }

        if (right - left < 1.f) continue;

        float top = m_textRect.getMaxY() - m_lineHeight * static_cast<float>(lineIndex);
        float bottom = std::max(top - m_lineHeight, m_textRect.origin.y);
        top = std::min(top - 1.f, m_textRect.getMaxY());
        bottom = std::max(bottom + 1.f, m_textRect.origin.y);

        CCPoint rect[4] = {
            ccp(m_textRect.origin.x + left, bottom),
            ccp(m_textRect.origin.x + right, bottom),
            ccp(m_textRect.origin.x + right, top),
            ccp(m_textRect.origin.x + left, top)
        };

        ccColor4F fillColor = {0.3f, 0.5f, 0.9f, 0.25f};
        ccColor4F borderColor = {0.4f, 0.6f, 1.0f, 0.45f};
        m_highlight->drawPolygon(rect, 4, fillColor, 1.0f, borderColor);
    }

    m_highlight->setVisible(true);
}

// ─── Text extraction ───

std::string CommentTextSelector::getSelectedText() const {
    if (m_fullText.empty()) return "";

    size_t startIdx = std::min(m_startIndex, m_endIndex);
    size_t endIdx = std::max(m_startIndex, m_endIndex);
    endIdx = std::min(endIdx, m_fullText.size());

    if (startIdx >= endIdx || startIdx >= m_fullText.size()) return "";
    return m_fullText.substr(startIdx, endIdx - startIdx);
}

// ─── Copy action ───

void CommentTextSelector::onCopy(CCObject*) {
    std::string selected = getSelectedText();
    if (!selected.empty()) {
        geode::utils::clipboard::write(selected);
    }

    dismissCopyButton();
}

void CommentTextSelector::dismissCopyButton() {
    if (m_copyMenu) {
        m_copyMenu->removeAllChildren();
        m_copyMenu->setVisible(false);
    }
    if (m_highlight) {
        m_highlight->clear();
        m_highlight->setVisible(false);
    }
}

// ─── Static attach helper ───

void CommentTextSelector::attach(
    CCNode* parent,
    std::string const& text,
    CCNode* textNode,
    std::string const& fontFile)
{
    if (!parent || text.empty()) return;

    if (auto* existing = typeinfo_cast<CommentTextSelector*>(parent->getChildByID("paimon-text-selector"_spr))) {
        existing->refresh(text, textNode, parent->getContentSize(), fontFile);
        existing->setZOrder(200);
        return;
    }

    auto selector = create(text, textNode, parent->getContentSize(), fontFile);
    if (selector) {
        parent->addChild(selector, 200);
    }
}
