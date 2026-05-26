#pragma once

#include "PaiDrawModels.hpp"
#include <array>
#include <span>

namespace paidraw::codec {

inline void pushU16(geode::ByteVector& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

inline void pushU32(geode::ByteVector& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

inline void pushU64(geode::ByteVector& out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

inline uint16_t readU16(std::span<uint8_t const> bytes, size_t& offset) {
    auto value = static_cast<uint16_t>(bytes[offset] << 8 | bytes[offset + 1]);
    offset += 2;
    return value;
}

inline uint32_t readU32(std::span<uint8_t const> bytes, size_t& offset) {
    auto value =
        (static_cast<uint32_t>(bytes[offset]) << 24) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
        static_cast<uint32_t>(bytes[offset + 3]);
    offset += 4;
    return value;
}

inline uint64_t readU64(std::span<uint8_t const> bytes, size_t& offset) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<uint64_t>(bytes[offset + i]);
    }
    offset += 8;
    return value;
}

inline geode::ByteVector encodeEnvelope(PaiDrawPacket const& packet) {
    geode::ByteVector out;
    out.reserve(1 + 4 + 4 + 8 + 4 + packet.payload.size());
    out.push_back(static_cast<uint8_t>(packet.type));
    pushU32(out, packet.roomId);
    pushU32(out, packet.senderId);
    pushU64(out, packet.timestamp);
    pushU32(out, static_cast<uint32_t>(packet.payload.size()));
    out.insert(out.end(), packet.payload.begin(), packet.payload.end());
    return out;
}

inline geode::Result<PaiDrawPacket> decodeEnvelope(std::span<uint8_t const> bytes) {
    if (bytes.size() < 21) {
        return geode::Err("PaiDraw envelope too small");
    }

    size_t offset = 0;
    PaiDrawPacket packet;
    packet.type = static_cast<PacketType>(bytes[offset++]);
    packet.roomId = readU32(bytes, offset);
    packet.senderId = readU32(bytes, offset);
    packet.timestamp = readU64(bytes, offset);
    auto payloadSize = readU32(bytes, offset);

    if (offset + payloadSize > bytes.size()) {
        return geode::Err("PaiDraw envelope payload overflow");
    }

    packet.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + payloadSize));
    return geode::Ok(std::move(packet));
}

class MsgPackWriter {
public:
    void nil() {
        m_bytes.push_back(0xC0);
    }

    void boolean(bool value) {
        m_bytes.push_back(value ? 0xC3 : 0xC2);
    }

    void integer(int64_t value) {
        if (value >= 0 && value <= 127) {
            m_bytes.push_back(static_cast<uint8_t>(value));
            return;
        }

        if (value >= -32 && value < 0) {
            m_bytes.push_back(static_cast<uint8_t>(value));
            return;
        }

        if (value >= INT8_MIN && value <= INT8_MAX) {
            m_bytes.push_back(0xD0);
            m_bytes.push_back(static_cast<uint8_t>(static_cast<int8_t>(value)));
            return;
        }

        if (value >= INT16_MIN && value <= INT16_MAX) {
            m_bytes.push_back(0xD1);
            pushU16(m_bytes, static_cast<uint16_t>(static_cast<int16_t>(value)));
            return;
        }

        if (value >= INT32_MIN && value <= INT32_MAX) {
            m_bytes.push_back(0xD2);
            pushU32(m_bytes, static_cast<uint32_t>(static_cast<int32_t>(value)));
            return;
        }

        m_bytes.push_back(0xD3);
        pushU64(m_bytes, static_cast<uint64_t>(value));
    }

    void uinteger(uint64_t value) {
        if (value <= 127) {
            m_bytes.push_back(static_cast<uint8_t>(value));
            return;
        }

        if (value <= UINT8_MAX) {
            m_bytes.push_back(0xCC);
            m_bytes.push_back(static_cast<uint8_t>(value));
            return;
        }

        if (value <= UINT16_MAX) {
            m_bytes.push_back(0xCD);
            pushU16(m_bytes, static_cast<uint16_t>(value));
            return;
        }

        if (value <= UINT32_MAX) {
            m_bytes.push_back(0xCE);
            pushU32(m_bytes, static_cast<uint32_t>(value));
            return;
        }

        m_bytes.push_back(0xCF);
        pushU64(m_bytes, value);
    }

    void floating(double value) {
        m_bytes.push_back(0xCB);
        std::array<uint8_t, sizeof(double)> raw {};
        std::memcpy(raw.data(), &value, sizeof(double));
        for (int i = static_cast<int>(raw.size()) - 1; i >= 0; --i) {
            m_bytes.push_back(raw[static_cast<size_t>(i)]);
        }
    }

