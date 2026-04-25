#include "EmoteRenderer.hpp"
#include "services/EmoteService.hpp"
#include "services/EmoteCache.hpp"
#include "../../utils/AnimatedGIFSprite.hpp"
#include <Geode/Geode.hpp>
#include <cctype>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::emotes;

// ─── Helpers ───

// Strip GD color codes (<cX> and </c>) from text so BMFont labels
// don't render them as literal characters.
static std::string stripGDColorCodes(std::string const& text) {
    std::string result;
    result.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<' && i + 1 < text.size()) {
            // Check for <cX> pattern (color open)
            if (text[i + 1] == 'c' && i + 3 < text.size() && text[i + 3] == '>') {
                i += 4; // skip <cX>
                continue;
            }
            // Check for </c> pattern (color close)
            if (i + 3 < text.size() && text[i + 1] == '/' && text[i + 2] == 'c' && text[i + 3] == '>') {
                i += 4; // skip </c>
                continue;
            }
        }
        result += text[i];
        ++i;
    }
    return result;
}

static std::vector<std::string> splitTextChunks(std::string const& text) {
    std::vector<std::string> chunks;
    std::string current;

    enum class ChunkKind {
        None,
        Word,
        Space,
    };

    ChunkKind kind = ChunkKind::None;

    auto flushCurrent = [&]() {
        if (!current.empty()) {
            chunks.push_back(current);
            current.clear();
        }
    };

    for (char ch : text) {
        if (ch == '\n') {
            flushCurrent();
            chunks.emplace_back("\n");
            kind = ChunkKind::None;
            continue;
        }

        bool isSpace = std::isspace(static_cast<unsigned char>(ch)) != 0;
        auto nextKind = isSpace ? ChunkKind::Space : ChunkKind::Word;

        if (kind != ChunkKind::None && kind != nextKind) {
            flushCurrent();
        }

        current += ch;
        kind = nextKind;
    }

    flushCurrent();
    return chunks;
}

static bool isWhitespaceChunk(std::string const& chunk) {
    for (char ch : chunk) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

// ─── Parsing ───

static bool isValidEmoteName(std::string const& name) {
    if (name.size() < 2) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

static bool isGDColorCode(std::string const& inner) {
    if (inner.size() == 2 && inner[0] == 'c') return true;
    if (inner == "/c") return true;
    return false;
}

bool EmoteRenderer::hasEmoteSyntax(std::string const& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == ':') {
            auto end = text.find(':', i + 1);
            if (end != std::string::npos && end - i >= 3) {
                return true;
            }
        }
        if (c == '<') {
            auto end = text.find('>', i + 1);
            if (end != std::string::npos && end > i + 1) {
                auto inner = text.substr(i + 1, end - i - 1);
                if (!isGDColorCode(inner) && inner.size() >= 2) return true;
            }
        }
    }
    return false;
}

std::vector<CommentToken> EmoteRenderer::parseTokens(std::string const& rawText) {
    std::vector<CommentToken> tokens;
    auto& service = EmoteService::get();

    // Strip GD color codes so they don't interfere with parsing or rendering
    std::string text = stripGDColorCodes(rawText);

    if (!service.isLoaded()) {
        tokens.push_back(TextToken{text});
        return tokens;
    }

    size_t i = 0;
    std::string currentText;

    while (i < text.size()) {
        bool matched = false;

        // Try :emotename: syntax
        if (text[i] == ':') {
            auto end = text.find(':', i + 1);
            if (end != std::string::npos && end > i + 1) {
                auto name = text.substr(i + 1, end - i - 1);
                if (isValidEmoteName(name) && service.getEmoteByName(name).has_value()) {
                    if (!currentText.empty()) {
                        tokens.push_back(TextToken{currentText});
                        currentText.clear();
                    }
                    tokens.push_back(EmoteToken{name});
                    i = end + 1;
                    matched = true;
                }
            }
        }

        // Try <emotename> syntax
        if (!matched && text[i] == '<') {
            auto end = text.find('>', i + 1);
            if (end != std::string::npos && end > i + 1) {
                auto name = text.substr(i + 1, end - i - 1);
                if (!isGDColorCode(name) && isValidEmoteName(name) && service.getEmoteByName(name).has_value()) {
                    if (!currentText.empty()) {
                        tokens.push_back(TextToken{currentText});
                        currentText.clear();
                    }
                    tokens.push_back(EmoteToken{name});
                    i = end + 1;
                    matched = true;
                }
            }
        }

        if (!matched) {
            currentText += text[i];
            ++i;
        }
    }

    if (!currentText.empty()) {
        tokens.push_back(TextToken{currentText});
    }

    return tokens;
}

// ─── Rendering ───

