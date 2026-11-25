#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <string>
#include <algorithm>
#include <cctype>
#include <memory>
#include "../core/BanGate.hpp"

// non-blocking web dispatch; callbacks on main thread.
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

inline void dispatch(
    geode::utils::web::WebRequest&& req,
    std::string const& method,
    std::string const& url,
    geode::CopyableFunction<void(geode::utils::web::WebResponse)> cb
) {
    auto const permit = paimon::ban::authorizeWebRequest(method, url);
    if (!permit) {
        geode::queueInMainThread([cb = std::move(cb)] {
            if (cb) cb(geode::utils::web::WebResponse{});
        });
        return;
    }
    auto future = req.send(normalizeMethod(method), url);

    auto safeCb = std::make_shared<decltype(cb)>(std::move(cb));

    auto handle = geode::async::spawn(std::move(future), [safeCb, permit, method, url](geode::utils::web::WebResponse res) {
        if (safeCb && *safeCb) {
            if (paimon::ban::authorizeWebRequest(method, url) != permit) {
                (*safeCb)(geode::utils::web::WebResponse{});
                return;
            }
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
    auto const permit = paimon::ban::authorizeWebRequest(method, url);
    if (!permit) {
        geode::queueInMainThread([cb = std::move(cb)] {
            if (cb) cb(geode::utils::web::WebResponse{});
        });
        return;
    }
    auto future = req.send(normalizeMethod(method), url);
    auto safeCb = std::make_shared<decltype(cb)>(std::move(cb));
    owner.spawn("Paimbnails WebRequest", std::move(future), [safeCb, permit, method, url](geode::utils::web::WebResponse res) {
        if (safeCb && *safeCb) {
            if (paimon::ban::authorizeWebRequest(method, url) != permit) {
                (*safeCb)(geode::utils::web::WebResponse{});
                return;
            }
            (*safeCb)(std::move(res));
        }
    });
}

} // namespace webhelper
