#include "../src/utils/GIFDecoder.hpp"

#include <array>
#include <algorithm>
#include <cassert>
#include <vector>

std::vector<uint8_t> fixture(int width, int height, bool interlaced) {
    std::vector<uint8_t> gif{'G', 'I', 'F', '8', '9', 'a',
        static_cast<uint8_t>(width), 0, static_cast<uint8_t>(height), 0, 0x84, 0, 0};
    for (int color = 0; color < 32; ++color) {
        gif.insert(gif.end(), {static_cast<uint8_t>(color * 7),
            static_cast<uint8_t>(color * 3), static_cast<uint8_t>(color)});
    }
    gif.insert(gif.end(), {0x2c, 0, 0, 0, 0, static_cast<uint8_t>(width), 0,
        static_cast<uint8_t>(height), 0, static_cast<uint8_t>(interlaced ? 0x40 : 0), 5});
    std::vector<int> rows;
    if (interlaced) {
        constexpr std::array starts{0, 4, 2, 1};
        constexpr std::array steps{8, 8, 4, 2};
        for (int pass = 0; pass < 4; ++pass) {
            for (int row = starts[pass]; row < height; row += steps[pass]) rows.push_back(row);
        }
    } else {
        for (int row = 0; row < height; ++row) rows.push_back(row);
    }
    std::vector<uint8_t> compressed;
    unsigned accumulator = 0;
    int bits = 0;
    auto code = [&](unsigned value) {
        accumulator |= value << bits;
        bits += 6;
        while (bits >= 8) {
            compressed.push_back(static_cast<uint8_t>(accumulator));
            accumulator >>= 8;
            bits -= 8;
        }
    };
    for (int row : rows) {
        for (int x = 0; x < width; ++x) {
            code(32); // Clear keeps code width constant.
            code(static_cast<unsigned>(row + 1));
        }
    }
    code(33);
    if (bits) compressed.push_back(static_cast<uint8_t>(accumulator));
    for (std::size_t offset = 0; offset < compressed.size();) {
        auto size = std::min<std::size_t>(255, compressed.size() - offset);
        gif.push_back(static_cast<uint8_t>(size));
        gif.insert(gif.end(), compressed.begin() + offset, compressed.begin() + offset + size);
        offset += size;
    }
    gif.insert(gif.end(), {0, 0x3b});
    return gif;
}

int main() {
    assert(!GIFDecoder::isGIF(nullptr, 10));
    auto invalid = GIFDecoder::decode(nullptr, 0);
    assert(invalid.frames.empty() && invalid.width == 0 && invalid.height == 0);
    {
        auto single = fixture(1, 1, false);
        auto firstImage = std::find(single.begin() + 13 + 32 * 3, single.end(), 0x2c);
        std::vector<uint8_t> two(single.begin(), firstImage);
        two.insert(two.end(), {0x21, 0xf9, 4, 1, 50, 0, 1, 0});
        two.insert(two.end(), firstImage, single.end() - 1);
        two.insert(two.end(), firstImage, single.end());
        auto decoded = GIFDecoder::decode(two.data(), two.size());
        assert(decoded.frames.size() == 2);
        assert(decoded.frames[0].pixels[3] == 0 && decoded.frames[0].delayMs == 500);
        assert(decoded.frames[1].pixels[3] == 255 && decoded.frames[1].delayMs == 100);
    }
    for (int height = 1; height <= 17; ++height) {
        for (int width : {1, 3}) {
            auto plain = fixture(width, height, false);
            auto interlaced = fixture(width, height, true);
            auto expected = GIFDecoder::decode(plain.data(), plain.size());
            auto actual = GIFDecoder::decode(interlaced.data(), interlaced.size());
            assert(expected.frames.size() == 1 && actual.frames.size() == 1);
            assert(expected.frames[0].pixels == actual.frames[0].pixels);
            for (int row = 0; row < height; ++row) {
                auto offset = static_cast<std::size_t>(row * width) * 4;
                assert(actual.frames[0].pixels[offset] == (row + 1) * 7);
                assert(actual.frames[0].pixels[offset + 3] == 255);
            }
        }
    }
}
