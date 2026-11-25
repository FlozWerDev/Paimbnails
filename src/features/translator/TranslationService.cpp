#include "TranslationService.hpp"

#include "TranslatorConfig.hpp"
#include "TranslatorLanguages.hpp"
#include "TranslatorText.hpp"
#include "../../core/RuntimeLifecycle.hpp"
#include "../../utils/HttpClient.hpp"
#include "../../utils/WebHelper.hpp"
#include <Geode/utils/web.hpp>
#include <chrono>
#include <utility>

using namespace geode::prelude;

namespace paimon::translate {
namespace {

char const kSecondaryEndpoint[] = "https://clients5.google.com/translate_a/t?client=dict-chrome-ex";
// google rate-limits the public endpoints when the request looks like a script.
char const kUserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0.0.0 Safari/537.36";
constexpr std::size_t kMaxCacheEntries = 400;
constexpr std::size_t kMaxInputBytes = 1200;

std::string cacheKey(std::string const& target, std::string const& text) {
    return target + '|' + text;
}

std::string trimmed(std::string const& text) {
    auto const begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    auto const end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

enum class EndpointKind { Gtx, GoogleT, Lingva };

EndpointKind classify(std::string const& endpoint) {
    if (endpoint.find("translate_a/single") != std::string::npos) return EndpointKind::Gtx;
    if (endpoint.find("translate_a/t") != std::string::npos) return EndpointKind::GoogleT;
    return EndpointKind::Lingva;
}

std::string withQuery(std::string endpoint) {
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    return endpoint;
}

std::string buildUrl(std::string const& endpoint, std::string const& text,
    std::string const& target) {
    auto const encoded = HttpClient::encodeQueryParam(text);
    auto const kind = classify(endpoint);

    if (kind == EndpointKind::Lingva) {
        auto base = withQuery(endpoint);
        if (base.find("/api/v1") != std::string::npos &&
            base.rfind("/auto", base.size() - std::min<std::size_t>(5, base.size())) ==
                std::string::npos) {
            base += "/auto";
        }
        return base + "/" + target + "/" + encoded;
    }

    // both Google endpoints need client + sl/tl/q; gtx also needs dt=t or it
    // answers without any translated segment.
    auto url = withQuery(endpoint);
    bool const hasQuery = url.find('?') != std::string::npos;
    auto const separator = hasQuery ? '&' : '?';
    std::string extra;
    if (kind == EndpointKind::Gtx) {
        if (url.find("client=") == std::string::npos) extra += "client=gtx&";
        if (url.find("dt=") == std::string::npos) extra += "dt=t&";
    } else if (url.find("client=") == std::string::npos) {
        extra += "client=dict-chrome-ex&";
    }
    extra += "sl=auto&tl=" + target + "&q=" + encoded;
    return url + separator + extra;
}

// gtx: [[["Hola","hello",...],[...]], ...]
// translate_a/t: [["Hola","en"]] or ["Hola"]
bool parseGoogle(std::string const& body, std::string& out) {
    auto parsed = matjson::parse(body);
    if (!parsed.isOk()) return false;
    auto& root = parsed.unwrap();
    if (!root.isArray() || root.size() == 0) return false;

    if (root[0].isString()) {
        out = root[0].asString().unwrapOr("");
        return !out.empty();
    }
    if (!root[0].isArray()) return false;

    auto& segments = root[0];
    if (segments.size() == 0) return false;

    // translate_a/t wraps the text in a single pair: [["Hola","en"]]
    if (segments.size() == 2 && segments[0].isString() && segments[1].isString()) {
        auto candidate = segments[0].asString().unwrapOr("");
        if (!candidate.empty()) {
            out = std::move(candidate);
            return true;
        }
    }

    std::string text;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        auto& segment = segments[index];
        if (segment.isString()) {
            text += segment.asString().unwrapOr("");
            continue;
        }
        if (!segment.isArray() || segment.size() == 0) continue;
        text += segment[0].asString().unwrapOr("");
    }
    if (text.empty()) return false;
    out = std::move(text);
    return true;
}

bool parseLingva(std::string const& body, std::string& out) {
    auto parsed = matjson::parse(body);
    if (!parsed.isOk()) return false;
    auto& root = parsed.unwrap();
    if (!root.isObject()) return false;
    auto text = root["translation"].asString().unwrapOr("");
    if (text.empty()) return false;
    out = std::move(text);
    return true;
}

}

TranslationService& TranslationService::get() {
    static TranslationService instance;
    return instance;
}

void TranslationService::translate(
    std::string const& text, std::string const& target, Callback callback
) {
    auto clean = clampUtf8(trimmed(sanitizeForTranslation(text)), kMaxInputBytes);
    if (paimon::isRuntimeShuttingDown() || !findLanguage(target) || !isTranslatable(clean)) {
        if (callback) callback(false, "");
        return;
    }

    // emote codes travel as placeholders so the engine cannot translate them.
    // the same text always maps to the same placeholders, so the cache and the
    // shared in-flight requests keep working on the protected form.
    auto protectedText = protectEmoteCodes(clean);
    Callback deliver = [callback = std::move(callback),
                        codes = std::move(protectedText.codes)](bool ok, std::string result) mutable {
        if (!callback) return;
        if (ok) result = restoreEmoteCodes(std::move(result), codes);
        callback(ok, std::move(result));
    };

    auto key = cacheKey(target, protectedText.text);
    if (auto cached = m_cache.find(key); cached != m_cache.end()) {
        deliver(true, cached->second);
        return;
    }

    auto& waiters = m_pending[key];
    bool const first = waiters.empty();
    waiters.push_back(std::move(deliver));
    if (first) requestEndpoint(key, protectedText.text, target, loadSettings().endpoint, true);
}

void TranslationService::clearCache() {
    m_cache.clear();
}

void TranslationService::requestEndpoint(
    std::string const& key, std::string const& text, std::string const& target,
    std::string const& endpoint, bool allowBuiltinFallback
) {
    auto const url = buildUrl(endpoint, text, target);
    auto const kind = classify(endpoint);

    auto req = web::WebRequest();
    req.timeout(asp::Duration::fromSecs(12));
    req.header("Accept", "application/json");
    req.header("User-Agent", kUserAgent);
    WebHelper::dispatch(std::move(req), "GET", url,
        [this, key, text, target, endpoint, allowBuiltinFallback, kind](
            web::WebResponse res) {
            if (paimon::isRuntimeShuttingDown()) return;

            std::string translated;
            auto const body = res.ok() ? res.string().unwrapOr("") : std::string();
            bool const parsed = kind == EndpointKind::Lingva ? parseLingva(body, translated)
                                                             : parseGoogle(body, translated);
            if (res.ok() && parsed) {
                resolve(key, true, std::move(translated));
                return;
            }

            log::warn("[Translator] endpoint failed (code {})", res.code());

            // a user endpoint that fails falls back to the built-in chain.
            if (allowBuiltinFallback && endpoint != defaultEndpoint()) {
                requestEndpoint(key, text, target, defaultEndpoint(), false);
                return;
            }
            if (endpoint != kSecondaryEndpoint) {
                requestEndpoint(key, text, target, kSecondaryEndpoint, false);
                return;
            }
            resolve(key, false, "");
        });
}

void TranslationService::resolve(std::string const& key, bool ok, std::string text) {
    std::vector<Callback> waiters;
    if (auto it = m_pending.find(key); it != m_pending.end()) {
        waiters = std::move(it->second);
        m_pending.erase(it);
    }

    if (ok && !text.empty()) {
        if (m_cache.size() >= kMaxCacheEntries) m_cache.clear();
        m_cache[key] = text;
    }

    for (auto& callback : waiters) {
        if (callback) callback(ok, text);
    }
}

}
