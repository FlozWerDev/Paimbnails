#include "ChatImageCrypto.hpp"

#include "../../utils/HmacSha256.hpp"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <bcrypt.h>
#elif defined(__APPLE__)
    #include <stdlib.h>
#else
    #include <errno.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace paimon::chatimg::crypto {

namespace {

constexpr uint32_t load32Le(uint8_t const* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

void store32Le(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

void store64Le(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xFFu);
    }
}

constexpr uint32_t rotl32(uint32_t v, unsigned n) {
    return (v << n) | (v >> (32u - n));
}

void quarterRound(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    a += b; d ^= a; d = rotl32(d, 16);
    c += d; b ^= c; b = rotl32(b, 12);
    a += b; d ^= a; d = rotl32(d, 8);
    c += d; b ^= c; b = rotl32(b, 7);
}

void chacha20Block(
    uint8_t const key[kKeyBytes],
    uint32_t counter,
    uint8_t const nonce[kNonceBytes],
    uint8_t out[64]
) {
    static constexpr uint32_t kConsts[4] = {
        0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u
    };
    uint32_t state[16] = {
        kConsts[0], kConsts[1], kConsts[2], kConsts[3],
        load32Le(key + 0),  load32Le(key + 4),  load32Le(key + 8),  load32Le(key + 12),
        load32Le(key + 16), load32Le(key + 20), load32Le(key + 24), load32Le(key + 28),
        counter,
        load32Le(nonce + 0), load32Le(nonce + 4), load32Le(nonce + 8)
    };
    uint32_t w[16];
    std::memcpy(w, state, sizeof(state));

    for (int i = 0; i < 10; ++i) {
        quarterRound(w[0], w[4], w[8],  w[12]);
        quarterRound(w[1], w[5], w[9],  w[13]);
        quarterRound(w[2], w[6], w[10], w[14]);
        quarterRound(w[3], w[7], w[11], w[15]);
        quarterRound(w[0], w[5], w[10], w[15]);
        quarterRound(w[1], w[6], w[11], w[12]);
        quarterRound(w[2], w[7], w[8],  w[13]);
        quarterRound(w[3], w[4], w[9],  w[14]);
    }
    for (int i = 0; i < 16; ++i) {
        store32Le(out + i * 4, w[i] + state[i]);
    }
}

void chacha20Xor(
    uint8_t const key[kKeyBytes],
    uint32_t initialCounter,
    uint8_t const nonce[kNonceBytes],
    uint8_t const* in,
    uint8_t* out,
    size_t len
) {
    uint8_t block[64];
    uint32_t counter = initialCounter;
    size_t offset = 0;
    while (offset < len) {
        chacha20Block(key, counter++, nonce, block);
        size_t chunk = std::min<size_t>(64, len - offset);
        for (size_t i = 0; i < chunk; ++i) {
            out[offset + i] = in[offset + i] ^ block[i];
        }
        offset += chunk;
    }
}

class Poly1305 {
public:
    explicit Poly1305(uint8_t const key32[32]) {
        m_r[0] = load32Le(key32 + 0) & 0x3ffffffu;
        m_r[1] = (load32Le(key32 + 3) >> 2) & 0x3ffff03u;
        m_r[2] = (load32Le(key32 + 6) >> 4) & 0x3ffc0ffu;
        m_r[3] = (load32Le(key32 + 9) >> 6) & 0x3f03fffu;
        m_r[4] = (load32Le(key32 + 12) >> 8) & 0x00fffffu;
        std::memcpy(m_pad, key32 + 16, 16);
    }

    void update(uint8_t const* data, size_t len) {
        if (!len || !data) return;
        if (m_bufUsed > 0) {
            size_t take = std::min(16 - m_bufUsed, len);
            std::memcpy(m_buf + m_bufUsed, data, take);
            m_bufUsed += take;
            data += take;
            len -= take;
            if (m_bufUsed == 16) {
                processBlock(m_buf, true);
                m_bufUsed = 0;
            }
        }
        while (len >= 16) {
            processBlock(data, true);
            data += 16;
            len -= 16;
        }
        if (len > 0) {
            std::memcpy(m_buf, data, len);
            m_bufUsed = len;
        }
    }

    void updateZeroPad16(size_t dataLen) {
        static constexpr uint8_t kZeros[16]{};
        size_t rem = dataLen % 16u;
        if (rem != 0) update(kZeros, 16u - rem);
    }

