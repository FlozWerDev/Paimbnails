#include "../src/utils/HmacSha256.hpp"

#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string toHex(std::uint8_t const* data, std::size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(size * 2, '\0');
    for (std::size_t i = 0; i < size; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0f];
    }
    return out;
}

std::span<std::uint8_t const> bytesOf(std::string_view text) {
    return std::span<std::uint8_t const>(
        reinterpret_cast<std::uint8_t const*>(text.data()), text.size());
}

void expectSha(std::string_view input, std::string_view expected) {
    std::uint8_t digest[32]{};
    paimon::crypto::sha256(bytesOf(input), digest);
    assert(toHex(digest, sizeof(digest)) == std::string(expected));
}

void expectHmac(std::span<std::uint8_t const> key,
                std::string_view message,
                std::string_view expected) {
    std::uint8_t digest[32]{};
    paimon::crypto::hmacSha256(key, bytesOf(message), digest);
    assert(toHex(digest, sizeof(digest)) == std::string(expected));
}

} // namespace

int main() {
    expectSha("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expectSha("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    expectSha("The quick brown fox jumps over the lazy dog",
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

    // RFC 4231 case 1.
    std::vector<std::uint8_t> key(20, 0x0b);
    expectHmac(std::span<std::uint8_t const>(key.data(), key.size()),
        "Hi There",
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    // RFC 4231 case 2.
    expectHmac(bytesOf("Jefe"),
        "what do ya want for nothing?",
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

    // RFC 4231 case 6: exercises the key > block-size hash path.
    std::vector<std::uint8_t> longKey(131, 0xaa);
    expectHmac(std::span<std::uint8_t const>(longKey.data(), longKey.size()),
        "Test Using Larger Than Block-Size Key - Hash Key First",
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

    return 0;
}