    void string(std::string const& value) {
        auto size = value.size();
        if (size <= 31) {
            m_bytes.push_back(static_cast<uint8_t>(0xA0 | size));
        }
        else if (size <= UINT8_MAX) {
            m_bytes.push_back(0xD9);
            m_bytes.push_back(static_cast<uint8_t>(size));
        }
        else if (size <= UINT16_MAX) {
            m_bytes.push_back(0xDA);
            pushU16(m_bytes, static_cast<uint16_t>(size));
        }
        else {
            m_bytes.push_back(0xDB);
            pushU32(m_bytes, static_cast<uint32_t>(size));
        }
        m_bytes.insert(m_bytes.end(), value.begin(), value.end());
    }

    void binary(geode::ByteVector const& bytes) {
        auto size = bytes.size();
        if (size <= UINT8_MAX) {
            m_bytes.push_back(0xC4);
            m_bytes.push_back(static_cast<uint8_t>(size));
        }
        else if (size <= UINT16_MAX) {
            m_bytes.push_back(0xC5);
            pushU16(m_bytes, static_cast<uint16_t>(size));
        }
        else {
            m_bytes.push_back(0xC6);
            pushU32(m_bytes, static_cast<uint32_t>(size));
        }
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }

    void array(size_t size) {
        if (size <= 15) {
            m_bytes.push_back(static_cast<uint8_t>(0x90 | size));
        }
        else if (size <= UINT16_MAX) {
            m_bytes.push_back(0xDC);
            pushU16(m_bytes, static_cast<uint16_t>(size));
        }
        else {
            m_bytes.push_back(0xDD);
            pushU32(m_bytes, static_cast<uint32_t>(size));
        }
    }

    void map(size_t size) {
        if (size <= 15) {
            m_bytes.push_back(static_cast<uint8_t>(0x80 | size));
        }
        else if (size <= UINT16_MAX) {
            m_bytes.push_back(0xDE);
            pushU16(m_bytes, static_cast<uint16_t>(size));
        }
        else {
            m_bytes.push_back(0xDF);
            pushU32(m_bytes, static_cast<uint32_t>(size));
        }
    }

    void key(std::string const& value) {
        string(value);
    }

    geode::ByteVector const& bytes() const {
        return m_bytes;
    }

private:
    geode::ByteVector m_bytes;
};

class MsgPackReader {
public:
    explicit MsgPackReader(std::span<uint8_t const> bytes) : m_bytes(bytes) {}

    geode::Result<matjson::Value> readValue() {
        if (m_offset >= m_bytes.size()) {
            return geode::Err("PaiDraw msgpack EOF");
        }

        uint8_t prefix = m_bytes[m_offset++];
        if ((prefix & 0x80) == 0x00) {
            return geode::Ok(matjson::Value(static_cast<int64_t>(prefix)));
        }
        if ((prefix & 0xE0) == 0xE0) {
            return geode::Ok(matjson::Value(static_cast<int64_t>(static_cast<int8_t>(prefix))));
        }
        if ((prefix & 0xE0) == 0xA0) {
            return readString(prefix & 0x1F);
        }
        if ((prefix & 0xF0) == 0x90) {
            return readArray(prefix & 0x0F);
        }
        if ((prefix & 0xF0) == 0x80) {
            return readMap(prefix & 0x0F);
        }

        switch (prefix) {
            case 0xC0: return geode::Ok(matjson::Value());
            case 0xC2: return geode::Ok(matjson::Value(false));
            case 0xC3: return geode::Ok(matjson::Value(true));
            case 0xCC: return geode::Ok(matjson::Value(static_cast<int64_t>(readByte())));
            case 0xCD: return geode::Ok(matjson::Value(static_cast<int64_t>(readUnsigned16())));
            case 0xCE: return geode::Ok(matjson::Value(static_cast<int64_t>(readUnsigned32())));
            case 0xCF: return geode::Ok(matjson::Value(static_cast<double>(readUnsigned64())));
            case 0xD0: return geode::Ok(matjson::Value(static_cast<int64_t>(static_cast<int8_t>(readByte()))));
            case 0xD1: return geode::Ok(matjson::Value(static_cast<int64_t>(static_cast<int16_t>(readUnsigned16()))));
            case 0xD2: return geode::Ok(matjson::Value(static_cast<int64_t>(static_cast<int32_t>(readUnsigned32()))));
            case 0xD3: return geode::Ok(matjson::Value(static_cast<double>(static_cast<int64_t>(readUnsigned64()))));
            case 0xCB: return geode::Ok(matjson::Value(readDouble()));
            case 0xD9: return readString(readByte());
            case 0xDA: return readString(readUnsigned16());
            case 0xDB: return readString(readUnsigned32());
            case 0xDC: return readArray(readUnsigned16());
            case 0xDD: return readArray(readUnsigned32());
            case 0xDE: return readMap(readUnsigned16());
            case 0xDF: return readMap(readUnsigned32());
            case 0xC4: return readBinary(readByte());
            case 0xC5: return readBinary(readUnsigned16());
            case 0xC6: return readBinary(readUnsigned32());
            default: return geode::Err(fmt::format("Unsupported PaiDraw msgpack prefix {:02X}", prefix));
        }
    }

private:
    // Caps protect against malicious / corrupted msgpack frames declaring
    // billions of elements, which would otherwise OOM-crash the game during
    // allocation. PaiDraw payloads in normal play are tiny (chat lines,
    // strokes, room lists) — these limits are well above realistic worst-case.
    static constexpr size_t kMaxStringSize = 1 * 1024 * 1024;     // 1 MB string
    static constexpr size_t kMaxBinarySize = 4 * 1024 * 1024;     // 4 MB blob
    static constexpr size_t kMaxArrayElements = 100000;            // 100k items
    static constexpr size_t kMaxMapEntries    = 100000;            // 100k pairs