    void finish(uint8_t outTag[16]) {
        if (m_bufUsed > 0) {
            std::memset(m_buf + m_bufUsed, 0, 16 - m_bufUsed);
            m_buf[m_bufUsed] = 1;
            processBlock(m_buf, false);
            m_bufUsed = 0;
        }

        uint32_t h0 = m_h[0], h1 = m_h[1], h2 = m_h[2], h3 = m_h[3], h4 = m_h[4];
        uint32_t c;
        c = h1 >> 26; h1 &= 0x3ffffffu; h2 += c;
        c = h2 >> 26; h2 &= 0x3ffffffu; h3 += c;
        c = h3 >> 26; h3 &= 0x3ffffffu; h4 += c;
        c = h4 >> 26; h4 &= 0x3ffffffu; h0 += c * 5u;
        c = h0 >> 26; h0 &= 0x3ffffffu; h1 += c;

        uint32_t g0 = h0 + 5u; c = g0 >> 26; g0 &= 0x3ffffffu;
        uint32_t g1 = h1 + c;  c = g1 >> 26; g1 &= 0x3ffffffu;
        uint32_t g2 = h2 + c;  c = g2 >> 26; g2 &= 0x3ffffffu;
        uint32_t g3 = h3 + c;  c = g3 >> 26; g3 &= 0x3ffffffu;
        uint32_t g4 = h4 + c - (1u << 26);

        uint32_t mask = (g4 >> 31) - 1u;
        g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
        uint32_t inv = ~mask;
        h0 = (h0 & inv) | g0;
        h1 = (h1 & inv) | g1;
        h2 = (h2 & inv) | g2;
        h3 = (h3 & inv) | g3;
        h4 = (h4 & inv) | g4;

        h0 = (h0) | (h1 << 26);
        h1 = (h1 >> 6) | (h2 << 20);
        h2 = (h2 >> 12) | (h3 << 14);
        h3 = (h3 >> 18) | (h4 << 8);

        uint64_t f = static_cast<uint64_t>(h0) + load32Le(m_pad + 0);  h0 = static_cast<uint32_t>(f);
        f = static_cast<uint64_t>(h1) + load32Le(m_pad + 4)  + (f >> 32); h1 = static_cast<uint32_t>(f);
        f = static_cast<uint64_t>(h2) + load32Le(m_pad + 8)  + (f >> 32); h2 = static_cast<uint32_t>(f);
        f = static_cast<uint64_t>(h3) + load32Le(m_pad + 12) + (f >> 32); h3 = static_cast<uint32_t>(f);

        store32Le(outTag + 0, h0);
        store32Le(outTag + 4, h1);
        store32Le(outTag + 8, h2);
        store32Le(outTag + 12, h3);
    }

private:
    void processBlock(uint8_t const block[16], bool fullBlock) {
        uint32_t const hibit = fullBlock ? (1u << 24) : 0u;
        uint32_t h0 = m_h[0] + (load32Le(block + 0) & 0x3ffffffu);
        uint32_t h1 = m_h[1] + ((load32Le(block + 3) >> 2) & 0x3ffffffu);
        uint32_t h2 = m_h[2] + ((load32Le(block + 6) >> 4) & 0x3ffffffu);
        uint32_t h3 = m_h[3] + ((load32Le(block + 9) >> 6) & 0x3ffffffu);
        uint32_t h4 = m_h[4] + ((load32Le(block + 12) >> 8) | hibit);

        uint32_t const r0 = m_r[0], r1 = m_r[1], r2 = m_r[2], r3 = m_r[3], r4 = m_r[4];
        uint32_t const s1 = r1 * 5u, s2 = r2 * 5u, s3 = r3 * 5u, s4 = r4 * 5u;

        uint64_t d0 = uint64_t(h0) * r0 + uint64_t(h1) * s4 + uint64_t(h2) * s3 + uint64_t(h3) * s2 + uint64_t(h4) * s1;
        uint64_t d1 = uint64_t(h0) * r1 + uint64_t(h1) * r0 + uint64_t(h2) * s4 + uint64_t(h3) * s3 + uint64_t(h4) * s2;
        uint64_t d2 = uint64_t(h0) * r2 + uint64_t(h1) * r1 + uint64_t(h2) * r0 + uint64_t(h3) * s4 + uint64_t(h4) * s3;
        uint64_t d3 = uint64_t(h0) * r3 + uint64_t(h1) * r2 + uint64_t(h2) * r1 + uint64_t(h3) * r0 + uint64_t(h4) * s4;
        uint64_t d4 = uint64_t(h0) * r4 + uint64_t(h1) * r3 + uint64_t(h2) * r2 + uint64_t(h3) * r1 + uint64_t(h4) * r0;

        uint32_t c;
        c = static_cast<uint32_t>(d0 >> 26); m_h[0] = static_cast<uint32_t>(d0) & 0x3ffffffu; d1 += c;
        c = static_cast<uint32_t>(d1 >> 26); m_h[1] = static_cast<uint32_t>(d1) & 0x3ffffffu; d2 += c;
        c = static_cast<uint32_t>(d2 >> 26); m_h[2] = static_cast<uint32_t>(d2) & 0x3ffffffu; d3 += c;
        c = static_cast<uint32_t>(d3 >> 26); m_h[3] = static_cast<uint32_t>(d3) & 0x3ffffffu; d4 += c;
        c = static_cast<uint32_t>(d4 >> 26); m_h[4] = static_cast<uint32_t>(d4) & 0x3ffffffu; m_h[0] += c * 5u;
        c = m_h[0] >> 26;                    m_h[0] &= 0x3ffffffu;                            m_h[1] += c;
    }

