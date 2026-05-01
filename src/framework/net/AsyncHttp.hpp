#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <arc/future/Future.hpp>
#include <string>
#include <chrono>

/**
 * AsyncHttp — Coroutine-based HTTP helpers for Paimbnails.
 *
 * Uses native Geode 5.x co_await support on web::WebRequest.
 * Requires C++23 (coroutine support).
 *
 * Usage:
 *   auto result = co_await AsyncHttp::get("/api/thumbnails/123");
 *   if (result.isOk()) {
 *       auto& body = result.unwrap();
 *       // use body
 *   }
 *
 *   auto binary = co_await AsyncHttp::download("https://cdn.example.com/image.png");
 *   if (binary.isOk()) {
 *       auto& bytes = binary.unwrap();
 *   }
 */
namespace AsyncHttp {

    using namespace geode::prelude;

    /**
     * GET request that returns the response body as string.
     */
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

    /**
     * POST request with JSON body, returns the response body as string.
     */
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

    /**
     * POST with auth header (X-Mod-Code), returns response as string.
     */
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

    /**
     * Download binary data from a URL.
     */
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
