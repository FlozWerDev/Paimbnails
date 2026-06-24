#include "PackGenSyncFetch.hpp"

#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace paimon::texture_studio {

geode::Result<std::vector<std::uint8_t>> syncFetchBytes(
    std::string url,
    std::chrono::seconds timeout) {

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

    auto bytes = std::move(response).data();
    if (bytes.empty()) {
        return Err("empty response body");
    }
    return Ok(std::move(bytes));
}

geode::Result<bool> syncCheckExists(std::string url, std::chrono::seconds timeout) {
    // Use GET instead of HEAD: Cloudflare Pages returns 200 for HEAD on SPA
    // catch-all routes, falsely reporting missing assets as existing.
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
        return Err(fmt::format("HEAD-equivalent failed: HTTP {}", response.code()));
    }
    auto contentType = response.header("Content-Type");
    if (contentType.has_value()) {
        std::string ct(contentType.value());
        if (ct.find("text/html") != std::string::npos) {
            return Ok(false);
        }
    }
    return Ok(true);
}

}  // namespace paimon::texture_studio
