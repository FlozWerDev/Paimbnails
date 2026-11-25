#pragma once

#include <cstdint>
#include <span>

namespace paimon::crypto {

void sha256(std::span<std::uint8_t const> data, std::uint8_t out[32]);

void hmacSha256(std::span<std::uint8_t const> key,
                std::span<std::uint8_t const> message,
                std::uint8_t out[32]);

bool constantTimeEquals(std::span<std::uint8_t const> a,
                        std::span<std::uint8_t const> b);

} // namespace paimon::crypto
