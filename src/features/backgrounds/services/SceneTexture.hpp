#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "../../../utils/FormatDetect.hpp"

namespace paimon::wallpaper {

// one TEXS sprite-sheet cell, already expressed in pixels of the decoded level.
struct SceneTextureFrame {
    int image = 0;
    float duration = 0.f;
    float x = 0.f, y = 0.f, width = 0.f, widthY = 0.f, heightX = 0.f, height = 0.f;
};

enum class SceneTextureFormat : uint8_t { RGBA8, DXT1, DXT3, DXT5 };

struct SceneTexture {
    // pixel size of the stored level.
    int width = 0, height = 0;
    // image region inside the level (top-left anchored, pixels).
    int visibleWidth = 0, visibleHeight = 0;
    // size of the image at level 0: Wallpaper Engine sizes objects in these units.
    int sourceWidth = 0, sourceHeight = 0;
    // embedded png/jpeg payload; decode with stb before uploading.
    bool encoded = false;
    // raw S3TC blocks are only kept when the caller can upload them as-is.
    SceneTextureFormat format = SceneTextureFormat::RGBA8;
    // TEXI flag 1: authored for point sampling (pixel art).
    bool nearest = false;
    // TEXI pixel format of the container. Masks read R8 values from alpha.
    int sourceFormat = 0;
    // video texture: `pixels` holds the embedded MP4 container, not pixels.
    bool video = false;
    // the MP4 extracted to disk for the video decoder (set by the loader).
    std::filesystem::path videoFile;
    std::vector<uint8_t> pixels;
    std::vector<SceneTextureFrame> frames;
};

struct SceneTextureRequest {
    // smallest level whose image (or first sheet cell) still covers this many
    // pixels is chosen; zero keeps level 0.
    int targetWidth = 0;
    int targetHeight = 0;
    // keep DXT1/3/5 blocks compressed when the level is block aligned.
    bool keepCompressed = false;
    // GL_MAX_TEXTURE_SIZE; larger levels are skipped when smaller ones exist.
    int maxDimension = 0;
};

namespace scene_detail {
class Reader {
public:
    explicit Reader(std::span<uint8_t const> bytes) : m_bytes(bytes) {}