    uint32_t m_r[5]{};
    uint32_t m_h[5]{};
    uint8_t m_pad[16]{};
    uint8_t m_buf[16]{};
    size_t m_bufUsed = 0;
};

void computeAeadTag(
    uint8_t const otk[32],
    std::span<uint8_t const> aad,
    std::span<uint8_t const> ciphertext,
    uint8_t outTag[kTagBytes]
) {
    Poly1305 poly(otk);
    poly.update(aad.data(), aad.size());
    poly.updateZeroPad16(aad.size());
    poly.update(ciphertext.data(), ciphertext.size());
    poly.updateZeroPad16(ciphertext.size());
    uint8_t lens[16];
    store64Le(lens + 0, static_cast<uint64_t>(aad.size()));
    store64Le(lens + 8, static_cast<uint64_t>(ciphertext.size()));
    poly.update(lens, sizeof(lens));
    poly.finish(outTag);
}

bool validExt(std::string_view ext) {
    return ext == "jpg" || ext == "png" || ext == "webp" || ext == "gif";
}

void deriveThreadKey(
    std::span<uint8_t const, kSeedBytes> seed,
    int threadID,
    std::array<uint8_t, kKeyBytes>& outKey
) {
    std::string threadStr = std::to_string(threadID);
    constexpr std::string_view kPrefix = "paimon-chatimg-v2-key|";
    std::vector<uint8_t> material;
    material.reserve(kPrefix.size() + kSeedBytes + 1 + threadStr.size());
    material.insert(material.end(), kPrefix.begin(), kPrefix.end());
    material.insert(material.end(), seed.begin(), seed.end());
    material.push_back('|');
    material.insert(material.end(), threadStr.begin(), threadStr.end());
    paimon::crypto::sha256(material, outKey.data());
    std::fill(material.begin(), material.end(), uint8_t{0});
}

std::string buildAad(std::string_view ext, int width, int height, int threadID) {
    return "paimon-chatimg-v2|" + std::string(ext) + "|"
         + std::to_string(width) + "x" + std::to_string(height) + "|"
         + std::to_string(threadID);
}

constexpr char kB64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int b64UrlVal(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return 26 + (ch - 'a');
    if (ch >= '0' && ch <= '9') return 52 + (ch - '0');
    if (ch == '-') return 62;
    if (ch == '_') return 63;
    return -1;
}

} // namespace

