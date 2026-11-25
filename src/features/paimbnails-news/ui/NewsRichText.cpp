#include "NewsRichText.hpp"

#include "../../../ui/PaimonUI.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_map>

using namespace geode::prelude;

namespace paimon::news {

namespace {

constexpr char const* kFont = "chatFont.fnt";
// chatFont is ~8 px tall at body scale; pad the hit box so links are tappable.
constexpr float kLinkPadX = 2.f;
constexpr float kLinkPadY = 3.f;
constexpr float kLinkPressScale = 1.08f;

// glyph advances of chatFont, unscaled. Rebuilt for every call so a texture
// pack that swaps the font after a reload is picked up.
class Measurer {
public:
    bool init() {
        m_probe = CCLabelBMFont::create("a", kFont);
        if (!m_probe) return false;
        m_lineHeight = m_probe->getContentSize().height;
        float const pair = measure("aa");
        m_space = std::max(1.f, measure("a a") - pair);
        return m_lineHeight > 0.f;
    }

    float advance(char32_t cp) {
        if (cp == U' ') return m_space;
        if (auto it = m_cache.find(cp); it != m_cache.end()) return it->second;
        std::string glyph;
        appendUtf8(glyph, cp);
        float const width = glyph.empty() ? 0.f : measure(glyph.c_str());
        m_cache.emplace(cp, width);
        return width;
    }

    float width(std::u32string_view text) {
        float total = 0.f;
        for (char32_t cp : text) total += advance(cp);
        return total;
    }

    float space() const { return m_space; }
    float lineHeight() const { return m_lineHeight; }

private:
    float measure(char const* text) {
        m_probe->setString(text);
        return m_probe->getContentSize().width;
    }

    Ref<CCLabelBMFont> m_probe;
    std::unordered_map<char32_t, float> m_cache;
    float m_space = 0.f;
    float m_lineHeight = 0.f;
};

struct Word {
    std::u32string text;
    std::string url;
    bool spaceBefore = false;
    bool lineBreak = false;
};

struct Run {
    std::u32string text;
    std::string url;
    bool spaceBefore = false;
};

struct Line {
    std::vector<Run> runs;
    float width = 0.f;
};

std::string toUtf8(std::u32string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t cp : text) appendUtf8(out, cp);
    return out;
}

std::vector<Word> tokenize(std::vector<TextSpan> const& spans) {
    std::vector<Word> words;
    bool pendingSpace = false;
    for (auto const& span : spans) {
        std::u32string current;
        auto flush = [&] {
            if (current.empty()) return;
            words.push_back({std::move(current), span.url, pendingSpace, false});
            current = {};
            pendingSpace = false;
        };
        std::string_view const text = span.text;
        for (std::size_t i = 0; i < text.size();) {
            char32_t const cp = nextCodePoint(text, i);
            if (cp == U' ') {
                flush();
                pendingSpace = true;
            } else if (cp == U'\n') {
                flush();
                words.push_back({{}, {}, false, true});
                pendingSpace = false;
            } else {
                current.push_back(cp);
            }
        }
        flush();
    }
    while (!words.empty() && words.front().lineBreak) words.erase(words.begin());
    while (!words.empty() && words.back().lineBreak) words.pop_back();
    return words;
}

bool startsWithScheme(std::u32string_view word) {
    auto matches = [&](std::u32string_view scheme) {
        if (word.size() <= scheme.size()) return false;
        for (std::size_t i = 0; i < scheme.size(); ++i) {
            char32_t c = word[i];
            if (c >= U'A' && c <= U'Z') c += U'a' - U'A';
            if (c != scheme[i]) return false;
        }
        return true;
    };
    return matches(U"https://") || matches(U"http://");
}

// same rule as Discord: closing punctuation after a URL is not part of it,
// unless it closes a bracket opened inside the URL (wiki-style links).
std::size_t urlEnd(std::u32string_view word) {
    std::size_t end = word.size();
    while (end > 0) {
        char32_t const c = word[end - 1];
        if (c == U')') {
            auto const head = word.substr(0, end - 1);
            if (std::count(head.begin(), head.end(), U'(') > std::count(head.begin(), head.end(), U')')) break;
        } else if (std::u32string_view(U".,:;!?\"'>]").find(c) == std::u32string_view::npos) {
            break;
        }
        --end;
    }
    return end;
}

void linkBareUrls(std::vector<Word>& words) {
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (words[i].lineBreak || !words[i].url.empty() || !startsWithScheme(words[i].text)) continue;
        std::size_t const end = urlEnd(words[i].text);
        auto url = toUtf8(std::u32string_view(words[i].text).substr(0, end));
        if (!isAllowedLinkUrl(url)) continue;
        if (end == words[i].text.size()) {
            words[i].url = std::move(url);
            continue;
        }
        Word tail{words[i].text.substr(end), {}, false, false};
        words[i].text.resize(end);
        words[i].url = std::move(url);
        words.insert(words.begin() + static_cast<std::ptrdiff_t>(i) + 1, std::move(tail));
        ++i;
    }
}

