#include "EmoteRenderer.hpp"
#include "services/EmoteService.hpp"
#include "services/EmoteCache.hpp"
#include "../../utils/AnimatedGIFSprite.hpp"
#include "../../core/RuntimeLifecycle.hpp"
#include "../comment-mentions/MentionLink.hpp"
#include <Geode/Geode.hpp>
#include <cctype>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::emotes;

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

// Parsing

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

// Characters that may form a @mention username.
static bool isMentionWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// Reads a mention starting at text[i] (which must be '@'). Returns the username
// length (excluding '@') in `len`, or 0 if it's not a valid clickable mention.
// '@everyone' is intentionally ignored (it has no profile to open).
static size_t matchMention(std::string const& text, size_t i) {
    if (text[i] != '@') return 0;
    if (i > 0 && isMentionWordChar(text[i - 1])) return 0; // must start a word
    size_t j = i + 1;
    while (j < text.size() && isMentionWordChar(text[j])) ++j;
    size_t len = j - i - 1;
    if (len == 0) return 0;
    std::string lower;
    lower.reserve(len);
    for (size_t k = i + 1; k < j; ++k) lower += (char)std::tolower((unsigned char)text[k]);
    if (lower == "everyone") return 0;
    return len;
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

bool EmoteRenderer::hasMentionSyntax(std::string const& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (matchMention(text, i) > 0) return true;
    }
    return false;
}