bool fillRandomBytes(std::span<uint8_t> out) {
    if (out.empty()) return true;
#ifdef _WIN32
    return BCryptGenRandom(
        nullptr,
        out.data(),
        static_cast<ULONG>(out.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    ) == 0;
#elif defined(__APPLE__)
    arc4random_buf(out.data(), out.size());
    return true;
#else
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    size_t done = 0;
    while (done < out.size()) {
        ssize_t n = ::read(fd, out.data() + done, out.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return false;
        }
        if (n == 0) {
            ::close(fd);
            return false;
        }
        done += static_cast<size_t>(n);
    }
    ::close(fd);
    return true;
#endif
}

std::string base64UrlEncode(std::span<uint8_t const> bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2u) / 3u) * 4u);
    size_t i = 0;
    while (i + 3u <= bytes.size()) {
        uint32_t v = (uint32_t(bytes[i]) << 16) | (uint32_t(bytes[i + 1]) << 8) | uint32_t(bytes[i + 2]);
        out.push_back(kB64UrlAlphabet[(v >> 18) & 0x3Fu]);
        out.push_back(kB64UrlAlphabet[(v >> 12) & 0x3Fu]);
        out.push_back(kB64UrlAlphabet[(v >> 6) & 0x3Fu]);
        out.push_back(kB64UrlAlphabet[v & 0x3Fu]);
        i += 3u;
    }
    size_t rem = bytes.size() - i;
    if (rem == 1u) {
        uint32_t v = uint32_t(bytes[i]) << 16;
        out.push_back(kB64UrlAlphabet[(v >> 18) & 0x3Fu]);
        out.push_back(kB64UrlAlphabet[(v >> 12) & 0x3Fu]);
    } else if (rem == 2u) {
        uint32_t v = (uint32_t(bytes[i]) << 16) | (uint32_t(bytes[i + 1]) << 8);
        out.push_back(kB64UrlAlphabet[(v >> 18) & 0x3Fu]);
        out.push_back(kB64UrlAlphabet[(v >> 12) & 0x3Fu]);
        out.push_back(kB64UrlAlphabet[(v >> 6) & 0x3Fu]);
    }
    return out;
}

bool base64UrlDecode(std::string_view text, std::vector<uint8_t>& out) {
    out.clear();
    if (text.empty()) return true;
    size_t const rem = text.size() % 4u;
    if (rem == 1u) return false;

    out.reserve((text.size() / 4u) * 3u + (rem ? rem - 1u : 0u));
    size_t i = 0;
    while (i + 4u <= text.size()) {
        int a = b64UrlVal(text[i]);
        int b = b64UrlVal(text[i + 1]);
        int c = b64UrlVal(text[i + 2]);
        int d = b64UrlVal(text[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) {
            out.clear();
            return false;
        }
        uint32_t v = (uint32_t(a) << 18) | (uint32_t(b) << 12) | (uint32_t(c) << 6) | uint32_t(d);
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>(v & 0xFFu));
        i += 4u;
    }

    if (rem == 2u) {
        int a = b64UrlVal(text[i]);
        int b = b64UrlVal(text[i + 1]);
        if (a < 0 || b < 0 || (b & 0x0Fu) != 0) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<uint8_t>((a << 2) | (b >> 4)));
    } else if (rem == 3u) {
        int a = b64UrlVal(text[i]);
        int b = b64UrlVal(text[i + 1]);
        int c = b64UrlVal(text[i + 2]);
        if (a < 0 || b < 0 || c < 0 || (c & 0x03u) != 0) {
            out.clear();
            return false;
        }
        uint32_t v = (uint32_t(a) << 10) | (uint32_t(b) << 4) | (uint32_t(c) >> 2);
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>(v & 0xFFu));
    }
    return true;
}

bool chacha20Poly1305Encrypt(
    std::span<uint8_t const, kKeyBytes> key,
    std::span<uint8_t const, kNonceBytes> nonce,
    std::span<uint8_t const> aad,
    std::span<uint8_t const> plaintext,
    std::vector<uint8_t>& outCiphertext,
    std::array<uint8_t, kTagBytes>& outTag
) {
    if (plaintext.size() > 256u * 1024u * 1024u) return false;
    uint8_t otkBlock[64];
    chacha20Block(key.data(), 0u, nonce.data(), otkBlock);

    outCiphertext.resize(plaintext.size());
    chacha20Xor(key.data(), 1u, nonce.data(), plaintext.data(), outCiphertext.data(), plaintext.size());
    computeAeadTag(otkBlock, aad, outCiphertext, outTag.data());
    std::memset(otkBlock, 0, sizeof(otkBlock));
    return true;
}

bool chacha20Poly1305Decrypt(
    std::span<uint8_t const, kKeyBytes> key,
    std::span<uint8_t const, kNonceBytes> nonce,
    std::span<uint8_t const> aad,
    std::span<uint8_t const> ciphertext,
    std::span<uint8_t const, kTagBytes> tag,
    std::vector<uint8_t>& outPlaintext
) {
    outPlaintext.clear();
    if (ciphertext.size() > 256u * 1024u * 1024u) return false;
    uint8_t otkBlock[64];
    chacha20Block(key.data(), 0u, nonce.data(), otkBlock);

    std::array<uint8_t, kTagBytes> expected{};
    computeAeadTag(otkBlock, aad, ciphertext, expected.data());
    std::memset(otkBlock, 0, sizeof(otkBlock));

    if (!paimon::crypto::constantTimeEquals(expected, tag)) {
        return false;
    }

    outPlaintext.resize(ciphertext.size());
    chacha20Xor(key.data(), 1u, nonce.data(), ciphertext.data(), outPlaintext.data(), ciphertext.size());
    return true;
}

