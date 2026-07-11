#include "FusionStore.hpp"
#include "SlotPaths.hpp"
#include "../engine/FusionAsset.hpp"
#include "../data/ImageBuffer.hpp"
#include "../../../utils/GIFDecoder.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

void writeU16LE(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void writeU32LE(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

void writeF32LE(std::vector<std::uint8_t>& out, float v) {
    static_assert(sizeof(float) == 4);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    writeU32LE(out, bits);
}

std::uint16_t readU16LE(std::uint8_t const* p) {
    return static_cast<std::uint16_t>(p[0])
         | (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t readU32LE(std::uint8_t const* p) {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

float readF32LE(std::uint8_t const* p) {
    std::uint32_t bits = readU32LE(p);
    float v = 0.f;
    std::memcpy(&v, &bits, 4);
    return v;
}

std::string lowerExt(std::filesystem::path const& path) {
    auto e = path.extension().string();
    for (char& c : e) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return e;
}

bool maskNonEmpty(MaskBuffer const& m) {
    if (m.empty()) return false;
    for (auto v : m.data) {
        if (v != 0) return true;
    }
    return false;
}

// Header layout (48 bytes):
//  0  magic u32
//  4  version u16
//  6  flags u16          bit0 = animated
//  8  width u32
// 12  height u32
// 16  blendMode u8
// 17  colorTolerance u8
// 18  opacity u8         (0..255)
// 19  fitMode u8
// 20  scale f32
// 24  offsetX f32
// 28  offsetY f32
// 32  rotationDeg f32
// 36  transformOpacity u8
// 37  flipX u8
// 38  flipY u8
// 39  reserved u8
// 40  extLen u16         (0 or 4 typical: ".png"/".gif")
// 42  reserved2 u16
// 44  reserved3 u32
// 48  ext bytes (extLen)
//     mask R8 data (W*H)
constexpr std::size_t kHeaderSize = 48;

}  // anonymous namespace

geode::Result<> FusionStore::save(std::filesystem::path const& path,
                                  FusionPayload const& payload) {
    if (!maskNonEmpty(payload.mask)) {
        return Err("FusionStore::save: empty mask for {}",
            geode::utils::string::pathToString(path));
    }
    int W = payload.mask.width;
    int H = payload.mask.height;
    if (W <= 0 || H <= 0 || W > 8192 || H > 8192) {
        return Err("FusionStore::save: invalid mask size {}x{}", W, H);
    }
    if (payload.mask.data.size()
        != static_cast<std::size_t>(W) * static_cast<std::size_t>(H)) {
        return Err("FusionStore::save: mask buffer size mismatch");
    }

    std::string ext = payload.textureExt.empty() ? ".png" : payload.textureExt;
    if (ext.size() > 16) ext.resize(16);

    std::uint16_t flags = 0;
    if (payload.animated) flags |= 1;

    std::vector<std::uint8_t> buf;
    buf.reserve(kHeaderSize + ext.size()
        + static_cast<std::size_t>(W) * static_cast<std::size_t>(H));

    writeU32LE(buf, kMagic);
    writeU16LE(buf, kVersion);
    writeU16LE(buf, flags);
    writeU32LE(buf, static_cast<std::uint32_t>(W));
    writeU32LE(buf, static_cast<std::uint32_t>(H));

    buf.push_back(static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(payload.blendMode), 0, 2)));
    buf.push_back(static_cast<std::uint8_t>(
        std::clamp(payload.colorTolerance, 0, 255)));
    buf.push_back(static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(std::lround(std::clamp(payload.opacity, 0.f, 1.f) * 255.f)),
        0, 255)));
    buf.push_back(static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(payload.transform.fitMode), 0, 2)));

    writeF32LE(buf, payload.transform.scale);
    writeF32LE(buf, payload.transform.offsetX);
    writeF32LE(buf, payload.transform.offsetY);
    writeF32LE(buf, payload.transform.rotationDeg);

    buf.push_back(static_cast<std::uint8_t>(
        std::clamp(payload.transform.opacity, 0, 255)));
    buf.push_back(payload.transform.flipX ? 1 : 0);
    buf.push_back(payload.transform.flipY ? 1 : 0);
    buf.push_back(0);  // reserved

    writeU16LE(buf, static_cast<std::uint16_t>(ext.size()));
    writeU16LE(buf, 0);
    writeU32LE(buf, 0);

    if (buf.size() != kHeaderSize) {
        return Err("FusionStore::save: internal header size {} != {}",
            buf.size(), kHeaderSize);
    }

    buf.insert(buf.end(), ext.begin(), ext.end());
    buf.insert(buf.end(), payload.mask.data.begin(), payload.mask.data.end());

    std::error_code ec;
    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return Err("create_directories {}: {}",
                geode::utils::string::pathToString(parent), ec.message());
        }
    }

    auto wr = file::writeBinary(path, buf);
    if (!wr) {
        return Err("writeBinary {}: {}",
            geode::utils::string::pathToString(path), wr.unwrapErr());
    }
    return Ok();
}

