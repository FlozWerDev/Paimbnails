#pragma once

#include "PaiDrawModels.hpp"
#include <bit>
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

    void floating(double value) {
        m_bytes.push_back(0xCB);
        pushU64(m_bytes, std::bit_cast<uint64_t>(value));
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

    geode::Result<matjson::Value> readValue(size_t depth = 0) {
        if (depth >= 64) {
            return geode::Err("PaiDraw msgpack nesting too deep");
        }
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
            return readArray(prefix & 0x0F, depth);
        }
        if ((prefix & 0xF0) == 0x80) {
            return readMap(prefix & 0x0F, depth);
        }

        size_t required = 0;
        switch (prefix) {
            case 0xCC: case 0xD0: case 0xD9: case 0xC4: required = 1; break;
            case 0xCD: case 0xD1: case 0xDA: case 0xDC: case 0xDE: case 0xC5: required = 2; break;
            case 0xCE: case 0xD2: case 0xDB: case 0xDD: case 0xDF: case 0xC6: required = 4; break;
            case 0xCF: case 0xD3: case 0xCB: required = 8; break;
            default: break;
        }
        if (!canRead(required)) {
            return geode::Err("PaiDraw msgpack truncated value");
        }

        switch (prefix) {
            case 0xC0: return geode::Ok(matjson::Value());
            case 0xC2: return geode::Ok(matjson::Value(false));
            case 0xC3: return geode::Ok(matjson::Value(true));
            case 0xCC: return geode::Ok(matjson::Value(static_cast<int64_t>(readByte())));
            case 0xCD: return geode::Ok(matjson::Value(static_cast<int64_t>(readUnsigned16())));
            case 0xCE: return geode::Ok(matjson::Value(static_cast<int64_t>(readUnsigned32())));
            case 0xCF: return geode::Ok(matjson::Value(readUnsigned64()));
            case 0xD0: return geode::Ok(matjson::Value(static_cast<int64_t>(static_cast<int8_t>(readByte()))));
            case 0xD1: return geode::Ok(matjson::Value(static_cast<int64_t>(static_cast<int16_t>(readUnsigned16()))));
            case 0xD2: return geode::Ok(matjson::Value(static_cast<int64_t>(static_cast<int32_t>(readUnsigned32()))));
            case 0xD3: return geode::Ok(matjson::Value(static_cast<int64_t>(readUnsigned64())));
            case 0xCB: return geode::Ok(matjson::Value(readDouble()));
            case 0xD9: return readString(readByte());
            case 0xDA: return readString(readUnsigned16());
            case 0xDB: return readString(readUnsigned32());
            case 0xDC: return readArray(readUnsigned16(), depth);
            case 0xDD: return readArray(readUnsigned32(), depth);
            case 0xDE: return readMap(readUnsigned16(), depth);
            case 0xDF: return readMap(readUnsigned32(), depth);
            case 0xC4: return readBinary(readByte());
            case 0xC5: return readBinary(readUnsigned16());
            case 0xC6: return readBinary(readUnsigned32());
            default: return geode::Err(fmt::format("Unsupported PaiDraw msgpack prefix {:02X}", prefix));
        }
    }

private:
    // caps vs malformed msgpack frames claiming huge counts; normal payloads are far smaller.
    static constexpr size_t kMaxStringSize = 1 * 1024 * 1024;     // 1 mb string
    static constexpr size_t kMaxBinarySize = 4 * 1024 * 1024;     // 4 mb blob
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

    geode::Result<matjson::Value> readArray(size_t size, size_t depth) {
        if (size > kMaxArrayElements) {
            return geode::Err("PaiDraw msgpack array too large");
        }
        // each element is at least 1 byte; reject huge bogus counts early.
        if (size > m_bytes.size() - m_offset) {
            return geode::Err("PaiDraw msgpack array exceeds buffer");
        }
        matjson::Value arr = matjson::Value::array();
        for (size_t i = 0; i < size; ++i) {
            auto value = readValue(depth + 1);
            if (!value) {
                return geode::Err(value.unwrapErr());
            }
            arr.push(value.unwrap());
        }
        return geode::Ok(arr);
    }

    geode::Result<matjson::Value> readMap(size_t size, size_t depth) {
        if (size > kMaxMapEntries) {
            return geode::Err("PaiDraw msgpack map too large");
        }
        // each (key, value) pair is at least 2 bytes; reject huge bogus counts early.
        if (size > (m_bytes.size() - m_offset) / 2) {
            return geode::Err("PaiDraw msgpack map exceeds buffer");
        }
        matjson::Value obj = matjson::Value::object();
        for (size_t i = 0; i < size; ++i) {
            auto keyValue = readValue(depth + 1);
            if (!keyValue) {
                return geode::Err(keyValue.unwrapErr());
            }
            if (!keyValue.unwrap().isString()) {
                return geode::Err("PaiDraw msgpack map key is not a string");
            }
            auto key = keyValue.unwrap().asString().unwrapOr("");
            auto value = readValue(depth + 1);
            if (!value) {
                return geode::Err(value.unwrapErr());
            }
            obj[key] = value.unwrap();
        }
        return geode::Ok(obj);
    }

    bool canRead(size_t count) const {
        return m_offset <= m_bytes.size() && count <= m_bytes.size() - m_offset;
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
        return std::bit_cast<double>(readUnsigned64());
    }

    std::span<uint8_t const> m_bytes;
    size_t m_offset = 0;
};

inline geode::Result<matjson::Value> decodePayload(std::span<uint8_t const> bytes) {
    MsgPackReader reader(bytes);
    return reader.readValue();
}

} // namespace paidraw::codec