    geode::Result<matjson::Value> readString(size_t size) {
        if (size > kMaxStringSize) {
            return geode::Err("PaiDraw msgpack string too large");
        }
        if (!canRead(size)) {
            return geode::Err("PaiDraw msgpack short string");
        }
        std::string out(reinterpret_cast<char const*>(m_bytes.data() + m_offset), size);
        m_offset += size;
        return geode::Ok(matjson::Value(out));
    }

    geode::Result<matjson::Value> readBinary(size_t size) {
        if (size > kMaxBinarySize) {
            return geode::Err("PaiDraw msgpack binary too large");
        }
        if (!canRead(size)) {
            return geode::Err("PaiDraw msgpack short binary");
        }
        matjson::Value arr = matjson::Value::array();
        for (size_t i = 0; i < size; ++i) {
            arr.push(static_cast<int64_t>(m_bytes[m_offset + i]));
        }
        m_offset += size;
        return geode::Ok(arr);
    }

    geode::Result<matjson::Value> readArray(size_t size) {
        if (size > kMaxArrayElements) {
            return geode::Err("PaiDraw msgpack array too large");
        }
        // A single msgpack element can be one byte (e.g. a small int), so the
        // declared count must not exceed the bytes left. This rejects huge
        // bogus counts before the loop tries to allocate one entry per claim.
        if (size > m_bytes.size() - m_offset) {
            return geode::Err("PaiDraw msgpack array exceeds buffer");
        }
        matjson::Value arr = matjson::Value::array();
        for (size_t i = 0; i < size; ++i) {
            auto value = readValue();
            if (!value) {
                return geode::Err(value.unwrapErr());
            }
            arr.push(value.unwrap());
        }
        return geode::Ok(arr);
    }

    geode::Result<matjson::Value> readMap(size_t size) {
        if (size > kMaxMapEntries) {
            return geode::Err("PaiDraw msgpack map too large");
        }
        // Each (key, value) pair is at least 2 bytes; if the declared count
        // can't fit in the remaining buffer, refuse outright instead of
        // letting the loop attempt absurd allocations.
        if (size > (m_bytes.size() - m_offset) / 2) {
            return geode::Err("PaiDraw msgpack map exceeds buffer");
        }
        matjson::Value obj = matjson::Value::object();
        for (size_t i = 0; i < size; ++i) {
            auto keyValue = readValue();
            if (!keyValue) {
                return geode::Err(keyValue.unwrapErr());
            }
            auto key = keyValue.unwrap().asString().unwrapOr("");
            auto value = readValue();
            if (!value) {
                return geode::Err(value.unwrapErr());
            }
            obj[key] = value.unwrap();
        }
        return geode::Ok(obj);
    }

    bool canRead(size_t count) const {
        return m_offset + count <= m_bytes.size();
    }

    uint8_t readByte() {
        return canRead(1) ? m_bytes[m_offset++] : 0;
    }

    uint16_t readUnsigned16() {
        if (!canRead(2)) return 0;
        return readU16(m_bytes, m_offset);
    }

    uint32_t readUnsigned32() {
        if (!canRead(4)) return 0;
        return readU32(m_bytes, m_offset);
    }

    uint64_t readUnsigned64() {
        if (!canRead(8)) return 0;
        return readU64(m_bytes, m_offset);
    }

    double readDouble() {
        if (!canRead(8)) return 0.0;
        std::array<uint8_t, sizeof(double)> raw {};
        for (size_t i = 0; i < raw.size(); ++i) {
            raw[raw.size() - 1 - i] = m_bytes[m_offset + i];
        }
        m_offset += raw.size();
        double value = 0.0;
        std::memcpy(&value, raw.data(), sizeof(double));
        return value;
    }

    std::span<uint8_t const> m_bytes;
    size_t m_offset = 0;
};

inline geode::Result<matjson::Value> decodePayload(std::span<uint8_t const> bytes) {
    MsgPackReader reader(bytes);
    return reader.readValue();
}

} // namespace paidraw::codec