geode::Result<FusionPayload> FusionStore::load(std::filesystem::path const& path) {
    auto rd = file::readBinary(path);
    if (!rd) {
        return Err("readBinary {}: {}",
            geode::utils::string::pathToString(path), rd.unwrapErr());
    }
    auto const& bytes = rd.unwrap();
    if (bytes.size() < kHeaderSize) {
        return Err("fusion too small ({} bytes)", bytes.size());
    }
    if (readU32LE(bytes.data()) != kMagic) {
        return Err("bad magic in fusion file");
    }
    auto version = readU16LE(bytes.data() + 4);
    if (version != kVersion) {
        return Err("unsupported fusion version: {}", version);
    }

    std::uint16_t flags = readU16LE(bytes.data() + 6);
    int W = static_cast<int>(readU32LE(bytes.data() + 8));
    int H = static_cast<int>(readU32LE(bytes.data() + 12));
    if (W <= 0 || H <= 0 || W > 8192 || H > 8192) {
        return Err("fusion has invalid dims {}x{}", W, H);
    }

    FusionPayload out;
    out.animated = (flags & 1) != 0;
    out.blendMode = static_cast<FusionBlendMode>(
        std::clamp<int>(bytes[16], 0, 2));
    out.colorTolerance = bytes[17];
    out.opacity = bytes[18] / 255.f;
    out.transform.fitMode = static_cast<ImageFitMode>(
        std::clamp<int>(bytes[19], 0, 2));
    out.transform.scale = readF32LE(bytes.data() + 20);
    out.transform.offsetX = readF32LE(bytes.data() + 24);
    out.transform.offsetY = readF32LE(bytes.data() + 28);
    out.transform.rotationDeg = readF32LE(bytes.data() + 32);
    out.transform.opacity = bytes[36];
    out.transform.flipX = bytes[37] != 0;
    out.transform.flipY = bytes[38] != 0;

    std::uint16_t extLen = readU16LE(bytes.data() + 40);
    if (extLen > 16) {
        return Err("fusion extLen too large: {}", extLen);
    }
    std::size_t per = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    std::size_t expected = kHeaderSize + extLen + per;
    if (bytes.size() != expected) {
        return Err("fusion size mismatch: expected {} got {}",
            expected, bytes.size());
    }

    if (extLen > 0) {
        out.textureExt.assign(
            reinterpret_cast<char const*>(bytes.data() + kHeaderSize), extLen);
    } else {
        out.textureExt = ".png";
    }

    out.mask.width  = W;
    out.mask.height = H;
    out.mask.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize + extLen),
                         bytes.end());
    return Ok(std::move(out));
}

