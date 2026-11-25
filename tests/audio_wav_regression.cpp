#include "../src/video/AudioWavCommon.hpp"
#include "../src/video/AudioExtractor.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace fs = std::filesystem;

std::string utf8(fs::path const& path) {
    auto text = path.u8string();
    return {text.begin(), text.end()};
}

#ifdef _WIN32
std::wstring environment(wchar_t const* key) {
    size_t length = 0;
    assert(_wgetenv_s(&length, nullptr, 0, key) == 0);
    if (length == 0) return {};
    std::wstring value(length, L'\0');
    assert(_wgetenv_s(&length, value.data(), value.size(), key) == 0);
    value.resize(length - 1);
    return value;
}
#endif

int main() {
    using paimon::video::detail::writeWavFile;
    auto temp = fs::temp_directory_path();
    auto directory = temp / ("paimbnails-wav-regression-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    directory /= fs::path(u8"m\u00fasica-\u65e5\u672c-\U0001f3b5");
    fs::create_directories(directory);
    auto target = directory / fs::path(u8"canci\u00f3n.wav");
    std::array<uint8_t, 8> pcm{0, 1, 2, 3, 4, 5, 6, 7};

    assert(writeWavFile(utf8(target), pcm.data(), pcm.size(), 2, 48000, 16));
    assert(fs::file_size(target) == sizeof(paimon::video::detail::WavHeader) + pcm.size());
    std::ifstream input(target, std::ios::binary);
    paimon::video::detail::WavHeader header;
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    assert(input.good());
    assert(std::string(header.riff, 4) == "RIFF");
    assert(std::string(header.wave, 4) == "WAVE");
    assert(header.fileSize == 44 && header.dataSize == pcm.size());
    assert(header.numChannels == 2 && header.sampleRate == 48000);
    assert(header.bitsPerSample == 16 && header.blockAlign == 4 && header.byteRate == 192000);
    std::string samples{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    assert(samples == std::string(pcm.begin(), pcm.end()));
    input.close();

    assert(!writeWavFile(utf8(target), nullptr, pcm.size(), 2, 48000, 16));
    assert(!writeWavFile(utf8(target), pcm.data(), 0, 2, 48000, 16));
    assert(!writeWavFile(utf8(target), pcm.data(), pcm.size(), 0, 48000, 16));
    assert(!writeWavFile(utf8(target), pcm.data(), pcm.size(), 2, 0, 16));
    assert(!writeWavFile(utf8(target), pcm.data(), pcm.size(), 2, 48000, 7));
    assert(!writeWavFile(utf8(target), pcm.data(), 7, 2, 48000, 16));
    assert(!writeWavFile(utf8(target), pcm.data(), std::numeric_limits<uint32_t>::max(), 2, 48000, 16));
    assert(!writeWavFile(utf8(target), pcm.data(), pcm.size(), 2,
        std::numeric_limits<uint32_t>::max(), 16));
    assert(fs::file_size(target) == sizeof(header) + pcm.size());
    assert(!writeWavFile(utf8(directory), pcm.data(), pcm.size(), 2, 48000, 16));
    assert(fs::is_directory(directory));

    // Cached paths crossing the public UTF-8 API must resolve to the same file.
#ifdef _WIN32
    auto priorTemp = environment(L"TEMP");
    auto priorTmp = environment(L"TMP");
    assert(_wputenv_s(L"TEMP", directory.c_str()) == 0);
    assert(_wputenv_s(L"TMP", directory.c_str()) == 0);
#else
    std::string priorTmp = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "";
    assert(setenv("TMPDIR", directory.c_str(), 1) == 0);
#endif
    assert(fs::equivalent(fs::temp_directory_path(), directory));
    auto video = utf8(directory / "source.mp4");
    auto cache = paimon::video::detail::makeWavPath(video);
    assert(writeWavFile(cache, pcm.data(), pcm.size(), 2, 48000, 16));
    assert(paimon::video::getCachedWavPath(video) == cache);
    paimon::video::cleanupAudioCache(video);
    assert(paimon::video::getCachedWavPath(video).empty());
#ifdef _WIN32
    assert(_wputenv_s(L"TEMP", priorTemp.c_str()) == 0);
    assert(_wputenv_s(L"TMP", priorTmp.c_str()) == 0);
#else
    assert(priorTmp.empty() ? unsetenv("TMPDIR") == 0 : setenv("TMPDIR", priorTmp.c_str(), 1) == 0);
#endif

    auto root = directory.parent_path();
    assert(root.is_absolute() && fs::equivalent(root.parent_path(), temp));
    fs::remove_all(root);
}
