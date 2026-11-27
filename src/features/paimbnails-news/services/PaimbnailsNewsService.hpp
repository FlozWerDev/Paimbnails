#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace paimon::news {

inline constexpr std::string_view kVanillaSourceId = "vanilla";
inline constexpr std::string_view kDefaultNewsChannelId = "1476641913582129353";

struct NewsProvider {
    std::string id;
    std::string channelId;
    std::string name;
    std::string channelName;
    std::string guildName;
    std::string iconUrl;
    int favoritesCount = 0;
    int rank = 0;
    bool favoritedByMe = false;
    bool isDefault = false;
};

// a piece of message text; `url` is set when the piece is a masked link.
struct TextSpan {
    std::string text;
    std::string url;
};

// a Discord link preview (X post, YouTube video, bot embed...).
struct NewsEmbed {
    uint32_t color = 0;
    std::string provider;
    std::string author;
    std::string authorUrl;
    std::string title;
    std::string url;
    std::vector<TextSpan> description;
    std::string imageUrl;
    int imageWidth = 0;
    int imageHeight = 0;
    // the image is a video's poster frame; the panel draws a play button on it.
    bool video = false;
    std::string thumbnailUrl;
    int thumbnailWidth = 0;
    int thumbnailHeight = 0;
    std::string footer;
    int64_t timestamp = 0;
};

// one message from the Paimbnails announcements channel, already flattened
// to plain text by the bot (GET /api/news on the forum server).
struct NewsItem {
    std::string id;
    std::string title;
    // message text. Bare URLs are plain text here; the panel turns them into links.
    std::vector<TextSpan> body;
    std::string author;
    std::string avatarUrl;
    std::string imageUrl;
    int imageWidth = 0;
    int imageHeight = 0;
    std::vector<NewsEmbed> embeds;
    int64_t timestamp = 0;
};

struct NewsFeed {
    std::vector<NewsItem> items;
    // bumped on every successful download so panels can skip identical rebuilds.
    int revision = 0;
};

class NewsService {
public:
    // ok=false only when nothing was ever downloaded; a failed refresh hands
    // back the previous feed instead.
    using Callback = std::function<void(bool ok, NewsFeed const& feed)>;
    using ProvidersCallback = std::function<void(bool ok, std::vector<NewsProvider> const& providers)>;
    using FavoriteCallback = std::function<void(bool ok, int favoritesCount, bool favoritedByMe, std::string const& error)>;

    static NewsService& get();

    // calls back on the main thread. Reuses the in-memory feed while it is
    // fresh unless `force` is set; concurrent callers share one request.
    void fetch(bool force, Callback callback);
    void fetch(std::string const& channelId, bool force, Callback callback);

    void fetchProviders(bool force, ProvidersCallback callback);
    std::vector<NewsProvider> const& providers();
    std::optional<NewsProvider> findProvider(std::string const& channelId);

    void fetchTopProviders(bool force, ProvidersCallback callback);
    std::vector<NewsProvider> const& topProviders();
    void toggleFavoriteProvider(std::string const& channelId, std::optional<bool> desired, FavoriteCallback callback);

    bool isSourceEnabled(std::string const& sourceId);
    bool setSourceEnabled(std::string const& sourceId, bool enabled);
    bool isVanillaEnabled();
    std::vector<NewsProvider> enabledProviders();

    void setPendingOpenChannel(std::string channelId);
    std::string consumePendingOpenChannel();

private:
    struct ChannelFeedState {
        std::optional<NewsFeed> feed;
        int64_t fetchedAtMs = 0;
        bool inFlight = false;
        std::vector<Callback> waiters;
    };

    NewsService() = default;
    void ensureStateLoaded();
    void saveEnabledSources();
    void saveProvidersCache();
    void applyFavoriteUpdate(std::string const& channelId, int favoritesCount, bool favoritedByMe);
    void finishChannel(std::string const& channelKey);
    void finishProviders(bool ok);
    void finishTopProviders(bool ok);

    std::unordered_map<std::string, ChannelFeedState> m_channels;
    bool m_stateLoaded = false;
    std::vector<NewsProvider> m_providers;
    std::vector<NewsProvider> m_topProviders;
    std::vector<std::string> m_enabledSources;
    std::string m_pendingOpenChannel;
    int64_t m_providersFetchedAtMs = 0;
    int64_t m_topProvidersFetchedAtMs = 0;
    bool m_providersInFlight = false;
    bool m_topProvidersInFlight = false;
    std::vector<ProvidersCallback> m_providerWaiters;
    std::vector<ProvidersCallback> m_topProviderWaiters;
};

// image hosts the bot hands out (Discord CDN and media proxy), https only.
bool isAllowedImageUrl(std::string_view url);

// links the panel may open: http(s) on a public host, at most 2 KB.
bool isAllowedLinkUrl(std::string_view url);

// text for one span: valid UTF-8, no controls or glyphs cocos cannot draw.
// unlike sanitizeDisplayText it keeps edge spaces and paragraph breaks, since
// spans are joined back together.
std::string cleanSpanText(std::string_view utf8, std::size_t maxBytes);

// decodes the code point at `i` of valid UTF-8 and advances `i`.
char32_t nextCodePoint(std::string_view utf8, std::size_t& i);
void appendUtf8(std::string& out, char32_t cp);

// chatFont covers Latin, Greek, Cyrillic and common punctuation, but cocos
// cannot draw characters outside the BMP (emoji) or the joiners around them.
std::string toChatFontText(std::string_view utf8, std::size_t maxBytes);

// goldFont/bigFont are ASCII only: fold accents (á -> a) and drop the rest.
std::string toAsciiFontText(std::string_view utf8, std::size_t maxBytes);

// "dd/mm/yyyy HH:MM" in local time; empty for 0.
std::string formatLocalDate(int64_t unixSeconds);

} // namespace paimon::news