CCNode* EmoteRenderer::renderComment(
    std::string const& rawText,
    float emoteSize,
    float maxWidth,
    const char* font,
    float fontSize,
    bool forceRender
) {
    // Compute emote size from font metrics so emotes match the natural
    // line height, similar to how Comment-Emojis-Reloaded sizes them.
    auto refProbe = CCLabelBMFont::create("Ag", "chatFont.fnt");
    if (emoteSize <= 0.f) {
        float originalRefHeight = refProbe ? refProbe->getContentSize().height * fontSize : 20.f;
        emoteSize = originalRefHeight * 1.2f;
    }

    auto tokens = parseTokens(rawText);

    // If no emotes found and not forced, return nullptr (caller keeps original label)
    bool hasEmote = false;
    for (auto& t : tokens) {
        if (std::holds_alternative<EmoteToken>(t)) {
            hasEmote = true;
            break;
        }
    }
    if (!hasEmote && !forceRender) return nullptr;

    auto container = CCNode::create();
    container->setAnchorPoint({0.f, 1.f});

    // ── Baseline-normalized line height ──
    // Always use chatFont as the reference so all fonts occupy the same
    // vertical space regardless of their individual metrics.
    float refHeight = refProbe ? refProbe->getContentSize().height * fontSize : 20.f;

    auto fontProbe = CCLabelBMFont::create("Ag", font);
    // Normalize custom font scale so all fonts render at the same visual
    // height as chatFont — prevents bigFont/gjFontXX from being oversized.
    float fontScale = fontSize;
    if (fontProbe && refProbe && std::string(font) != "chatFont.fnt") {
        float fontRawH = fontProbe->getContentSize().height;
        float refRawH = refProbe->getContentSize().height;
        if (fontRawH > 1.f && refRawH > 1.f) {
            fontScale = fontSize * (refRawH / fontRawH);
        }
    }
    float fontHeight = fontProbe ? fontProbe->getContentSize().height * fontScale : refHeight;

    // Line height is based on the reference font, not the custom font
    constexpr float LINE_GAP = 3.f;
    float lineHeight = std::max(emoteSize, refHeight) + LINE_GAP;

    // Baseline offset: shift custom font labels so their baseline aligns with
    // where chatFont's baseline would be. We measure the bottom of a baseline
    // character "x" relative to the full "Ag" bounding box for each font.
    // Since CCLabelBMFont places glyphs from the top, fonts with taller
    // ascenders push the baseline lower. We compensate by computing the
    // vertical center difference between the reference and custom font.
    float baselineAdjust = (refHeight - fontHeight) / 2.f;

    float curX = 0.f;
    float curY = -lineHeight;
    float maxUsedX = 0.f;

    for (auto& token : tokens) {
        if (auto* tt = std::get_if<TextToken>(&token)) {
            for (auto const& chunk : splitTextChunks(tt->text)) {
                if (chunk == "\n") {
                    maxUsedX = std::max(maxUsedX, curX);
                    curX = 0.f;
                    curY -= lineHeight;
                    continue;
                }

                auto label = CCLabelBMFont::create(chunk.c_str(), font);
                if (!label) {
                    continue;
                }

                label->setScale(fontScale);
                label->setAnchorPoint({0.f, 0.f});

                float labelW = label->getContentSize().width * fontScale;

                if (curX + labelW > maxWidth && curX > 0.f) {
                    maxUsedX = std::max(maxUsedX, curX);
                    curX = 0.f;
                    curY -= lineHeight;

                    if (isWhitespaceChunk(chunk)) {
                        continue;
                    }
                }

                // Center text vertically within the line using reference height,
                // then apply baseline adjustment so all fonts sit at the same Y
                float labelH = label->getContentSize().height * fontScale;
                float textYOff = (lineHeight - labelH) / 2.f + baselineAdjust;
                label->setPosition({curX, curY + textYOff});
                container->addChild(label);
                curX += labelW;
                maxUsedX = std::max(maxUsedX, curX);
            }

        } else if (auto* et = std::get_if<EmoteToken>(&token)) {
            if (curX + emoteSize > maxWidth && curX > 0.f) {
                maxUsedX = std::max(maxUsedX, curX);
                curX = 0.f;
                curY -= lineHeight;
            }

            auto placeholder = CCNode::create();
            placeholder->setContentSize({emoteSize, emoteSize});
            placeholder->setAnchorPoint({0.f, 0.f});
            // Center emote vertically within the line
            float emoteYOff = (lineHeight - emoteSize) / 2.f;
            placeholder->setPosition({curX, curY + emoteYOff});
            container->addChild(placeholder, 5);

            auto info = EmoteService::get().getEmoteByName(et->name);
            if (info) {
                auto phRef = Ref(placeholder);
                EmoteCache::get().loadEmote(*info, [phRef, emoteSize](CCTexture2D* tex, bool isGif, std::vector<uint8_t> const& gifData) {
                    Loader::get()->queueInMainThread([phRef, tex, isGif, gifData, emoteSize]() {
                        auto ph = phRef.data();
                        if (!ph || !ph->getParent()) return;

                        CCNode* sprite = nullptr;
                        if (isGif && !gifData.empty()) {
                            auto gifSprite = AnimatedGIFSprite::create(gifData.data(), gifData.size());
                            if (gifSprite) sprite = gifSprite;
                        } else if (tex) {
                            auto spr = CCSprite::createWithTexture(tex);
                            if (spr) sprite = spr;
                        }

                        if (sprite) {
                            float scale = emoteSize / std::max(sprite->getContentSize().width, sprite->getContentSize().height);
                            sprite->setScale(scale);
                            sprite->setAnchorPoint({0.5f, 0.5f});
                            sprite->setPosition({emoteSize / 2.f, emoteSize / 2.f});
                            ph->addChild(sprite);
                        }
                    });
                });
            }

            curX += emoteSize + 2.f;
            maxUsedX = std::max(maxUsedX, curX);
        }
    }

    // curY ended at the bottom of the last line (negative value).
    // Content goes from y=0 (top of first line) down to curY (bottom of last line).
    // With anchor {0, 1}, the container's top-left is at its position.
    float totalH = -curY;  // positive height (curY is negative)

    // Shift all children up so that y=0 in content space maps to the top.
    // Children currently at negative y-values; shift by totalH so they fit in [0, totalH].
    for (auto* child : CCArrayExt<CCNode*>(container->getChildren())) {
        child->setPositionY(child->getPositionY() + totalH);
    }

    // Clamp container size to maxWidth to prevent overflow
    float clampedW = std::min(maxUsedX, maxWidth);
    container->setContentSize({clampedW, totalH});

    return container;
}
