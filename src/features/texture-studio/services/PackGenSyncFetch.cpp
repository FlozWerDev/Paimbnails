#include "PackGenSyncFetch.hpp"

#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace paimon::texture_studio {

geode::Result<std::vector<std::uint8_t>> syncFetchBytes(
    std::string url,
    std::chrono::seconds timeout) {

    // Geode 5's WebRequest::getSync blocks until the response arrives.
    // Set the timeout via the request builder before invoking the sync
    // form so the call cannot hang indefinitely on a flaky network.
    auto response = web::WebRequest()
        .timeout(timeout)
        .userAgent("Paimbnails/TextureStudio (PackGen client)")
        .acceptEncoding("gzip, deflate")
        .getSync(url);

    if (response.cancelled()) {
        return Err("request cancelled");
    }
    if (!response.ok()) {
        std::string msg = std::string(response.errorMessage());
        if (msg.empty()) msg = fmt::format("HTTP {}", response.code());
        return Err("download failed: " + msg);
    }

    // Move the bytes out of the response. ByteVector is std::vector<uint8_t>.
    auto bytes = std::move(response).data();
    if (bytes.empty()) {
        return Err("empty response body");
    }
    return Ok(std::move(bytes));
}

geode::Result<bool> syncCheckExists(std::string url, std::chrono::seconds timeout) {
    // We deliberately do a real GET instead of a HEAD: many static hosts
    // (Cloudflare Pages included) handle HEAD with a generic 200 from the
    // SPA shell, which would falsely report missing assets as existing.
    auto response = web::WebRequest()
        .timeout(timeout)
        .userAgent("Paimbnails/TextureStudio (PackGen client)")
        .getSync(url);

    if (response.cancelled()) {
        return Err("request cancelled");
    }
    if (response.code() == 404) {
        return Ok(false);
    }
    if (!response.ok()) {
        // 5xx etc.: treat as "we don't know", but surface as Err so the
        // caller doesn't accidentally treat the file as missing.
        return Err(fmt::format("HEAD-equivalent failed: HTTP {}", response.code()));
    }
    // Some hosts return 200 with an HTML body for any path under their
    // SPA fallback. Distinguish by content type / body size.
    auto contentType = response.header("Content-Type");
    if (contentType.has_value()) {
        std::string ct(contentType.value());
        // A real PNG / plist response will be image/png or text/xml-ish;
        // an HTML SPA fallback identifies itself with text/html.
        if (ct.find("text/html") != std::string::npos) {
            return Ok(false);
        }
    }
    return Ok(true);
}

}  // namespace paimon::texture_studio
