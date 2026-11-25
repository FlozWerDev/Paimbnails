#include "HmacSha256.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace paimon::crypto {
namespace {

constexpr std::uint32_t kRound[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr std::uint32_t rotr(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32u - bits));
}

void compress(std::uint32_t state[8], std::uint8_t const* block) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24)
             | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
             | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
             |  static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        std::uint32_t const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        std::uint32_t const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (int i = 0; i < 64; ++i) {
        std::uint32_t const s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        std::uint32_t const ch = (e & f) ^ (~e & g);
        std::uint32_t const t1 = h + s1 + ch + kRound[i] + w[i];
        std::uint32_t const s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        std::uint32_t const maj = (a & b) ^ (a & c) ^ (b & c);
        std::uint32_t const t2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

void sha256(std::span<std::uint8_t const> data, std::uint8_t out[32]) {
    std::uint32_t state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };

    std::size_t offset = 0;
    while (offset + 64 <= data.size()) {
        compress(state, data.data() + offset);
        offset += 64;
    }

    std::array<std::uint8_t, 128> tail{};
    std::size_t const remaining = data.size() - offset;
    if (remaining != 0) {
        std::memcpy(tail.data(), data.data() + offset, remaining);
    }
    tail[remaining] = 0x80;

    std::size_t const tailBytes = remaining < 56 ? 64 : 128;
    std::uint64_t const bitLength = static_cast<std::uint64_t>(data.size()) * 8u;
    for (int i = 0; i < 8; ++i) {
        tail[tailBytes - 1 - static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(bitLength >> (i * 8));
    }

    compress(state, tail.data());
    if (tailBytes == 128) {
        compress(state, tail.data() + 64);
    }

    for (int i = 0; i < 8; ++i) {
        out[i * 4]     = static_cast<std::uint8_t>(state[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(state[i]);
    }
}

void hmacSha256(std::span<std::uint8_t const> key,
                std::span<std::uint8_t const> message,
                std::uint8_t out[32]) {
    std::array<std::uint8_t, 64> keyBlock{};
    if (key.size() > keyBlock.size()) {
        sha256(key, keyBlock.data());
    } else if (!key.empty()) {
        std::memcpy(keyBlock.data(), key.data(), key.size());
    }

    std::array<std::uint8_t, 64> innerPad{};
    std::array<std::uint8_t, 64> outerPad{};
    for (std::size_t i = 0; i < keyBlock.size(); ++i) {
        innerPad[i] = keyBlock[i] ^ 0x36;
        outerPad[i] = keyBlock[i] ^ 0x5c;
    }

    std::vector<std::uint8_t> inner;
    inner.reserve(innerPad.size() + message.size());
    inner.insert(inner.end(), innerPad.begin(), innerPad.end());
    inner.insert(inner.end(), message.begin(), message.end());

    std::array<std::uint8_t, 32> innerHash{};
    sha256(std::span<std::uint8_t const>(inner.data(), inner.size()), innerHash.data());

    std::array<std::uint8_t, 96> outer{};
    std::memcpy(outer.data(), outerPad.data(), outerPad.size());
    std::memcpy(outer.data() + outerPad.size(), innerHash.data(), innerHash.size());

    sha256(std::span<std::uint8_t const>(outer.data(), outer.size()), out);
}

bool constantTimeEquals(std::span<std::uint8_t const> a,
                        std::span<std::uint8_t const> b) {
    if (a.size() != b.size()) {
        return false;
    }
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

} // namespace paimon::crypto