class Layout {
public:
    Layout(Measurer& measurer, float maxWidth) : m_measurer(measurer), m_maxWidth(maxWidth) {}

    void add(Word const& word) {
        if (word.lineBreak) {
            m_lines.emplace_back();
            return;
        }
        float const wordWidth = m_measurer.width(word.text);
        if (current().width + spaceFor(word.spaceBefore) + wordWidth <= m_maxWidth) {
            append(word.text, word.url, word.spaceBefore, wordWidth);
            return;
        }
        if (wordWidth <= m_maxWidth && !current().runs.empty()) {
            m_lines.emplace_back();
            append(word.text, word.url, false, wordWidth);
            return;
        }
        splitAcrossLines(word);
    }

    std::vector<Line> const& lines() const { return m_lines; }

private:
    Line& current() { return m_lines.back(); }

    float spaceFor(bool spaceBefore) {
        return spaceBefore && !current().runs.empty() ? m_measurer.space() : 0.f;
    }

    void append(std::u32string_view text, std::string const& url, bool spaceBefore, float width) {
        auto& line = current();
        bool const space = spaceBefore && !line.runs.empty();
        // neighbouring words with the same target share one label (or one button).
        if (!line.runs.empty() && line.runs.back().url == url) {
            auto& run = line.runs.back();
            if (space) run.text.push_back(U' ');
            run.text.append(text);
        } else {
            line.runs.push_back({std::u32string(text), url, space});
        }
        line.width += (space ? m_measurer.space() : 0.f) + width;
    }

    // long URLs: fill what is left of the line, then carry on below.
    void splitAcrossLines(Word const& word) {
        std::u32string_view rest = word.text;
        bool spaceBefore = word.spaceBefore;
        while (!rest.empty()) {
            float const available = m_maxWidth - current().width - spaceFor(spaceBefore);
            std::size_t count = 0;
            float used = 0.f;
            while (count < rest.size() && used + m_measurer.advance(rest[count]) <= available) {
                used += m_measurer.advance(rest[count]);
                ++count;
            }
            if (count == 0) {
                if (!current().runs.empty()) {
                    m_lines.emplace_back();
                    spaceBefore = false;
                    continue;
                }
                // a single glyph wider than the line still has to go somewhere.
                count = 1;
                used = m_measurer.advance(rest[0]);
            }
            append(rest.substr(0, count), word.url, spaceBefore, used);
            rest.remove_prefix(count);
            spaceBefore = false;
            if (!rest.empty()) m_lines.emplace_back();
        }
    }

    Measurer& m_measurer;
    float m_maxWidth;
    std::vector<Line> m_lines{1};
};

} // namespace

CCNode* createRichText(std::vector<TextSpan> const& spans, float width,
                       RichTextStyle const& style, LinkCallback const& onLink) {
    if (width <= 0.f || style.scale <= 0.f) return nullptr;
    auto words = tokenize(spans);
    if (words.empty()) return nullptr;
    linkBareUrls(words);

    Measurer measurer;
    if (!measurer.init()) return nullptr;
    Layout layout(measurer, width / style.scale);
    for (auto const& word : words) layout.add(word);
    auto const& lines = layout.lines();

    float const lineHeight = measurer.lineHeight() * style.scale;
    float const spaceWidth = measurer.space() * style.scale;
    float const height = lineHeight * static_cast<float>(lines.size());

    auto* node = CCNode::create();
    node->setAnchorPoint({0.f, 0.f});
    node->setContentSize({width, height});
    CCMenu* menu = nullptr;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        float const top = height - lineHeight * static_cast<float>(i);
        float x = 0.f;
        for (auto const& run : lines[i].runs) {
            if (run.spaceBefore) x += spaceWidth;
            auto const text = toUtf8(run.text);
            auto* label = CCLabelBMFont::create(text.c_str(), kFont);
            if (!label) continue;
            label->setScale(style.scale);
            float const runWidth = label->getScaledContentSize().width;

            if (run.url.empty()) {
                label->setColor(style.color);
                label->setAnchorPoint({0.f, 1.f});
                label->setPosition({x, top});
                node->addChild(label);
            } else {
                label->setColor(style.linkColor);
                if (!menu) {
                    menu = paimon::ui::makeZeroMenu();
                    node->addChild(menu, 1);
                }
                auto* item = CCMenuItemExt::createSpriteExtra(
                    label, [onLink, url = run.url](CCMenuItemSpriteExtra* sender) {
                        if (onLink) onLink(url, sender);
                    });
                CCSize const box{runWidth + kLinkPadX * 2.f, lineHeight + kLinkPadY * 2.f};
                item->setContentSize(box);
                label->setPosition({box.width / 2.f, box.height / 2.f});
                item->m_scaleMultiplier = kLinkPressScale;
                item->setPosition({x + runWidth / 2.f, top - lineHeight / 2.f});
                menu->addChild(item);
            }
            x += runWidth;
        }
    }
    return node;
}

} // namespace paimon::news
