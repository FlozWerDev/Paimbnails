#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::chatimg::crypto {

inline constexpr std::array<uint8_t, 6> kMagic = {'P', 'I', 'M', 'G', '2', 0x00};
inline constexpr size_t kSeedBytes = 16;
inline constexpr size_t kNonceBytes = 12;
inline constexpr size_t kTagBytes = 16;
inline constexpr size_t kKeyBytes = 32;
inline constexpr size_t kKeyTextLen = 22; // unpadded base64url(16 bytes)
inline constexpr size_t kEnvelopeOverhead = kMagic.size() + kNonceBytes + kTagBytes; // 34 bytes

bool fillRandomBytes(std::span<uint8_t> out);

std::string base64UrlEncode(std::span<uint8_t const> bytes);
bool base64UrlDecode(std::string_view text, std::vector<uint8_t>& out);

bool chacha20Poly1305Encrypt(
    std::span<uint8_t const, kKeyBytes> key,
    std::span<uint8_t const, kNonceBytes> nonce,
    std::span<uint8_t const> aad,
    std::span<uint8_t const> plaintext,
    std::vector<uint8_t>& outCiphertext,
    std::array<uint8_t, kTagBytes>& outTag
);

bool chacha20Poly1305Decrypt(
    std::span<uint8_t const, kKeyBytes> key,
    std::span<uint8_t const, kNonceBytes> nonce,
    std::span<uint8_t const> aad,
    std::span<uint8_t const> ciphertext,
    std::span<uint8_t const, kTagBytes> tag,
    std::vector<uint8_t>& outPlaintext
);

bool sealChatImage(
    std::span<uint8_t const> plaintext,
    std::string_view ext,
    int width,
    int height,
    int threadID,
    std::vector<uint8_t>& outBlob,
    std::string& outKey22
);

bool openChatImage(
    std::span<uint8_t const> blob,
    std::string_view ext,
    int width,
    int height,
    int threadID,
    std::string_view key22,
    std::vector<uint8_t>& outPlaintext
);

} // namespace paimon::chatimg::crypto
