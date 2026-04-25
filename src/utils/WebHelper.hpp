#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/function.hpp>
#include <string>
#include <algorithm>
#include <cctype>
#include <memory>

/**
 * WebHelper — Centralized async web dispatch for Paimbnails.
 *
 * Uses Geode v5 native async::spawn + WebFuture for non-blocking requests.
 * The callback is guaranteed to run on the main (Cocos2d-x) thread.
 */
namespace WebHelper {

inline std::string normalizeMethod(std::string method) {
    std::transform(
        method.begin(),
        method.end(),
        method.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); }
    );
    return method;
}

/**
 * Dispatch a web request asynchronously (fire-and-forget).
 * The callback is guaranteed to run on the main thread.
 *
 * @param req    The prepared WebRequest (moved in).
 * @param method "GET" or "POST".
 * @param url    The target URL.
 * @param cb     Callback receiving the WebResponse on the main thread.
 */
inline void dispatch(
    geode::utils::web::WebRequest&& req,
    std::string const& method,
    std::string const& url,
    geode::CopyableFunction<void(geode::utils::web::WebResponse)> cb
) {
    auto future = req.send(normalizeMethod(method), url);

    auto safeCb = std::make_shared<decltype(cb)>(std::move(cb));

    auto handle = geode::async::spawn(std::move(future), [safeCb](geode::utils::web::WebResponse res) {
        if (safeCb && *safeCb) {
            (*safeCb)(std::move(res));
        }
    });
    handle.setName("Paimbnails WebRequest");
}

inline void dispatchOwned(
    geode::async::TaskHolder<geode::utils::web::WebResponse>& owner,
    geode::utils::web::WebRequest&& req,
    std::string const& method,
    std::string const& url,
    geode::CopyableFunction<void(geode::utils::web::WebResponse)> cb
) {
    auto future = req.send(normalizeMethod(method), url);
    auto safeCb = std::make_shared<decltype(cb)>(std::move(cb));
    owner.spawn("Paimbnails WebRequest", std::move(future), [safeCb](geode::utils::web::WebResponse res) {
        if (safeCb && *safeCb) {
            (*safeCb)(std::move(res));
        }
    });
}

} // namespace WebHelper

