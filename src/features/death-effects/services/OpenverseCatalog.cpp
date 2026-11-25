#include "OpenverseCatalog.hpp"
#include "DeathSfxFormat.hpp"

#include "../../../utils/HttpClient.hpp"
#include "../../../utils/WebHelper.hpp"

#include <matjson.hpp>

#include <algorithm>
#include <array>
#include <cctype>

using namespace geode::prelude;

namespace paimon::death_effects::openverse {

namespace {

constexpr char const* kApiBase = "https://api.openverse.org/v1/audio/";
constexpr std::size_t kMaxDownloadBytes = 16u * 1024 * 1024;

web::WebRequest makeRequest(int timeoutSeconds) {
    web::WebRequest req;
    req.userAgent("Paimbnails/1.0 (Geode mod; death SFX search)");
    req.timeout(asp::Duration::fromSecs(timeoutSeconds));
    req.followRedirects(true);
    return req;
}

std::string urlEncode(std::string const& value) {
    std::string out;
    out.reserve(value.size());
    static constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char ch : value) {
        bool unreserved = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 0xF]);
        }
    }
    return out;
}

bool isSupportedType(std::string const& fileType) {
    static constexpr std::array<std::string_view, 7> supported = {
        "mp3", "ogg", "wav", "flac", "oga", "m4a", "opus"
    };
    return std::find(supported.begin(), supported.end(), fileType) != supported.end();
}

std::vector<ResultItem> parseResults(matjson::Value const& root) {
    std::vector<ResultItem> items;
    auto results = root["results"].asArray();
    if (!results.isOk()) return items;

    for (auto const& value : results.unwrap()) {
        if (!value.isObject()) continue;
        ResultItem item;
        item.title = value["title"].asString().unwrapOr("");
        item.creator = value["creator"].asString().unwrapOr("");
        item.creatorUrl = value["creator_url"].asString().unwrapOr("");
        item.license = value["license"].asString().unwrapOr("");
        item.licenseVersion = value["license_version"].asString().unwrapOr("");
        item.licenseUrl = value["license_url"].asString().unwrapOr("");
        item.downloadUrl = value["url"].asString().unwrapOr("");
        item.sourceUrl = value["foreign_landing_url"].asString().unwrapOr("");
        item.fileType = geode::utils::string::toLower(
            value["filetype"].asString().unwrapOr(""));
        item.attribution = value["attribution"].asString().unwrapOr("");
        {
            auto duration = value["duration"].asInt().unwrapOr(0);
            item.durationMs = duration > 0 ? static_cast<unsigned int>(duration) : 0;
        }

        // fail closed: without a license, a URL, or an FMOD-readable
        // format, the sound is never offered for download.
        if (item.title.empty() || item.license.empty() ||
            item.downloadUrl.empty() || item.sourceUrl.empty() ||
            !HttpClient::isUrlSafe(item.downloadUrl) ||
            !isSupportedType(item.fileType)) {
            continue;
        }
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace

std::string ResultItem::licenseLabel() const {
    return paimon::death_effects::licenseLabel(license, licenseVersion);
}

SoundCredit ResultItem::toCredit() const {
    SoundCredit credit;
    credit.title = title;
    credit.creator = creator;
    credit.creatorUrl = creatorUrl;
    credit.license = licenseLabel();
    credit.licenseUrl = licenseUrl;
    credit.sourceUrl = sourceUrl;
    if (!attribution.empty()) {
        credit.attribution = attribution;
        return credit;
    }
    credit.attribution = fmt::format(
        "\"{}\" by {}{}{} ({}) - {}",
        title,
        creator.empty() ? "unknown author" : creator,
        creatorUrl.empty() ? "" : " (",
        creatorUrl.empty() ? "" : creatorUrl + ")",
        licenseLabel(),
        sourceUrl
    );
    return credit;
}

void search(std::string const& query, SearchCallback callback) {
    auto url = fmt::format(
        "{}?q={}&page_size=20&license_type=all&filter_dead=false",
        kApiBase, urlEncode(query)
    );
    WebHelper::dispatch(makeRequest(20), "GET", url,
        [callback = std::move(callback)](web::WebResponse res) mutable {
            if (!callback) return;
            if (!res.ok()) {
                callback(false, {}, fmt::format(
                    "Openverse answered {} - check your connection.", res.code()));
                return;
            }
            auto parsed = matjson::parse(res.string().unwrapOr(""));
            if (!parsed.isOk()) {
                callback(false, {}, "Openverse returned an unreadable response.");
                return;
            }
            callback(true, parseResults(parsed.unwrap()), "");
        });
}

void download(ResultItem const& item, DownloadCallback callback) {
    WebHelper::dispatch(makeRequest(60), "GET", item.downloadUrl,
        [callback = std::move(callback)](web::WebResponse res) mutable {
            if (!callback) return;
            if (!res.ok()) {
                callback(false, {}, fmt::format("Download failed ({}).", res.code()));
                return;
            }
            auto data = res.data();
            if (data.empty()) {
                callback(false, {}, "The download was empty.");
                return;
            }
            if (data.size() > kMaxDownloadBytes) {
                callback(false, {}, "That sound is larger than 16 MB.");
                return;
            }
            callback(true, std::vector<std::uint8_t>(data.begin(), data.end()), "");
        });
}

} // namespace paimon::death_effects::openverse
