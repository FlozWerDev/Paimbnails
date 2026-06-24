#include "ManualOverrideStore.hpp"

#include "SlotPaths.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <cstring>
#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

// Pack a 16-bit value as little-endian.
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

bool maskNonEmpty(MaskBuffer const& m) {
    if (m.empty()) return false;
    // Treat all-zero as empty. Saves disk space when one role is unused.
    for (auto v : m.data) {
        if (v != 0) return true;
    }
    return false;
}

void appendMask(std::vector<std::uint8_t>& out, MaskBuffer const& m) {
    out.insert(out.end(), m.data.begin(), m.data.end());
}

MaskBuffer makeBlankMask(int W, int H) {
    MaskBuffer m;
    m.width  = W;
    m.height = H;
    m.data.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0);
    return m;
}

}  // anonymous namespace

geode::Result<> ManualOverrideStore::save(std::filesystem::path const& path,
                                          MaskSet const& masks) {
    // Determine which masks are present.
    bool hasC1   = maskNonEmpty(masks.color1);
    bool hasC2   = maskNonEmpty(masks.color2);
    bool hasGlow = maskNonEmpty(masks.glow);
    bool hasOL   = maskNonEmpty(masks.outline);

    if (!(hasC1 || hasC2 || hasGlow || hasOL)) {
        return Err("ManualOverrideStore::save: all masks empty for {}", geode::utils::string::pathToString(path));
    }

    // Determine canonical W/H. We require all non-empty masks to share
    // dimensions; the writer normalises to the first non-empty mask.
    int W = 0, H = 0;
    auto pick = [&](MaskBuffer const& m) {
        if (W == 0 && !m.empty()) { W = m.width; H = m.height; }
    };
    pick(masks.color1);
    pick(masks.color2);
    pick(masks.glow);
    pick(masks.outline);

    auto check = [&](MaskBuffer const& m, char const* role) -> geode::Result<> {
        if (m.empty()) return Ok();
        if (m.width != W || m.height != H) {
            return Err("mask '{}' size {}x{} mismatches expected {}x{}",
                role, m.width, m.height, W, H);
        }
        return Ok();
    };
    if (auto r = check(masks.color1,  "C1");      !r) return r;
    if (auto r = check(masks.color2,  "C2");      !r) return r;
    if (auto r = check(masks.glow,    "Glow");    !r) return r;
    if (auto r = check(masks.outline, "Outline"); !r) return r;

    std::uint16_t flags = 0;
    if (hasC1)   flags |= kFlagHasC1;
    if (hasC2)   flags |= kFlagHasC2;
    if (hasGlow) flags |= kFlagHasGlow;
    if (hasOL)   flags |= kFlagHasOutline;

    std::vector<std::uint8_t> buf;
    buf.reserve(16 + static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4);

    writeU32LE(buf, kMagic);
    writeU16LE(buf, kVersion);
    writeU16LE(buf, flags);
    writeU32LE(buf, static_cast<std::uint32_t>(W));
    writeU32LE(buf, static_cast<std::uint32_t>(H));

    if (hasC1)   appendMask(buf, masks.color1);
    if (hasC2)   appendMask(buf, masks.color2);
    if (hasGlow) appendMask(buf, masks.glow);
    if (hasOL)   appendMask(buf, masks.outline);

    // Ensure parent dir exists.
    std::error_code ec;
    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return Err("create_directories {}: {}", geode::utils::string::pathToString(parent), ec.message());
        }
    }

    auto wr = file::writeBinary(path, buf);
    if (!wr) {
        return Err("writeBinary {}: {}", geode::utils::string::pathToString(path), wr.unwrapErr());
    }
    return Ok();
}

geode::Result<MaskSet> ManualOverrideStore::load(std::filesystem::path const& path) {
    auto rd = file::readBinary(path);
    if (!rd) {
        return Err("readBinary {}: {}", geode::utils::string::pathToString(path), rd.unwrapErr());
    }
    auto const& bytes = rd.unwrap();
    if (bytes.size() < 16) {
        return Err("override too small ({} bytes)", bytes.size());
    }

    if (readU32LE(bytes.data()) != kMagic) {
        return Err("bad magic in override file");
    }
    auto version = readU16LE(bytes.data() + 4);
    if (version != kVersion) {
        return Err("unsupported override version: {}", version);
    }
    std::uint16_t flags = readU16LE(bytes.data() + 6);
    int W = static_cast<int>(readU32LE(bytes.data() + 8));
    int H = static_cast<int>(readU32LE(bytes.data() + 12));
    if (W <= 0 || H <= 0 || W > 8192 || H > 8192) {
        return Err("override has invalid dims {}x{}", W, H);
    }

    std::size_t per = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    int presentCount = 0;
    for (auto bit : {kFlagHasC1, kFlagHasC2, kFlagHasGlow, kFlagHasOutline}) {
        if (flags & bit) ++presentCount;
    }
    std::size_t expected = 16 + per * static_cast<std::size_t>(presentCount);
    if (bytes.size() != expected) {
        return Err("override size mismatch: expected {} bytes, got {}",
            expected, bytes.size());
    }

    MaskSet out;
    out.color1  = makeBlankMask(W, H);
    out.color2  = makeBlankMask(W, H);
    out.glow    = makeBlankMask(W, H);
    out.outline = makeBlankMask(W, H);

    std::size_t off = 16;
    auto readBlock = [&](MaskBuffer& m) {
        std::memcpy(m.data.data(), bytes.data() + off, per);
        off += per;
    };
    if (flags & kFlagHasC1)      readBlock(out.color1);
    if (flags & kFlagHasC2)      readBlock(out.color2);
    if (flags & kFlagHasGlow)    readBlock(out.glow);
    if (flags & kFlagHasOutline) readBlock(out.outline);

    return Ok(std::move(out));
}

geode::Result<> ManualOverrideStore::saveForSlot(std::string_view slotId,
                                                 std::string_view spriteName,
                                                 MaskSet const& masks) {
    if (auto r = SlotPaths::ensureSlotDirs(slotId); !r) return r;
    return save(SlotPaths::overrideFile(slotId, spriteName), masks);
}

geode::Result<MaskSet> ManualOverrideStore::loadForSlot(std::string_view slotId,
                                                        std::string_view spriteName) {
    return load(SlotPaths::overrideFile(slotId, spriteName));
}

geode::Result<> ManualOverrideStore::deleteForSlot(std::string_view slotId,
                                                   std::string_view spriteName) {
    auto path = SlotPaths::overrideFile(slotId, spriteName);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        // Idempotent: not-existing is fine.
        return Ok();
    }
    std::filesystem::remove(path, ec);
    if (ec) {
        return Err("remove {}: {}", geode::utils::string::pathToString(path), ec.message());
    }
    return Ok();
}

}  // namespace paimon::texture_studio