geode::Result<> FusionStore::saveForSlot(std::string_view slotId,
                                         std::string_view spriteName,
                                         FusionPayload const& payload) {
    if (auto r = SlotPaths::ensureSlotDirs(slotId); !r) return r;
    return save(SlotPaths::fusionMaskFile(slotId, spriteName), payload);
}

geode::Result<FusionPayload> FusionStore::loadForSlot(std::string_view slotId,
                                                      std::string_view spriteName) {
    return load(SlotPaths::fusionMaskFile(slotId, spriteName));
}

geode::Result<> FusionStore::deleteMaskForSlot(std::string_view slotId,
                                               std::string_view spriteName) {
    std::error_code ec;
    auto path = SlotPaths::fusionMaskFile(slotId, spriteName);
    if (!std::filesystem::exists(path, ec)) return Ok();
    std::filesystem::remove(path, ec);
    if (ec) {
        return Err("remove {}: {}",
            geode::utils::string::pathToString(path), ec.message());
    }
    return Ok();
}

geode::Result<> FusionStore::deleteForSlot(std::string_view slotId,
                                           std::string_view spriteName) {
    std::error_code ec;
    auto removeOne = [&](std::filesystem::path const& p) -> geode::Result<> {
        if (!std::filesystem::exists(p, ec)) return Ok();
        std::filesystem::remove(p, ec);
        if (ec) {
            return Err("remove {}: {}",
                geode::utils::string::pathToString(p), ec.message());
        }
        return Ok();
    };

    // Mask + any extension of the texture file.
    if (auto r = deleteMaskForSlot(slotId, spriteName); !r) return r;
    for (auto const* ext : {".png", ".gif", ".jpg", ".jpeg", ".webp"}) {
        auto p = SlotPaths::fusionTextureFile(slotId, spriteName, ext);
        if (auto r = removeOne(p); !r) return r;
    }
    return Ok();
}

geode::Result<std::filesystem::path> FusionStore::importTexture(
    std::string_view slotId,
    std::string_view spriteName,
    std::filesystem::path const& sourcePath) {
    if (auto r = SlotPaths::ensureSlotDirs(slotId); !r) {
        return Err(r.unwrapErr());
    }

    auto bytes = file::readBinary(sourcePath);
    if (!bytes) {
        return Err("cannot read {}: {}",
            geode::utils::string::pathToString(sourcePath), bytes.unwrapErr());
    }

    std::string ext = lowerExt(sourcePath);
    bool isGif = (ext == ".gif")
        || GIFDecoder::isGIF(bytes.unwrap().data(), bytes.unwrap().size());

    std::filesystem::path dst;
    if (isGif) {
        // Preserve original GIF bytes so multi-frame animation is not lost.
        dst = SlotPaths::fusionTextureFile(slotId, spriteName, ".gif");
        // Drop any stale static sibling.
        std::error_code ec;
        std::filesystem::remove(
            SlotPaths::fusionTextureFile(slotId, spriteName, ".png"), ec);
        auto wr = file::writeBinary(dst, bytes.unwrap());
        if (!wr) {
            return Err("write GIF {}: {}",
                geode::utils::string::pathToString(dst), wr.unwrapErr());
        }
        return Ok(std::move(dst));
    }

    // Re-encode static images as PNG for a stable on-disk format.
    auto imgRes = ImageBuffer::loadFromMemory(
        std::span<std::uint8_t const>(bytes.unwrap().data(), bytes.unwrap().size()));
    if (!imgRes) {
        return Err("decode image: {}", imgRes.unwrapErr());
    }
    dst = SlotPaths::fusionTextureFile(slotId, spriteName, ".png");
    std::error_code ec;
    std::filesystem::remove(
        SlotPaths::fusionTextureFile(slotId, spriteName, ".gif"), ec);
    if (auto wr = imgRes.unwrap().saveToPng(dst); !wr) {
        return Err(wr.unwrapErr());
    }
    return Ok(std::move(dst));
}

}  // namespace paimon::texture_studio