bool sealChatImage(
    std::span<uint8_t const> plaintext,
    std::string_view ext,
    int width,
    int height,
    int threadID,
    std::vector<uint8_t>& outBlob,
    std::string& outKey22
) {
    outBlob.clear();
    outKey22.clear();
    if (plaintext.size() < 12 || threadID <= 0 || !validExt(ext)
        || width < 1 || width > 1920 || height < 1 || height > 1920) {
        return false;
    }

    std::array<uint8_t, kSeedBytes> seed{};
    std::array<uint8_t, kNonceBytes> nonce{};
    if (!fillRandomBytes(seed) || !fillRandomBytes(nonce)) {
        return false;
    }

    std::array<uint8_t, kKeyBytes> derivedKey{};
    deriveThreadKey(seed, threadID, derivedKey);
    std::string aadStr = buildAad(ext, width, height, threadID);
    std::span<uint8_t const> aad(reinterpret_cast<uint8_t const*>(aadStr.data()), aadStr.size());

    std::vector<uint8_t> ciphertext;
    std::array<uint8_t, kTagBytes> tag{};
    bool ok = chacha20Poly1305Encrypt(derivedKey, nonce, aad, plaintext, ciphertext, tag);
    std::fill(derivedKey.begin(), derivedKey.end(), uint8_t{0});
    if (!ok) {
        std::fill(seed.begin(), seed.end(), uint8_t{0});
        return false;
    }

    outKey22 = base64UrlEncode(seed);
    std::fill(seed.begin(), seed.end(), uint8_t{0});
    if (outKey22.size() != kKeyTextLen) return false;

    outBlob.reserve(kMagic.size() + kNonceBytes + ciphertext.size() + kTagBytes);
    outBlob.insert(outBlob.end(), kMagic.begin(), kMagic.end());
    outBlob.insert(outBlob.end(), nonce.begin(), nonce.end());
    outBlob.insert(outBlob.end(), ciphertext.begin(), ciphertext.end());
    outBlob.insert(outBlob.end(), tag.begin(), tag.end());
    return true;
}

bool openChatImage(
    std::span<uint8_t const> blob,
    std::string_view ext,
    int width,
    int height,
    int threadID,
    std::string_view key22,
    std::vector<uint8_t>& outPlaintext
) {
    outPlaintext.clear();
    if (threadID <= 0 || !validExt(ext)
        || width < 1 || width > 1920 || height < 1 || height > 1920
        || key22.size() != kKeyTextLen
        || blob.size() < kEnvelopeOverhead + 12u) {
        return false;
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), blob.begin())) {
        return false;
    }

    std::vector<uint8_t> seedVec;
    if (!base64UrlDecode(key22, seedVec) || seedVec.size() != kSeedBytes) {
        return false;
    }
    std::array<uint8_t, kSeedBytes> seed{};
    std::memcpy(seed.data(), seedVec.data(), kSeedBytes);
    std::fill(seedVec.begin(), seedVec.end(), uint8_t{0});

    std::array<uint8_t, kKeyBytes> derivedKey{};
    deriveThreadKey(seed, threadID, derivedKey);
    std::fill(seed.begin(), seed.end(), uint8_t{0});

    std::span<uint8_t const, kNonceBytes> nonce(blob.data() + kMagic.size(), kNonceBytes);
    size_t const ctLen = blob.size() - kEnvelopeOverhead;
    std::span<uint8_t const> ciphertext(blob.data() + kMagic.size() + kNonceBytes, ctLen);
    std::span<uint8_t const, kTagBytes> tag(blob.data() + kMagic.size() + kNonceBytes + ctLen, kTagBytes);

    std::string aadStr = buildAad(ext, width, height, threadID);
    std::span<uint8_t const> aad(reinterpret_cast<uint8_t const*>(aadStr.data()), aadStr.size());

    bool ok = chacha20Poly1305Decrypt(derivedKey, nonce, aad, ciphertext, tag, outPlaintext);
    std::fill(derivedKey.begin(), derivedKey.end(), uint8_t{0});
    return ok;
}

} // namespace paimon::chatimg::crypto