    bool take(size_t length, uint8_t const*& result) {
        if (length > m_bytes.size() - m_pos) return false;
        result = m_bytes.data() + m_pos;
        m_pos += length;
        return true;
    }
    bool skip(size_t length) { uint8_t const* ignored = nullptr; return take(length, ignored); }
    bool u32(uint32_t& result) {
        uint8_t const* ptr = nullptr;
        if (!take(4, ptr)) return false;
        result = uint32_t(ptr[0]) | (uint32_t(ptr[1]) << 8) |
            (uint32_t(ptr[2]) << 16) | (uint32_t(ptr[3]) << 24);
        return true;
    }
    bool i32(int32_t& result) {
        uint32_t value = 0;
        if (!u32(value)) return false;
        result = static_cast<int32_t>(value);
        return true;
    }
    bool f32(float& result) {
        uint32_t bits = 0;
        if (!u32(bits)) return false;
        std::memcpy(&result, &bits, sizeof(result));
        return std::isfinite(result);
    }
    bool cstring(std::string_view& out, size_t maxLength = 16) {
        auto const available = std::min(maxLength + 1, m_bytes.size() - m_pos);
        for (size_t i = 0; i < available; ++i) {
            if (m_bytes[m_pos + i] == 0) {
                out = {reinterpret_cast<char const*>(m_bytes.data() + m_pos), i};
                m_pos += i + 1;
                return true;
            }
        }
        return false;
    }
private:
    std::span<uint8_t const> m_bytes;
    size_t m_pos = 0;
};

// LZ4 block decoder writing straight into a pre-sized buffer. Literals and
// matches are copied with memcpy; overlapping matches repeat in offset-sized
// chunks, which never overlap within a single copy.
inline bool decompressLz4(uint8_t const* src, size_t srcLength, uint8_t* dst, size_t dstLength) {
    size_t ip = 0, op = 0;
    while (ip < srcLength) {
        unsigned const token = src[ip++];
        size_t literal = token >> 4;
        if (literal == 15) {
            unsigned extra = 0;
            do {
                if (ip >= srcLength || literal > dstLength) return false;
                extra = src[ip++];
                literal += extra;
            } while (extra == 255);
        }
        if (literal > srcLength - ip || literal > dstLength - op) return false;
        std::memcpy(dst + op, src + ip, literal);
        ip += literal;
        op += literal;
        if (ip >= srcLength) break; // the last sequence carries literals only
        if (srcLength - ip < 2) return false;
        size_t const offset = size_t(src[ip]) | (size_t(src[ip + 1]) << 8);
        ip += 2;
        if (offset == 0 || offset > op) return false;
        size_t match = token & 15;
        if (match == 15) {
            unsigned extra = 0;
            do {
                if (ip >= srcLength || match > dstLength) return false;
                extra = src[ip++];
                match += extra;
            } while (extra == 255);
        }
        match += 4;
        if (match > dstLength - op) return false;
        uint8_t* out = dst + op;
        if (offset >= match) {
            std::memcpy(out, out - offset, match);
        } else if (offset == 1) {
            std::memset(out, out[-1], match);
        } else {
            for (size_t done = 0; done < match;) {
                size_t const chunk = std::min(offset, match - done);
                std::memcpy(out + done, out + done - offset, chunk);
                done += chunk;
            }
        }
        op += match;
    }
    return op == dstLength;
}

inline bool decompressLz4(uint8_t const* input, size_t length, std::vector<uint8_t>& output, size_t expected) {
    if (expected > 256ull * 1024 * 1024) return false;
    output.resize(expected);
    if (!decompressLz4(input, length, output.data(), expected)) {
        output.clear();
        return false;
    }
    return true;
}

inline std::array<uint8_t, 3> rgb565(uint16_t c) {
    uint8_t r = static_cast<uint8_t>((c >> 11) & 31);
    uint8_t g = static_cast<uint8_t>((c >> 5) & 63);
    uint8_t b = static_cast<uint8_t>(c & 31);
    return {static_cast<uint8_t>((r << 3) | (r >> 2)),
        static_cast<uint8_t>((g << 2) | (g >> 4)),
        static_cast<uint8_t>((b << 3) | (b >> 2))};
}

inline size_t blockBytes(int format, int w, int h) {
    size_t const blocks = size_t((w + 3) / 4) * size_t((h + 3) / 4);
    return blocks * (format == 7 ? 8u : 16u);
}

inline bool decodePixels(int format, int w, int h, std::span<uint8_t const> input,
    std::vector<uint8_t>& rgba) {
    if (w < 1 || h < 1 || w > 8192 || h > 8192 || uint64_t(w) * h > 32ull * 1024 * 1024) return false;
    size_t const count = size_t(w) * h;
    size_t const blocksX = (w + 3) / 4, blocksY = (h + 3) / 4;

    rgba.resize(count * 4);

    // format 0: RGBA8888
    if (format == 0) {
        if (input.size() < count * 4) return false;
        std::copy_n(input.data(), count * 4, rgba.data());
        return true;
    }

    // format 1: ARGB8888
    if (format == 1) {
        if (input.size() < count * 4) return false;
        for (size_t i = 0; i < count; ++i) {
            rgba[i * 4 + 0] = input[i * 4 + 1];
            rgba[i * 4 + 1] = input[i * 4 + 2];
            rgba[i * 4 + 2] = input[i * 4 + 3];
            rgba[i * 4 + 3] = input[i * 4 + 0];
        }
        return true;
    }

    auto expandRgb = [&] {
        for (size_t i = 0; i < count; ++i) {
            rgba[i * 4 + 0] = input[i * 3 + 0];
            rgba[i * 4 + 1] = input[i * 3 + 1];
            rgba[i * 4 + 2] = input[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
    };

    // format 2: RGB888
    if (format == 2) {
        if (input.size() < count * 3) return false;
        expandRgb();
        return true;
    }

    // format 5: multi-variant RGB
    if (format == 5) {
        if (input.size() >= count * 4) {
            std::copy_n(input.data(), count * 4, rgba.data());
            return true;
        }
        if (input.size() >= count * 3) {
            expandRgb();
            return true;
        }
        if (input.size() >= count * 2) {
            for (size_t i = 0; i < count; ++i) {
                uint16_t const c = uint16_t(input[i * 2]) | (uint16_t(input[i * 2 + 1]) << 8);
                auto rgb = rgb565(c);
                rgba[i * 4 + 0] = rgb[0];
                rgba[i * 4 + 1] = rgb[1];
                rgba[i * 4 + 2] = rgb[2];
                rgba[i * 4 + 3] = 255;
            }
            return true;
        }
    }

    // format 8 (RG88) & Format 9 (R8). Wallpaper Engine shaders expand them in
    // ConvertTexture0Format: RG88 reads as (r, r, r, g) and R8 as white with
    // alpha = r. Particle debris and fog are R8 alpha masks.
    if (format == 8 || format == 9) {
        size_t const need = format == 9 ? count : count * 2;
        if (input.size() < need) return false;
        for (size_t i = 0; i < count; ++i) {
            uint8_t* out = rgba.data() + i * 4;
            if (format == 9) {
                out[0] = out[1] = out[2] = 255;
                out[3] = input[i];
            } else {
                out[0] = out[1] = out[2] = input[i * 2];
                out[3] = input[i * 2 + 1];
            }
        }
        return true;
    }

    // block compression: Format 7 (DXT1), Format 6 (DXT3), Format 4 (DXT5)
    if (format == 7 || format == 4 || format == 6) {
        size_t const blockSize = (format == 7 ? 8 : 16);
        if (input.size() < blockBytes(format, w, h)) return false;

        size_t at = 0;
        for (size_t by = 0; by < blocksY; ++by) {
            for (size_t bx = 0; bx < blocksX; ++bx) {
                auto const* block = input.data() + at;
                auto const* color = block + (format == 7 ? 0 : 8);
                uint16_t c0 = uint16_t(color[0]) | (uint16_t(color[1]) << 8);
                uint16_t c1 = uint16_t(color[2]) | (uint16_t(color[3]) << 8);
                std::array<std::array<uint8_t, 4>, 4> palette{};
                auto a = rgb565(c0), b = rgb565(c1);
                bool const threeColor = format == 7 && c0 <= c1;
                for (int k = 0; k < 3; ++k) {
                    palette[0][k] = a[k];
                    palette[1][k] = b[k];
                    palette[2][k] = static_cast<uint8_t>(threeColor ? (int(a[k]) + b[k]) / 2 : (2 * int(a[k]) + b[k]) / 3);
                    palette[3][k] = static_cast<uint8_t>(threeColor ? 0 : (int(a[k]) + 2 * b[k]) / 3);
                }
                palette[0][3] = palette[1][3] = palette[2][3] = 255;
                palette[3][3] = threeColor ? 0 : 255;
                uint32_t const indices = uint32_t(color[4]) | (uint32_t(color[5]) << 8) |
                    (uint32_t(color[6]) << 16) | (uint32_t(color[7]) << 24);
                std::array<uint8_t, 8> alpha{};
                uint64_t alphaIndex = 0;
                if (format == 4) {
                    alpha[0] = block[0]; alpha[1] = block[1];
                    if (alpha[0] > alpha[1]) {
                        for (int i = 1; i <= 6; ++i) alpha[i + 1] = static_cast<uint8_t>(((7 - i) * alpha[0] + i * alpha[1]) / 7);
                    } else {
                        for (int i = 1; i <= 4; ++i) alpha[i + 1] = static_cast<uint8_t>(((5 - i) * alpha[0] + i * alpha[1]) / 5);
                        alpha[6] = 0; alpha[7] = 255;
                    }
                    for (int i = 0; i < 6; ++i) alphaIndex |= uint64_t(block[i + 2]) << (i * 8);
                }
                for (int py = 0; py < 4; ++py) {
                    int const y = int(by) * 4 + py;
                    if (y >= h) break;
                    for (int px = 0; px < 4; ++px) {
                        int const x = int(bx) * 4 + px;
                        if (x >= w) break;
                        int const idx = py * 4 + px;
                        auto const& pixel = palette[(indices >> (idx * 2)) & 3];
                        auto* dest = rgba.data() + (size_t(y) * w + x) * 4;
                        std::memcpy(dest, pixel.data(), 4);
                        if (format == 6) dest[3] = static_cast<uint8_t>(((block[idx / 2] >> ((idx % 2) * 4)) & 15) * 17);
                        if (format == 4) dest[3] = alpha[(alphaIndex >> (idx * 3)) & 7];
                    }
                }
                at += blockSize;
            }
        }
        return true;
    }

    return false;
}

struct MipEntry {
    int width = 0, height = 0;
    int compressed = 0;
    int rawSize = 0;
    uint8_t const* data = nullptr;
    size_t bytes = 0;
};
} // namespace scene_detail

// parses a Wallpaper Engine TEXV container. Only the requested mip level is
// decompressed; DXT payloads stay compressed when the caller can upload them.
inline std::optional<SceneTexture> decodeSceneTexture(std::span<uint8_t const> bytes,
    SceneTextureRequest const& request = {}) {
    using namespace scene_detail;
    Reader r(bytes);
    std::string_view version, info, container;
    int32_t format = 0, flags = 0, texW = 0, texH = 0, imgW = 0, imgH = 0, unused = 0;
    if (!r.cstring(version) || !version.starts_with("TEXV") || !r.cstring(info) || info != "TEXI0001" ||
        !r.i32(format) || !r.i32(flags) || !r.i32(texW) || !r.i32(texH) ||
        !r.i32(imgW) || !r.i32(imgH) || !r.i32(unused) || !r.cstring(container) ||
        !container.starts_with("TEXB") || container.size() != 8) return std::nullopt;
    int const containerVersion = container[7] - '0';
    if (containerVersion < 1 || containerVersion > 8 || texW < 1 || texH < 1 ||
        texW > 16384 || texH > 16384) return std::nullopt;
    int32_t imageCount = 0, freeImageFormat = -1;
    if (!r.i32(imageCount) || imageCount < 1 || imageCount > 100 ||
        (containerVersion >= 3 && !r.i32(freeImageFormat))) return std::nullopt;
    int effectiveVersion = containerVersion;
    bool mp4Container = false;
    if (containerVersion >= 4) {
        int32_t isMp4 = 0;
        if (!r.i32(isMp4)) return std::nullopt;
        mp4Container = isMp4 != 0;
        effectiveVersion = 3;
    }

    std::vector<MipEntry> mips;
    for (int i = 0; i < imageCount; ++i) {
        int32_t mipCount = 0;
        if (!r.i32(mipCount) || mipCount < 1 || mipCount > 32) return std::nullopt;
        for (int j = 0; j < mipCount; ++j) {
            int32_t w = 0, h = 0, compressed = 0, rawSize = 0, byteCount = 0;
            if (!r.i32(w) || !r.i32(h) ||
                (effectiveVersion >= 2 && (!r.i32(compressed) || !r.i32(rawSize))) ||
                !r.i32(byteCount) || byteCount < 0 || byteCount > 128 * 1024 * 1024) return std::nullopt;
            uint8_t const* data = nullptr;
            if (!r.take(static_cast<size_t>(byteCount), data)) return std::nullopt;
            if (i != 0) continue;
            if (w < 1 || h < 1 || w > 16384 || h > 16384 || uint64_t(w) * h > 64ull * 1024 * 1024) {
                if (j == 0) return std::nullopt;
                continue;
            }
            mips.push_back({w, h, compressed, rawSize, data, static_cast<size_t>(byteCount)});
        }
    }
    if (mips.empty()) return std::nullopt;

    // level 0 is either padded to the texture size (the image sits in its
    // top-left corner) or stores the image itself.
    auto const& base = mips.front();
    bool const padded = base.width == texW && base.height == texH;
    int const imageW = imgW > 0 ? imgW : texW;
    int const imageH = imgH > 0 ? imgH : texH;
    float const baseFrameScaleX = padded ? 1.f : static_cast<float>(base.width) / std::max(1, imageW);
    float const baseFrameScaleY = padded ? 1.f : static_cast<float>(base.height) / std::max(1, imageH);
    int const baseVisibleW = padded ? std::clamp(imageW, 1, base.width) : base.width;
    int const baseVisibleH = padded ? std::clamp(imageH, 1, base.height) : base.height;

    SceneTexture result;
    result.nearest = (flags & 1) != 0;
    result.sourceFormat = format;
    result.sourceWidth = padded ? imageW : base.width;
    result.sourceHeight = padded ? imageH : base.height;

    std::vector<SceneTextureFrame> frames;
    if (flags & 4) {
        std::string_view gifVersion;
        int32_t count = 0;
        if (!r.cstring(gifVersion) || !gifVersion.starts_with("TEXS000") ||
            !r.i32(count) || count < 0 || count > 100000) return std::nullopt;
        if (gifVersion != "TEXS0001" && gifVersion != "TEXS0002" && gifVersion != "TEXS0003") return std::nullopt;
        if (gifVersion == "TEXS0003" && !r.skip(8)) return std::nullopt;
        frames.reserve(std::min(count, 4096));
        for (int i = 0; i < count; ++i) {
            int32_t imageId = 0;
            SceneTextureFrame frame;
            if (!r.i32(imageId) || !r.f32(frame.duration)) return std::nullopt;
            frame.image = imageId;
            if (gifVersion == "TEXS0001") {
                int32_t coords[6]{};
                for (auto& c : coords) if (!r.i32(c)) return std::nullopt;
                frame.x = float(coords[0]); frame.y = float(coords[1]);
                frame.width = float(coords[2]); frame.widthY = float(coords[3]);
                frame.heightX = float(coords[4]); frame.height = float(coords[5]);
            } else if (!r.f32(frame.x) || !r.f32(frame.y) || !r.f32(frame.width) ||
                !r.f32(frame.widthY) || !r.f32(frame.heightX) || !r.f32(frame.height)) return std::nullopt;
            if (frame.duration <= 0.001f) frame.duration = 0.033f;
            if (imageId == 0 && frames.size() < 4096) frames.push_back(frame);
        }
    }

    // size of what one level displays: the first sheet cell, or the image.
    auto cellAt = [&](MipEntry const& mip) {
        float const levelScaleX = static_cast<float>(mip.width) / base.width;
        float const levelScaleY = static_cast<float>(mip.height) / base.height;
        if (!frames.empty()) {
            auto const& f = frames.front();
            float const fw = std::abs(f.width != 0.f ? f.width : f.heightX);
            float const fh = std::abs(f.height != 0.f ? f.height : f.widthY);
            if (fw > 1.0001f && fh > 1.0001f) {
                return std::pair{fw * baseFrameScaleX * levelScaleX, fh * baseFrameScaleY * levelScaleY};
            }
        }
        return std::pair{baseVisibleW * levelScaleX, baseVisibleH * levelScaleY};
    };

    // never pick a level the gpu cannot hold.
    size_t chosen = 0;
    if (request.maxDimension > 0) {
        while (chosen + 1 < mips.size() &&
            (mips[chosen].width > request.maxDimension || mips[chosen].height > request.maxDimension)) {
            ++chosen;
        }
    }
    if (request.targetWidth > 0 && request.targetHeight > 0) {
        for (size_t i = chosen + 1; i < mips.size(); ++i) {
            auto const [cw, ch] = cellAt(mips[i]);
            if (cw + 0.5f < request.targetWidth || ch + 0.5f < request.targetHeight) break;
            chosen = i;
        }
    }

    auto const& mip = mips[chosen];
    float const levelScaleX = static_cast<float>(mip.width) / base.width;
    float const levelScaleY = static_cast<float>(mip.height) / base.height;

    std::vector<uint8_t> unpacked;
    std::span<uint8_t const> payload(mip.data, mip.bytes);
    if (mip.compressed == 1) {
        if (mip.rawSize < 0 || !decompressLz4(mip.data, mip.bytes, unpacked, static_cast<size_t>(mip.rawSize))) {
            return std::nullopt;
        }
        payload = unpacked;
    } else if (mip.compressed != 0) {
        return std::nullopt;
    }

    result.width = mip.width;
    result.height = mip.height;
    result.visibleWidth = std::clamp(static_cast<int>(std::lround(baseVisibleW * levelScaleX)), 1, mip.width);
    result.visibleHeight = std::clamp(static_cast<int>(std::lround(baseVisibleH * levelScaleY)), 1, mip.height);

    // video textures embed a whole MP4 file in the level. TEXB0004 flags it,
    // older containers only carry the `ftyp` box; read as pixels it is noise.
    bool const mp4Payload = paimon::format::isMp4(payload.data(), payload.size());
    if (mp4Container || mp4Payload) {
        if (!mp4Payload) return std::nullopt;
        result.video = true;
        result.visibleWidth = mip.width;
        result.visibleHeight = mip.height;
        result.sourceWidth = imageW;
        result.sourceHeight = imageH;
        result.pixels.assign(payload.begin(), payload.end());
        return result;
    }

    bool const isPng = paimon::format::isPng(payload.data(), payload.size());
    bool const isJpg = paimon::format::isJpeg(payload.data(), payload.size());
    bool const isBmp = paimon::format::detect(payload.data(), payload.size()) == paimon::format::ImageFormat::BMP;
    result.encoded = (freeImageFormat != -1) || isPng || isJpg || isBmp;

    bool const blockFormat = format == 4 || format == 6 || format == 7;
    if (result.encoded) {
        result.pixels.assign(payload.begin(), payload.end());
    } else if (blockFormat && request.keepCompressed && mip.width % 4 == 0 && mip.height % 4 == 0 &&
        payload.size() >= blockBytes(format, mip.width, mip.height)) {
        result.format = format == 7 ? SceneTextureFormat::DXT1
            : format == 6 ? SceneTextureFormat::DXT3 : SceneTextureFormat::DXT5;
        result.pixels.assign(payload.begin(), payload.begin() + blockBytes(format, mip.width, mip.height));
    } else if (!decodePixels(format, mip.width, mip.height, payload, result.pixels)) {
        return std::nullopt;
    }

    result.frames.reserve(frames.size());
    for (auto frame : frames) {
        float const sx = baseFrameScaleX * levelScaleX;
        float const sy = baseFrameScaleY * levelScaleY;
        frame.x *= sx; frame.width *= sx; frame.heightX *= sx;
        frame.y *= sy; frame.height *= sy; frame.widthY *= sy;
        result.frames.push_back(frame);
    }
    if (result.pixels.empty()) return std::nullopt;
    return result;
}

inline std::optional<SceneTexture> decodeSceneTexture(std::vector<uint8_t> const& bytes,
    SceneTextureRequest const& request = {}) {
    return decodeSceneTexture(std::span<uint8_t const>(bytes.data(), bytes.size()), request);
}

} // namespace paimon::wallpaper
