#include "PaimbnailsNewsService.hpp"

#include "../../../core/ModAuthFlow.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/AccountVerifier.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/HttpUrl.hpp"
#include "../../../utils/SafeText.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>

using namespace geode::prelude;

namespace paimon::news {

namespace {

constexpr int64_t kFreshMs = 20 * 1000;
constexpr int64_t kProvidersFreshMs = 5 * 60 * 1000;
constexpr char const* kSavedSourcesKey = "news.enabled_sources";
constexpr char const* kSavedProvidersKey = "news.providers_cache";
constexpr std::size_t kMaxItems = 20;
constexpr std::size_t kMaxTitleBytes = 400;
constexpr std::size_t kMaxTextBytes = 8000;
constexpr std::size_t kMaxNameBytes = 120;
constexpr std::size_t kMaxUrlBytes = 2048;
constexpr std::size_t kMaxSpans = 200;
constexpr std::size_t kMaxEmbeds = 3;
constexpr std::size_t kMaxEmbedLineBytes = 400;
constexpr std::size_t kMaxEmbedTextBytes = 3000;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

std::string jsonStr(matjson::Value const& v) {
    if (v.isString()) return v.asString().unwrapOr("");
    return {};
}

int64_t jsonInt(matjson::Value const& v) {
    if (v.isNumber()) return static_cast<int64_t>(v.asDouble().unwrapOr(0.0));
    return 0;
}

int jsonDimension(matjson::Value const& v) {
    return static_cast<int>(std::clamp<int64_t>(jsonInt(v), 0, 8192));
}

std::string safeUrl(matjson::Value const& v) {
    auto url = jsonStr(v);
    if (url.size() > kMaxUrlBytes || !isAllowedImageUrl(url)) return {};
    // Discord's media proxy returns WebP by default on resized images unless format=png/jpeg is explicit.
    if (auto parsed = paimon::net::parseHttpUrl(url);
        parsed && (parsed->host == "media.discordapp.net" || parsed->host.ends_with(".discordapp.net"))) {
        for (auto pos = url.find("&&"); pos != std::string::npos; pos = url.find("&&", pos)) {
            url.replace(pos, 2, "&");
        }
        auto qPos = url.find('?');
        if (qPos != std::string::npos && url.find("format=", qPos) == std::string::npos) {
            if (url.size() + 11 <= kMaxUrlBytes) {
                if (url.back() != '?' && url.back() != '&') url += '&';
                url += "format=png";
            }
        }
    }
    return url;
}

std::string safeLink(matjson::Value const& v) {
    auto url = jsonStr(v);
    return isAllowedLinkUrl(url) ? url : std::string{};
}

// `spans` when the bot sent them (masked links), otherwise the plain text.
std::vector<TextSpan> parseSpans(matjson::Value const& spans, matjson::Value const& fallback,
                                 std::size_t maxBytes) {
    std::vector<TextSpan> out;
    std::size_t used = 0;
    auto push = [&](std::string_view raw, std::string url) {
        if (used >= maxBytes) return;
        auto text = cleanSpanText(raw, maxBytes - used);
        if (text.empty()) return;
        used += text.size();
        out.push_back({std::move(text), std::move(url)});
    };
    if (spans.isArray()) {
        for (auto const& span : spans) {
            if (!span.isObject()) continue;
            push(jsonStr(span["text"]), safeLink(span["url"]));
            if (out.size() >= kMaxSpans) break;
        }
    }
    if (out.empty()) push(jsonStr(fallback), {});
    return out;
}

std::optional<NewsEmbed> parseEmbed(matjson::Value const& v) {
    if (!v.isObject()) return std::nullopt;
    auto line = [&](char const* key) {
        return paimon::text::sanitizeDisplayText(jsonStr(v[key]), kMaxEmbedLineBytes);
    };

    NewsEmbed embed;
    embed.color = static_cast<uint32_t>(std::clamp<int64_t>(jsonInt(v["color"]), 0, 0xFFFFFF));
    embed.provider = line("provider");
    embed.author = line("author");
    embed.authorUrl = safeLink(v["authorUrl"]);
    embed.title = line("title");
    embed.url = safeLink(v["url"]);
    embed.description = parseSpans(v["descriptionSpans"], v["description"], kMaxEmbedTextBytes);
    embed.imageUrl = safeUrl(v["image"]);
    embed.imageWidth = jsonDimension(v["imageWidth"]);
    embed.imageHeight = jsonDimension(v["imageHeight"]);
    embed.video = !embed.imageUrl.empty() && v["video"].isBool() && v["video"].asBool().unwrapOr(false);
    embed.thumbnailUrl = safeUrl(v["thumbnail"]);
    embed.thumbnailWidth = jsonDimension(v["thumbnailWidth"]);
    embed.thumbnailHeight = jsonDimension(v["thumbnailHeight"]);
    embed.footer = line("footer");
    embed.timestamp = jsonInt(v["timestamp"]);

    bool const empty = embed.provider.empty() && embed.author.empty() && embed.title.empty()
        && embed.description.empty() && embed.imageUrl.empty() && embed.thumbnailUrl.empty()
        && embed.footer.empty();
    if (empty) return std::nullopt;
    return embed;
}

std::optional<NewsFeed> parseFeed(std::string const& body) {
    auto parsed = matjson::parse(body);
    if (!parsed) return std::nullopt;
    matjson::Value root = std::move(parsed).unwrap();
    auto const& items = root["items"];
    if (!items.isArray()) return std::nullopt;

    NewsFeed feed;
    for (auto const& v : items) {
        if (!v.isObject()) continue;
        NewsItem item;
        item.id = paimon::text::sanitizeDisplayText(jsonStr(v["id"]), 32);
        item.title = paimon::text::sanitizeDisplayText(jsonStr(v["title"]), kMaxTitleBytes);
        item.body = parseSpans(v["spans"], v["text"], kMaxTextBytes);
        item.author = paimon::text::sanitizeDisplayText(jsonStr(v["author"]), kMaxNameBytes);
        item.avatarUrl = safeUrl(v["avatar"]);
        item.imageUrl = safeUrl(v["image"]);
        item.imageWidth = jsonDimension(v["imageWidth"]);
        item.imageHeight = jsonDimension(v["imageHeight"]);
        if (auto const& embeds = v["embeds"]; embeds.isArray()) {
            for (auto const& e : embeds) {
                if (auto embed = parseEmbed(e)) item.embeds.push_back(std::move(*embed));
                if (item.embeds.size() >= kMaxEmbeds) break;
            }
        }
        item.timestamp = jsonInt(v["timestamp"]);
        if (item.title.empty() && item.body.empty() && item.imageUrl.empty() && item.embeds.empty()) continue;
        feed.items.push_back(std::move(item));
        if (feed.items.size() >= kMaxItems) break;
    }
    return feed;
}

void appendCollapsingSpaces(std::string& out, std::string_view piece) {
    for (char ch : piece) {
        if (ch == ' ' && (out.empty() || out.back() == ' ' || out.back() == '\n')) continue;
        out.push_back(ch);
    }
}

std::string trimmed(std::string s) {
    auto const first = s.find_first_not_of(" \n");
    if (first == std::string::npos) return {};
    auto const last = s.find_last_not_of(" \n");
    return s.substr(first, last - first + 1);
}

// U+00C0..U+00FF folded to ASCII.
constexpr std::array<char const*, 64> kLatin1Fold = {
    "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
    "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y",
};

char const* foldToAscii(char32_t cp) {
    if (cp >= 0xC0 && cp <= 0xFF) return kLatin1Fold[cp - 0xC0];
    switch (cp) {
        case 0x00A0: return " ";
        case 0x0152: return "OE";
        case 0x0153: return "oe";
        case 0x0160: return "S";
        case 0x0161: return "s";
        case 0x0178: return "Y";
        case 0x017D: return "Z";
        case 0x017E: return "z";
        case 0x2013: case 0x2014: case 0x2022: return "-";
        case 0x2018: case 0x2019: return "'";
        case 0x201C: case 0x201D: return "\"";
        case 0x2026: return "...";
        default: return "";
    }
}

bool isInvisibleOrUnsupported(char32_t cp) {
    if (cp >= 0x10000) return true;                  // emoji and other astral glyphs
    if (cp >= 0xE000 && cp <= 0xF8FF) return true;   // private use
    switch (cp) {
        case 0x200B: case 0x200C: case 0x200D: case 0x2060:
        case 0xFE0E: case 0xFE0F: case 0x20E3: case 0xFEFF:
            return true;
        default:
            return false;
    }
}

bool isValidChannelId(std::string_view channelId) {
    if (channelId.size() < 5 || channelId.size() > 25) return false;
    return std::all_of(channelId.begin(), channelId.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

std::vector<NewsProvider> builtinProviders() {
    return {
        NewsProvider{
            .id = "paimbnails",
            .channelId = std::string(kDefaultNewsChannelId),
            .name = "Paimbnails",
            .channelName = {},
            .guildName = "Paimbnails",
            .iconUrl = {},
            .favoritesCount = 0,
            .rank = 1,
            .favoritedByMe = false,
            .isDefault = true,
        },
    };
}

std::string normalizeChannelKey(std::string_view channelId) {
    if (channelId.empty()) return std::string(kDefaultNewsChannelId);
    return std::string(channelId);
}

std::optional<std::vector<NewsProvider>> parseProvidersJson(std::string const& body) {
    auto parsed = matjson::parse(body);
    if (!parsed) return std::nullopt;
    matjson::Value root = std::move(parsed).unwrap();
    auto const& arr = root["providers"];
    if (!arr.isArray()) return std::nullopt;

    std::vector<NewsProvider> out;
    for (auto const& v : arr) {
        if (!v.isObject()) continue;
        auto cid = paimon::text::sanitizeDisplayText(jsonStr(v["channelId"]), 32);
        if (!isValidChannelId(cid)) continue;
        NewsProvider p;
        p.channelId = std::move(cid);
        p.id = paimon::text::sanitizeDisplayText(jsonStr(v["id"]), 64);
        if (p.id.empty()) p.id = p.channelId;
        p.name = paimon::text::sanitizeDisplayText(jsonStr(v["name"]), kMaxNameBytes);
        if (p.name.empty()) p.name = p.channelId;
        p.channelName = paimon::text::sanitizeDisplayText(jsonStr(v["channelName"]), kMaxNameBytes);
        p.guildName = paimon::text::sanitizeDisplayText(jsonStr(v["guildName"]), kMaxNameBytes);
        p.iconUrl = safeUrl(v["iconUrl"]);
        p.favoritesCount = static_cast<int>(std::max<int64_t>(0, jsonInt(v["favoritesCount"])));
        p.rank = static_cast<int>(std::max<int64_t>(0, jsonInt(v["rank"])));
        p.favoritedByMe = v["favoritedByMe"].isBool() && v["favoritedByMe"].asBool().unwrapOr(false);
        p.isDefault = v["default"].isBool() && v["default"].asBool().unwrapOr(false);
        out.push_back(std::move(p));
    }
    if (out.empty()) return std::nullopt;
    return out;
}

std::vector<NewsProvider> sortAsTop(std::vector<NewsProvider> items) {
    std::stable_sort(items.begin(), items.end(), [](NewsProvider const& a, NewsProvider const& b) {
        if (a.favoritesCount != b.favoritesCount) return a.favoritesCount > b.favoritesCount;
        if (a.isDefault != b.isDefault) return a.isDefault;
        return false;
    });
    for (size_t i = 0; i < items.size(); ++i) {
        items[i].rank = static_cast<int>(i + 1);
    }
    return items;
}

std::vector<NewsProvider> mergeWithBuiltins(std::vector<NewsProvider> incoming) {
    auto defaults = builtinProviders();
    for (auto const& def : defaults) {
        bool const exists = std::any_of(incoming.begin(), incoming.end(), [&](NewsProvider const& p) {
            return p.channelId == def.channelId;
        });
        if (!exists) incoming.push_back(def);
    }
    return incoming;
}

} // namespace

NewsService& NewsService::get() {
    static NewsService instance;
    return instance;
}

void NewsService::ensureStateLoaded() {
    if (m_stateLoaded) return;
    m_stateLoaded = true;

    m_providers = builtinProviders();
    if (auto* mod = Mod::get()) {
        auto const cachedProviders = mod->getSavedValue<std::string>(kSavedProvidersKey, "");
        if (!cachedProviders.empty()) {
            if (auto parsed = parseProvidersJson(cachedProviders)) {
                m_providers = mergeWithBuiltins(std::move(*parsed));
            }
        }
        m_topProviders = sortAsTop(m_providers);

        auto const savedSources = mod->getSavedValue<std::string>(kSavedSourcesKey, "");
        if (!savedSources.empty()) {
            if (auto parsed = matjson::parse(savedSources); parsed && parsed.unwrap().isArray()) {
                for (auto const& entry : parsed.unwrap()) {
                    if (!entry.isString()) continue;
                    auto id = entry.asString().unwrapOr("");
                    if (id.empty()) continue;
                    if (std::find(m_enabledSources.begin(), m_enabledSources.end(), id) == m_enabledSources.end()) {
                        m_enabledSources.push_back(std::move(id));
                    }
                }
            }
        }
    }

    if (m_enabledSources.empty()) {
        m_enabledSources = {
            std::string(kVanillaSourceId),
            std::string(kDefaultNewsChannelId),
        };
    }
}

void NewsService::saveEnabledSources() {
    auto* mod = Mod::get();
    if (!mod) return;
    matjson::Value arr = matjson::Value::array();
    for (auto const& id : m_enabledSources) {
        arr.push(id);
    }
    mod->setSavedValue<std::string>(kSavedSourcesKey, arr.dump(matjson::NO_INDENTATION));
}

void NewsService::saveProvidersCache() {
    auto* mod = Mod::get();
    if (!mod) return;
    matjson::Value arr = matjson::Value::array();
    for (auto const& p : m_providers) {
        matjson::Value obj = matjson::Value::object();
        obj["id"] = p.id;
        obj["channelId"] = p.channelId;
        obj["name"] = p.name;
        if (!p.channelName.empty()) obj["channelName"] = p.channelName;
        if (!p.guildName.empty()) obj["guildName"] = p.guildName;
        if (!p.iconUrl.empty()) obj["iconUrl"] = p.iconUrl;
        obj["favoritesCount"] = p.favoritesCount;
        obj["favoritedByMe"] = p.favoritedByMe;
        obj["default"] = p.isDefault;
        arr.push(std::move(obj));
    }
    matjson::Value root = matjson::Value::object();
    root["providers"] = std::move(arr);
    mod->setSavedValue<std::string>(kSavedProvidersKey, root.dump(matjson::NO_INDENTATION));
}

std::vector<NewsProvider> const& NewsService::providers() {
    ensureStateLoaded();
    return m_providers;
}

std::optional<NewsProvider> NewsService::findProvider(std::string const& channelId) {
    ensureStateLoaded();
    for (auto const& p : m_providers) {
        if (p.channelId == channelId) return p;
    }
    return std::nullopt;
}

bool NewsService::isSourceEnabled(std::string const& sourceId) {
    ensureStateLoaded();
    return std::find(m_enabledSources.begin(), m_enabledSources.end(), sourceId) != m_enabledSources.end();
}

bool NewsService::isVanillaEnabled() {
    return isSourceEnabled(std::string(kVanillaSourceId));
}

std::vector<NewsProvider> NewsService::enabledProviders() {
    ensureStateLoaded();
    std::vector<NewsProvider> out;
    for (auto const& p : m_providers) {
        if (isSourceEnabled(p.channelId)) out.push_back(p);
    }
    return out;
}

bool NewsService::setSourceEnabled(std::string const& sourceId, bool enabled) {
    ensureStateLoaded();
    if (sourceId.empty()) return false;

    auto it = std::find(m_enabledSources.begin(), m_enabledSources.end(), sourceId);
    if (enabled) {
        if (it == m_enabledSources.end()) {
            m_enabledSources.push_back(sourceId);
            saveEnabledSources();
        }
        return true;
    }

    if (it == m_enabledSources.end()) return true;
    // keep at least one active tab in the News popup.
    if (m_enabledSources.size() <= 1) return false;
    m_enabledSources.erase(it);
    saveEnabledSources();
    return true;
}

void NewsService::fetch(bool force, Callback callback) {
    fetch(std::string(kDefaultNewsChannelId), force, std::move(callback));
}

void NewsService::fetch(std::string const& channelId, bool force, Callback callback) {
    auto const key = normalizeChannelKey(channelId);
    auto& state = m_channels[key];
    if (!force && state.feed && nowMs() - state.fetchedAtMs < kFreshMs) {
        if (callback) callback(true, *state.feed);
        return;
    }
    if (callback) state.waiters.push_back(std::move(callback));
    if (state.inFlight) return;

    auto const base = HttpClient::get().getForumServerURL();
    if (base.empty()) {
        geode::queueInMainThread([key] { NewsService::get().finishChannel(key); });
        return;
    }

    state.inFlight = true;
    auto const url = key == kDefaultNewsChannelId
        ? fmt::format("{}/api/news?limit={}", base, kMaxItems)
        : fmt::format("{}/api/news?limit={}&channelId={}", base, kMaxItems, key);
    web::WebRequest req;
    // Render can take a while to answer the first request after a deploy.
    req.timeout(asp::Duration::fromSecs(20));
    req.acceptEncoding("gzip, deflate");
    req.header("Accept", "application/json");
    if (force) req.header("Cache-Control", "no-cache");
    req.followRedirects(false);

    WebHelper::dispatch(std::move(req), "GET", url, [key](web::WebResponse res) {
        if (paimon::isRuntimeShuttingDown()) return;
        auto& self = NewsService::get();
        if (!res.ok()) {
            log::warn("[PaimbnailsNews] /api/news ({}) failed with HTTP {}", key, res.code());
            self.finishChannel(key);
            return;
        }
        auto feed = parseFeed(res.string().unwrapOr(""));
        if (!feed) {
            log::warn("[PaimbnailsNews] /api/news ({}) returned an unexpected payload", key);
            self.finishChannel(key);
            return;
        }
        auto& state = self.m_channels[key];
        feed->revision = (state.feed ? state.feed->revision : 0) + 1;
        state.feed = std::move(*feed);
        state.fetchedAtMs = nowMs();
        self.finishChannel(key);
    });
}

void NewsService::finishChannel(std::string const& channelKey) {
    auto it = m_channels.find(channelKey);
    if (it == m_channels.end()) return;
    auto& state = it->second;
    state.inFlight = false;
    auto waiters = std::move(state.waiters);
    state.waiters.clear();
    static NewsFeed const kEmpty;
    bool const hasFeed = state.feed.has_value();
    NewsFeed const& feedRef = hasFeed ? *state.feed : kEmpty;
    for (auto& cb : waiters) {
        if (cb) cb(hasFeed, feedRef);
    }
}

void NewsService::fetchProviders(bool force, ProvidersCallback callback) {
    ensureStateLoaded();
    if (!force && m_providersFetchedAtMs > 0 && nowMs() - m_providersFetchedAtMs < kProvidersFreshMs) {
        if (callback) callback(true, m_providers);
        return;
    }
    if (callback) m_providerWaiters.push_back(std::move(callback));
    if (m_providersInFlight) return;

    auto const base = HttpClient::get().getForumServerURL();
    if (base.empty()) {
        geode::queueInMainThread([] { NewsService::get().finishProviders(false); });
        return;
    }

    m_providersInFlight = true;
    auto const url = fmt::format("{}/api/news/providers", base);
    web::WebRequest req;
    req.timeout(asp::Duration::fromSecs(15));
    req.acceptEncoding("gzip, deflate");
    req.header("Accept", "application/json");
    if (auto const token = HttpClient::get().getSessionToken(); !token.empty()) {
        req.header("X-Session-Token", token);
    }
    if (force) req.header("Cache-Control", "no-cache");
    req.followRedirects(false);

    WebHelper::dispatch(std::move(req), "GET", url, [](web::WebResponse res) {
        if (paimon::isRuntimeShuttingDown()) return;
        auto& self = NewsService::get();
        if (!res.ok()) {
            log::warn("[PaimbnailsNews] /api/news/providers failed with HTTP {}", res.code());
            self.finishProviders(false);
            return;
        }
        auto parsed = parseProvidersJson(res.string().unwrapOr(""));
        if (!parsed) {
            log::warn("[PaimbnailsNews] /api/news/providers returned an unexpected payload");
            self.finishProviders(false);
            return;
        }
        self.m_providers = mergeWithBuiltins(std::move(*parsed));
        self.m_topProviders = sortAsTop(self.m_providers);
        self.m_providersFetchedAtMs = nowMs();
        self.saveProvidersCache();
        self.finishProviders(true);
    });
}

void NewsService::finishProviders(bool ok) {
    m_providersInFlight = false;
    auto waiters = std::move(m_providerWaiters);
    m_providerWaiters.clear();
    for (auto& cb : waiters) {
        if (cb) cb(ok, m_providers);
    }
}

std::vector<NewsProvider> const& NewsService::topProviders() {
    ensureStateLoaded();
    if (m_topProviders.empty()) m_topProviders = sortAsTop(m_providers);
    return m_topProviders;
}

void NewsService::fetchTopProviders(bool force, ProvidersCallback callback) {
    ensureStateLoaded();
    if (!force && m_topProvidersFetchedAtMs > 0 && nowMs() - m_topProvidersFetchedAtMs < 20 * 1000) {
        if (callback) callback(true, topProviders());
        return;
    }
    if (callback) m_topProviderWaiters.push_back(std::move(callback));
    if (m_topProvidersInFlight) return;

    auto const base = HttpClient::get().getForumServerURL();
    if (base.empty()) {
        geode::queueInMainThread([] { NewsService::get().finishTopProviders(false); });
        return;
    }

    m_topProvidersInFlight = true;
    auto const url = fmt::format("{}/api/news/top", base);
    web::WebRequest req;
    req.timeout(asp::Duration::fromSecs(15));
    req.acceptEncoding("gzip, deflate");
    req.header("Accept", "application/json");
    if (auto const token = HttpClient::get().getSessionToken(); !token.empty()) {
        req.header("X-Session-Token", token);
    }
    if (force) req.header("Cache-Control", "no-cache");
    req.followRedirects(false);

    WebHelper::dispatch(std::move(req), "GET", url, [](web::WebResponse res) {
        if (paimon::isRuntimeShuttingDown()) return;
        auto& self = NewsService::get();
        if (!res.ok()) {
            log::warn("[PaimbnailsNews] /api/news/top failed with HTTP {}", res.code());
            self.finishTopProviders(false);
            return;
        }
        auto parsed = parseProvidersJson(res.string().unwrapOr(""));
        if (!parsed) {
            log::warn("[PaimbnailsNews] /api/news/top returned an unexpected payload");
            self.finishTopProviders(false);
            return;
        }
        self.m_topProviders = sortAsTop(mergeWithBuiltins(std::move(*parsed)));
        for (auto const& topItem : self.m_topProviders) {
            for (auto& p : self.m_providers) {
                if (p.channelId == topItem.channelId) {
                    p.favoritesCount = topItem.favoritesCount;
                    p.favoritedByMe = topItem.favoritedByMe;
                }
            }
        }
        self.m_topProvidersFetchedAtMs = nowMs();
        self.saveProvidersCache();
        self.finishTopProviders(true);
    });
}

void NewsService::finishTopProviders(bool ok) {
    m_topProvidersInFlight = false;
    auto waiters = std::move(m_topProviderWaiters);
    m_topProviderWaiters.clear();
    auto const& list = topProviders();
    for (auto& cb : waiters) {
        if (cb) cb(ok, list);
    }
}

void NewsService::applyFavoriteUpdate(std::string const& channelId, int favoritesCount, bool favoritedByMe) {
    for (auto& p : m_providers) {
        if (p.channelId == channelId) {
            p.favoritesCount = favoritesCount;
            p.favoritedByMe = favoritedByMe;
        }
    }
    for (auto& p : m_topProviders) {
        if (p.channelId == channelId) {
            p.favoritesCount = favoritesCount;
            p.favoritedByMe = favoritedByMe;
        }
    }
    m_topProviders = sortAsTop(std::move(m_topProviders));
    saveProvidersCache();
}

void NewsService::toggleFavoriteProvider(
    std::string const& channelId,
    std::optional<bool> desired,
    FavoriteCallback callback
) {
    if (!isValidChannelId(channelId)) {
        if (callback) callback(false, 0, false, "Proveedor de noticias invalido.");
        return;
    }

    auto const base = HttpClient::get().getForumServerURL();
    if (base.empty()) {
        if (callback) callback(false, 0, false, "Servidor no disponible.");
        return;
    }

    auto const acc = AccountVerifier::get().verify();
    if (!acc.loggedIn()) {
        if (callback) callback(false, 0, false, "Necesitas una cuenta de Geometry Dash iniciada para votar.");
        return;
    }

    std::string reason;
    if (!paimon::modauth::hasVerifiedVoteEligibility(&reason)) {
        auto const token = HttpClient::get().getSessionToken();
        if (token.empty() || !paimon::auth::UnifiedAuthFlow::validSessionToken(token, acc.accountID)) {
            if (callback) callback(false, 0, false, reason);
            return;
        }
        // Session token is stored locally, refresh once against /api/auth/session before voting.
        paimon::modauth::refreshSession([this, channelId, desired, callback = std::move(callback)](
                paimon::modauth::SessionRoles const& roles) mutable {
            if (!roles.authenticated || !paimon::modauth::isAuthenticated()) {
                if (callback) {
                    callback(false, 0, false, "Verifica tu cuenta en Paimbnails (Mi Cuenta) para votar.");
                }
                return;
            }
            this->toggleFavoriteProvider(channelId, desired, std::move(callback));
        });
        return;
    }

    matjson::Value body = matjson::makeObject({
        {"channelId", channelId},
        {"accountID", acc.accountID},
        {"username", acc.username},
    });
    if (desired.has_value()) {
        body["favorite"] = *desired;
    }

    auto const url = fmt::format("{}/api/news/providers/{}/favorite", base, channelId);
    HttpClient::get().postWithAuth(url, body.dump(), [channelId, callback = std::move(callback)](
            bool ok, std::string const& resp) {
        if (paimon::isRuntimeShuttingDown()) return;
        geode::queueInMainThread([channelId, callback = std::move(callback), ok, resp]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto jsonStart = resp.find('{');
            std::optional<matjson::Value> parsed;
            if (jsonStart != std::string::npos) {
                auto res = matjson::parse(resp.substr(jsonStart));
                if (res.isOk() && res.unwrap().isObject()) parsed = res.unwrap();
            }
            if (!ok || !parsed || !(*parsed)["success"].asBool().unwrapOr(false)) {
                std::string err = parsed ? jsonStr((*parsed)["error"]) : std::string();
                if (err.empty()) err = "No se pudo registrar tu voto. Verifica tu sesion en Paimbnails.";
                if (callback) callback(false, 0, false, err);
                return;
            }
            int const count = static_cast<int>(std::max<int64_t>(0, jsonInt((*parsed)["favoritesCount"])));
            bool const fav = (*parsed)["favoritedByMe"].isBool()
                && (*parsed)["favoritedByMe"].asBool().unwrapOr(false);
            NewsService::get().applyFavoriteUpdate(channelId, count, fav);
            if (callback) callback(true, count, fav, {});
        });
    });
}

void NewsService::setPendingOpenChannel(std::string channelId) {
    m_pendingOpenChannel = std::move(channelId);
}

std::string NewsService::consumePendingOpenChannel() {
    auto out = std::move(m_pendingOpenChannel);
    m_pendingOpenChannel.clear();
    return out;
}

bool isAllowedImageUrl(std::string_view url) {
    auto parsed = paimon::net::parseHttpUrl(url);
    if (!parsed || parsed->scheme != "https" || parsed->port != 443) return false;
    auto const& host = parsed->host;
    return host == "cdn.discordapp.com" || host.ends_with(".discordapp.net");
}

bool isAllowedLinkUrl(std::string_view url) {
    return !url.empty() && url.size() <= kMaxUrlBytes && paimon::net::isPublicHttpUrl(url);
}

char32_t nextCodePoint(std::string_view s, std::size_t& i) {
    auto const c = static_cast<unsigned char>(s[i]);
    std::size_t const len = paimon::text::utf8SequenceLength(c);
    if (len == 0 || i + len > s.size()) {
        ++i;
        return U'?';
    }
    char32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    for (std::size_t k = 1; k < len; ++k) {
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    }
    i += len;
    return cp;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string cleanSpanText(std::string_view in, std::size_t maxBytes) {
    std::string out;
    out.reserve(std::min(in.size(), maxBytes));
    int newlines = 0;
    for (std::size_t i = 0; i < in.size();) {
        auto const lead = static_cast<unsigned char>(in[i]);
        std::size_t const len = paimon::text::utf8SequenceLength(lead);
        bool valid = len != 0 && i + len <= in.size();
        for (std::size_t k = 1; valid && k < len; ++k) {
            valid = paimon::text::isContinuationByte(static_cast<unsigned char>(in[i + k]));
        }
        if (!valid) {
            ++i;
            continue;
        }

        char32_t cp = nextCodePoint(in, i);
        if (cp == U'\t') cp = U' ';
        if (cp == U'\n') {
            // at most one blank line between paragraphs, as on Discord.
            if (++newlines > 2) continue;
        } else if (cp != U' ') {
            newlines = 0;
        }
        if ((cp < 0x20 && cp != U'\n') || (cp >= 0x7F && cp < 0xA0)) continue;
        if (isInvisibleOrUnsupported(cp)) continue;

        std::string piece;
        appendUtf8(piece, cp);
        if (out.size() + piece.size() > maxBytes) break;
        out += piece;
    }
    return out;
}

std::string toChatFontText(std::string_view utf8, std::size_t maxBytes) {
    // sanitizeDisplayText collapses blank lines, so keep paragraph breaks by
    // cleaning each paragraph on its own.
    std::string clean;
    std::size_t start = 0;
    while (start <= utf8.size() && clean.size() < maxBytes) {
        auto const end = utf8.find("\n\n", start);
        auto const paragraph = utf8.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        auto const budget = maxBytes - clean.size();
        auto part = paimon::text::sanitizeDisplayText(paragraph, budget > 2 ? budget - 2 : budget);
        if (!part.empty()) {
            if (!clean.empty()) clean += "\n\n";
            clean += part;
        }
        if (end == std::string_view::npos) break;
        start = end + 2;
    }

    std::string out;
    out.reserve(clean.size());
    for (std::size_t i = 0; i < clean.size();) {
        char32_t const cp = nextCodePoint(clean, i);
        if (isInvisibleOrUnsupported(cp)) continue;
        std::string piece;
        appendUtf8(piece, cp);
        appendCollapsingSpaces(out, piece);
    }
    return trimmed(std::move(out));
}

std::string toAsciiFontText(std::string_view utf8, std::size_t maxBytes) {
    auto const clean = paimon::text::sanitizeDisplayText(utf8, maxBytes);
    std::string out;
    out.reserve(clean.size());
    for (std::size_t i = 0; i < clean.size();) {
        char32_t const cp = nextCodePoint(clean, i);
        if (cp == U'\n' || (cp >= 0x20 && cp < 0x7F)) {
            char const ch = static_cast<char>(cp);
            appendCollapsingSpaces(out, std::string_view(&ch, 1));
            continue;
        }
        appendCollapsingSpaces(out, foldToAscii(cp));
    }
    return trimmed(std::move(out));
}

std::string formatLocalDate(int64_t unixSeconds) {
    if (unixSeconds <= 0) return {};
    std::time_t const t = static_cast<std::time_t>(unixSeconds);
    std::tm tm{};
#ifdef GEODE_IS_WINDOWS
    if (localtime_s(&tm, &t) != 0) return {};
#else
    if (!localtime_r(&t, &tm)) return {};
#endif
    char buffer[32] = {};
    if (std::strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M", &tm) == 0) return {};
    return buffer;
}

} // namespace paimon::news
