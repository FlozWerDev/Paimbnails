#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::translate {

using Callback = geode::CopyableFunction<void(bool ok, std::string text)>;

// free engines, no API key and no account:
//  - Google's public gtx endpoint, which also detects the source language
//  - Lingva, a keyless Google front end, as a fallback when gtx fails
// main-thread only; every callback runs on the main thread.
class TranslationService {
public:
    static TranslationService& get();

    // `target` is a code from TranslatorLanguages. Results are cached per
    // target + text, and identical in-flight requests share one network call.
    void translate(std::string const& text, std::string const& target, Callback callback);

    void clearCache();

private:
    void requestEndpoint(std::string const& key, std::string const& text,
        std::string const& target, std::string const& endpoint, bool allowBuiltinFallback);
    void resolve(std::string const& key, bool ok, std::string text);

    std::unordered_map<std::string, std::string> m_cache;
    std::unordered_map<std::string, std::vector<Callback>> m_pending;
};

}
