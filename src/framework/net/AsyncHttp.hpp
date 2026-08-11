#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <arc/future/Future.hpp>
#include <string>
#include <chrono>

// Coroutine helpers over Geode WebRequest; requires C++23.
namespace AsyncHttp {

    using namespace geode::prelude;

    inline arc::Future<Result<std::string>> get(
        std::string url,
        std::chrono::seconds timeout = std::chrono::seconds(10)
    ) {
        auto response = co_await web::WebRequest()
            .timeout(timeout)
            .acceptEncoding("gzip, deflate")
            .get(url);

        if (!response.ok()) {
            co_return Err(fmt::format("HTTP {}: {}", response.code(),
                response.string().unwrapOr("Unknown error")));
        }

        co_return Ok(response.string().unwrapOr(""));
    }

    inline arc::Future<Result<std::string>> post(
        std::string url,
        std::string body,
        std::chrono::seconds timeout = std::chrono::seconds(10)
    ) {
        auto response = co_await web::WebRequest()
            .timeout(timeout)
            .acceptEncoding("gzip, deflate")
            .header("Content-Type", "application/json")
            .bodyString(body)
            .post(url);

        if (!response.ok()) {
            co_return Err(fmt::format("HTTP {}: {}", response.code(),
                response.string().unwrapOr("Unknown error")));
        }

        co_return Ok(response.string().unwrapOr(""));
    }

    inline arc::Future<Result<std::string>> postWithAuth(
        std::string url,
        std::string body,
        std::string modCode,
        std::chrono::seconds timeout = std::chrono::seconds(10)
    ) {
        auto req = web::WebRequest()
            .timeout(timeout)
            .acceptEncoding("gzip, deflate")
            .header("Content-Type", "application/json")
            .bodyString(body);

        if (!modCode.empty()) {
            req.header("X-Mod-Code", modCode);
        }

        auto response = co_await req.post(url);

        if (!response.ok()) {
            co_return Err(fmt::format("HTTP {}: {}", response.code(),
                response.string().unwrapOr("Unknown error")));
        }

        co_return Ok(response.string().unwrapOr(""));
    }

    inline arc::Future<Result<ByteVector>> download(
        std::string url,
        std::chrono::seconds timeout = std::chrono::seconds(15)
    ) {
        auto response = co_await web::WebRequest()
            .timeout(timeout)
            .acceptEncoding("gzip, deflate")
            .get(url);

        if (!response.ok()) {
            co_return Err(fmt::format("HTTP {}: download failed", response.code()));
        }

        co_return Ok(std::move(response).data());
    }

} // namespace AsyncHttp
