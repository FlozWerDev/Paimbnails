#include "../src/features/thread-images/ChatImageCrypto.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace paimon::chatimg::crypto;

static std::vector<uint8_t> hexToBytes(std::string_view hex) {
    assert(hex.size() % 2 == 0);
    std::vector<uint8_t> out(hex.size() / 2);
    auto nib = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + c - 'a');
        assert(false);
        return 0;
    };
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<uint8_t>((nib(hex[i * 2]) << 4) | nib(hex[i * 2 + 1]));
    }
    return out;
}

int main() {
    // 1. Official RFC 8439 Section 2.8.2 AEAD test vector
    {
        auto keyVec = hexToBytes("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
        auto nonceVec = hexToBytes("070000004041424344454647");
        auto aadVec = hexToBytes("50515253c0c1c2c3c4c5c6c7");
        std::string_view ptStr =
            "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
        auto expectedCt = hexToBytes("d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b6116");
        auto expectedTag = hexToBytes("1ae10b594f09e26a7e902ecbd0600691");

        std::array<uint8_t, kKeyBytes> key{};
        std::array<uint8_t, kNonceBytes> nonce{};
        std::memcpy(key.data(), keyVec.data(), kKeyBytes);
        std::memcpy(nonce.data(), nonceVec.data(), kNonceBytes);
        std::span<uint8_t const> pt(reinterpret_cast<uint8_t const*>(ptStr.data()), ptStr.size());

        std::vector<uint8_t> ct;
        std::array<uint8_t, kTagBytes> tag{};
        assert(chacha20Poly1305Encrypt(key, nonce, aadVec, pt, ct, tag));
        assert(ct == expectedCt);
        assert(std::equal(tag.begin(), tag.end(), expectedTag.begin()));

        std::vector<uint8_t> dec;
        assert(chacha20Poly1305Decrypt(key, nonce, aadVec, ct, tag, dec));
        assert(dec.size() == pt.size() && std::equal(dec.begin(), dec.end(), pt.begin()));

        // Tampering with ciphertext or tag or AAD fails
        ct[5] ^= 0x01;
        assert(!chacha20Poly1305Decrypt(key, nonce, aadVec, ct, tag, dec));
        ct[5] ^= 0x01;

        tag[0] ^= 0x80;
        assert(!chacha20Poly1305Decrypt(key, nonce, aadVec, ct, tag, dec));
        tag[0] ^= 0x80;

        aadVec[0] ^= 0x01;
        assert(!chacha20Poly1305Decrypt(key, nonce, aadVec, ct, tag, dec));
    }

    // 2. base64url roundtrip and strict padding-bit checks
    {
        std::vector<uint8_t> sample(16);
        for (size_t i = 0; i < sample.size(); ++i) sample[i] = static_cast<uint8_t>(i * 17 + 3);
        std::string b64 = base64UrlEncode(sample);
        assert(b64.size() == kKeyTextLen);
        std::vector<uint8_t> back;
        assert(base64UrlDecode(b64, back));
        assert(back == sample);

        // Trailing char with non-zero unused low bits must be rejected
        std::string bad = "ABCDEFGHIJKLMNOPQRSTUB";
        assert(!base64UrlDecode(bad, back));
        assert(!base64UrlDecode("bad=padding=", back));
    }

    // 3. PIMG2 seal / open binding to threadID, dimensions, and ext
    {
        std::vector<uint8_t> fakePng = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82};
        std::vector<uint8_t> blob1;
        std::vector<uint8_t> blob2;
        std::string key1;
        std::string key2;
        assert(sealChatImage(fakePng, "png", 640, 480, 4242, blob1, key1));
        assert(sealChatImage(fakePng, "png", 640, 480, 4242, blob2, key2));
        assert(key1.size() == kKeyTextLen && key2.size() == kKeyTextLen);
        assert(key1 != key2);
        assert(blob1 != blob2);
        assert(std::equal(kMagic.begin(), kMagic.end(), blob1.begin()));

        std::vector<uint8_t> out;
        assert(openChatImage(blob1, "png", 640, 480, 4242, key1, out));
        assert(out == fakePng);

        // Wrong threadID cannot decrypt even with the exact key and blob!
        assert(!openChatImage(blob1, "png", 640, 480, 4243, key1, out));
        assert(out.empty());
        assert(!openChatImage(blob1, "png", 640, 480, 0, key1, out));
        assert(!openChatImage(blob1, "jpg", 640, 480, 4242, key1, out));
        assert(!openChatImage(blob1, "png", 641, 480, 4242, key1, out));
        assert(!openChatImage(blob1, "png", 640, 480, 4242, key2, out));

        // Flip nonce, magic, ciphertext, and tag bytes
        blob1[0] ^= 0x01;
        assert(!openChatImage(blob1, "png", 640, 480, 4242, key1, out));
        blob1[0] ^= 0x01;

        blob1[8] ^= 0x10;
        assert(!openChatImage(blob1, "png", 640, 480, 4242, key1, out));
        blob1[8] ^= 0x10;

        blob1[20] ^= 0x40;
        assert(!openChatImage(blob1, "png", 640, 480, 4242, key1, out));
        blob1[20] ^= 0x40;

        blob1.back() ^= 0x01;
        assert(!openChatImage(blob1, "png", 640, 480, 4242, key1, out));
    }

    std::cout << "chat_image_crypto_regression: OK\n";
    return 0;
}
