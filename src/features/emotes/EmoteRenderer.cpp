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

static std::string stripGDColorCodes(std::string const& text) {
    std::string result;
    result.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<' && i + 1 < text.size()) {
            if (text[i + 1] == 'c' && i + 3 < text.size() && text[i + 3] == '>') {
                i += 4;
                continue;
            }
            if (i + 3 < text.size() && text[i + 1] == '/' && text[i + 2] == 'c' && text[i + 3] == '>') {
                i += 4;
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

static bool isMentionWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// '@everyone' is intentionally ignored (it has no profile to open).
static size_t matchMention(std::string const& text, size_t i) {
    if (text[i] != '@') return 0;
    if (i > 0 && isMentionWordChar(text[i - 1])) return 0;
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

    std::string text = stripGDColorCodes(rawText);

    size_t i = 0;
    std::string currentText;

    while (i < text.size()) {
        bool matched = false;

        if (size_t mlen = matchMention(text, i); mlen > 0) {
            if (!currentText.empty()) {
                tokens.push_back(TextToken{currentText});
                currentText.clear();
            }
            tokens.push_back(MentionToken{text.substr(i + 1, mlen)});
            i += mlen + 1;
            matched = true;
        }

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

CCNode* EmoteRenderer::renderComment(
    std::string const& rawText,
    float emoteSize,
    float maxWidth,
    const char* font,
    float fontSize,
    bool forceRender
) {
    auto refProbe = CCLabelBMFont::create("Ag", "chatFont.fnt");
    if (emoteSize <= 0.f) {
        float originalRefHeight = refProbe ? refProbe->getContentSize().height * fontSize : 20.f;
        emoteSize = originalRefHeight * 1.2f;
    }

    auto tokens = parseTokens(rawText);

    bool hasEmote = false;
    bool hasMention = false;
    for (auto& t : tokens) {
        if (std::holds_alternative<EmoteToken>(t)) hasEmote = true;
        else if (std::holds_alternative<MentionToken>(t)) hasMention = true;
    }
    if (!hasEmote && !hasMention && !forceRender) return nullptr;

    auto container = CCNode::create();
    container->setAnchorPoint({0.f, 1.f});

    float refHeight = refProbe ? refProbe->getContentSize().height * fontSize : 20.f;

    auto fontProbe = CCLabelBMFont::create("Ag", font);
    float fontScale = fontSize;
    if (fontProbe && refProbe && std::string(font) != "chatFont.fnt") {
        float fontRawH = fontProbe->getContentSize().height;
        float refRawH = refProbe->getContentSize().height;
        if (fontRawH > 1.f && refRawH > 1.f) {
            fontScale = fontSize * (refRawH / fontRawH);
        }
    }
    float fontHeight = fontProbe ? fontProbe->getContentSize().height * fontScale : refHeight;

    constexpr float LINE_GAP = 3.f;
    float lineHeight = std::max(emoteSize, refHeight) + LINE_GAP;

    float baselineAdjust = (refHeight - fontHeight) / 2.f;

    float curX = 0.f;
    float curY = -lineHeight;
    float maxUsedX = 0.f;

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

                float labelH = label->getContentSize().height * fontScale;                float textYOff = (lineHeight - labelH) / 2.f + baselineAdjust;
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
            std::string display = "@" + mt->username;
            auto label = CCLabelBMFont::create(display.c_str(), font);
            if (label) {
                label->setColor({90, 170, 255});
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

    float totalH = -curY;

    for (auto* child : CCArrayExt<CCNode*>(container->getChildren())) {
        child->setPositionY(child->getPositionY() + totalH);
    }

    float clampedW = std::min(maxUsedX, maxWidth);
    container->setContentSize({clampedW, totalH});

    return container;
}