std::vector<CommentToken> EmoteRenderer::parseTokens(std::string const& rawText) {
    std::vector<CommentToken> tokens;
    auto& service = EmoteService::get();
    bool emotesAvailable = service.isLoaded();

    // Strip GD color codes so they don't interfere with parsing or rendering
    std::string text = stripGDColorCodes(rawText);

    size_t i = 0;
    std::string currentText;

    while (i < text.size()) {
        bool matched = false;

        // Try @username mention (works even without the emote catalog loaded)
        if (size_t mlen = matchMention(text, i); mlen > 0) {
            if (!currentText.empty()) {
                tokens.push_back(TextToken{currentText});
                currentText.clear();
            }
            tokens.push_back(MentionToken{text.substr(i + 1, mlen)});
            i += mlen + 1;
            matched = true;
        }

        // Try :emotename: syntax
        if (!matched && emotesAvailable && text[i] == ':') {
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
        if (!matched && emotesAvailable && text[i] == '<') {
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

// Rendering

CCNode* EmoteRenderer::renderComment(
    std::string const& rawText,
    float emoteSize,
    float maxWidth,
    const char* font,
    float fontSize,
    bool forceRender
) {
    // Size emotes from font metrics so they match the natural line height.
    auto refProbe = CCLabelBMFont::create("Ag", "chatFont.fnt");
    if (emoteSize <= 0.f) {
        float originalRefHeight = refProbe ? refProbe->getContentSize().height * fontSize : 20.f;
        emoteSize = originalRefHeight * 1.2f;
    }

    auto tokens = parseTokens(rawText);

    // If nothing special found and not forced, return nullptr (caller keeps original label)
    bool hasEmote = false;
    bool hasMention = false;
    for (auto& t : tokens) {
        if (std::holds_alternative<EmoteToken>(t)) hasEmote = true;
        else if (std::holds_alternative<MentionToken>(t)) hasMention = true;
    }
    if (!hasEmote && !hasMention && !forceRender) return nullptr;

    auto container = CCNode::create();
    container->setAnchorPoint({0.f, 1.f});

    // Use chatFont as the reference line height so all fonts occupy the same vertical space.
    float refHeight = refProbe ? refProbe->getContentSize().height * fontSize : 20.f;

    auto fontProbe = CCLabelBMFont::create("Ag", font);
    // Normalize custom font scale to chatFont's visual height.
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

    // Align custom-font baselines with chatFont by centering on the reference height.
    float baselineAdjust = (refHeight - fontHeight) / 2.f;

    float curX = 0.f;
    float curY = -lineHeight;
    float maxUsedX = 0.f;

    // Lazy menu holding clickable @mention buttons, sharing the container's coord space.
    CCMenu* mentionMenu = nullptr;
    auto ensureMentionMenu = [&]() -> CCMenu* {
        if (!mentionMenu) {
            mentionMenu = CCMenu::create();
            mentionMenu->ignoreAnchorPointForPosition(false);
            mentionMenu->setAnchorPoint({0.f, 0.f});
            mentionMenu->setPosition({0.f, 0.f});
            mentionMenu->setContentSize({0.f, 0.f});
            container->addChild(mentionMenu, 6);
        }
        return mentionMenu;
    };

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

                // Center text vertically using reference height, then apply baseline adjustment.
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
                std::string emoteKey = et->name;
                EmoteCache::get().loadEmote(*info, [phRef, emoteSize, emoteKey](CCTexture2D* tex, bool isGif, std::vector<uint8_t> const& gifData) {
                    // Retain the texture for the lifetime of the deferred task. The RAM cache
                    // may evict (and free) this texture before the queued task runs; capturing
                    // a raw pointer would leave `tex` dangling and crash inside
                    // CCSprite::initWithTexture when it dereferences the freed vtable.
                    geode::Ref<CCTexture2D> texRef = tex;
                    Loader::get()->queueInMainThread([phRef, texRef, isGif, gifData, emoteSize, emoteKey]() {
                        if (paimon::isRuntimeShuttingDown()) return;
                        if (auto ph = phRef.data(); !ph || !ph->getParent()) return;

                        auto attach = [phRef, emoteSize](CCNode* sprite) {
                            auto ph = phRef.data();
                            if (!ph || !ph->getParent() || !sprite) return;
                            float scale = emoteSize / std::max(sprite->getContentSize().width, sprite->getContentSize().height);
                            sprite->setScale(scale);
                            sprite->setAnchorPoint({0.5f, 0.5f});
                            sprite->setPosition({emoteSize / 2.f, emoteSize / 2.f});
                            ph->addChild(sprite);
                        };

                        if (isGif && !gifData.empty()) {
                            // Cached by emote name: shares GPU textures across uses and decodes off the main thread.
                            AnimatedGIFSprite::createAsync(gifData, emoteKey, [attach](AnimatedGIFSprite* spr) {
                                attach(spr);
                            });
                        } else if (auto* tex = texRef.data()) {
                            attach(CCSprite::createWithTexture(tex));
                        }
                    });
                });
            }

            curX += emoteSize + 2.f;
            maxUsedX = std::max(maxUsedX, curX);
        } else if (auto* mt = std::get_if<MentionToken>(&token)) {
            // Colored clickable label that opens the mentioned user's profile.
            std::string display = "@" + mt->username;
            auto label = CCLabelBMFont::create(display.c_str(), font);
            if (label) {
                label->setColor({90, 170, 255}); // link blue
                // Pre-scale the label, not the menu item: CCMenuItemSpriteExtra resets item scale on press.
                label->setScale(fontScale);

                float labelW = label->getContentSize().width * fontScale;
                float labelH = label->getContentSize().height * fontScale;

                if (curX + labelW > maxWidth && curX > 0.f) {
                    maxUsedX = std::max(maxUsedX, curX);
                    curX = 0.f;
                    curY -= lineHeight;
                }

                float textYOff = (lineHeight - labelH) / 2.f + baselineAdjust;

                std::string username = mt->username;
                auto* item = CCMenuItemExt::createSpriteExtra(
                    label, [username](CCMenuItemSpriteExtra*) {
                        paimon::mentions::openProfile(username);
                    });
                item->setAnchorPoint({0.f, 0.f});
                item->setPosition({curX, curY + textYOff});
                ensureMentionMenu()->addChild(item);

                curX += labelW;
                maxUsedX = std::max(maxUsedX, curX);
            }
        }
    }

    // curY is the bottom of the last line (negative); content spans y=0 down to curY.
    float totalH = -curY;  // positive height (curY is negative)

    // Shift children up so content fits in [0, totalH].
    for (auto* child : CCArrayExt<CCNode*>(container->getChildren())) {
        child->setPositionY(child->getPositionY() + totalH);
    }

    // Clamp container size to maxWidth to prevent overflow
    float clampedW = std::min(maxUsedX, maxWidth);
    container->setContentSize({clampedW, totalH});

    return container;
}
