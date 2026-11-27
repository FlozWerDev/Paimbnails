#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "../src/utils/WebPDecoder.hpp"

namespace {

bool check(bool cond, char const* label) {
    if (!cond) std::cerr << "FAIL: " << label << '\n';
    else std::cout << "PASS: " << label << '\n';
    return cond;
}

std::vector<std::uint8_t> readAll(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char** argv) {
    bool ok = true;

    // 1. Reject garbage / non-WebP data cleanly.
    std::vector<std::uint8_t> const garbage{'N', 'O', 'T', 'W', 'E', 'B', 'P', 0, 1, 2, 3, 4};
    ok &= check(!WebPDecoder::isWebP(garbage.data(), garbage.size()), "rejects non-WebP magic");
    int w = 0, h = 0;
    bool anim = false;
    ok &= check(!WebPDecoder::getDimensions(garbage.data(), garbage.size(), w, h, anim),
                "getDimensions rejects non-WebP");
    auto bad = WebPDecoder::decode(garbage.data(), garbage.size(), 1);
    ok &= check(bad.frames.empty(), "decode rejects non-WebP");

    // 2. Decode real WebP files passed on the command line (e.g. from Downloads).
    for (int i = 1; i < argc; ++i) {
        std::filesystem::path p = argv[i];
        auto bytes = readAll(p);
        if (bytes.empty()) {
            std::cerr << "FAIL: could not read " << p << '\n';
            ok = false;
            continue;
        }
        bool isW = WebPDecoder::isWebP(bytes.data(), bytes.size());
        int dw = 0, dh = 0;
        bool isA = false;
        bool dims = WebPDecoder::getDimensions(bytes.data(), bytes.size(), dw, dh, isA);
        auto dec = WebPDecoder::decode(bytes.data(), bytes.size(), 16);
        bool valid = isW && dims && dw > 0 && dh > 0 &&
            dec.width == dw && dec.height == dh && !dec.frames.empty() &&
            dec.frames.front().pixels.size() ==
                static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh) * 4;
        std::cout << "WebP " << p.filename().string() << ": "
                  << dw << "x" << dh << " animated=" << isA
                  << " frames=" << dec.frames.size() << '\n';
        ok &= check(valid, "real WebP file decoded to valid RGBA");
    }
    return ok ? 0 : 1;
}
