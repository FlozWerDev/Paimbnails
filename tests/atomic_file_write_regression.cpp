#include "../src/utils/AtomicFileWrite.hpp"

#include <cassert>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#endif

std::string read(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main() {
    namespace fs = std::filesystem;
    auto temp = fs::temp_directory_path();
    auto directory = temp / ("paimbnails-atomic-regression-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);
    auto target = directory / "project.json";
    assert(paimon::file::writeAtomically(target, "original"));
    assert(paimon::file::writeAtomically(target, "replacement"));
    assert(read(target) == "replacement");
#ifdef _WIN32
    auto locked = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    assert(!paimon::file::writeAtomically(target, "must not replace locked file"));
    CloseHandle(locked);
    assert(read(target) == "replacement");
#endif
    auto blocked = directory / "blocked.json";
    fs::create_directory(blocked);
    assert(paimon::file::writeAtomically(blocked / "original", "keep"));
    assert(!paimon::file::writeAtomically(blocked, "cannot replace a directory"));
    assert(read(blocked / "original") == "keep");

    std::string first(8192, 'a'), second(8192, 'b');
    // Exercise overlapping publishes repeatedly; unique temporary names alone
    // do not serialize Windows replacement handles for the destination.
    constexpr int concurrentWrites = 128;
    std::thread writer([&] {
        for (int i = 0; i < concurrentWrites; ++i) {
            assert(paimon::file::writeAtomically(target, first));
        }
    });
    for (int i = 0; i < concurrentWrites; ++i) {
        assert(paimon::file::writeAtomically(target, second));
    }
    writer.join();
    auto final = read(target);
    assert(final == first || final == second);
    for (auto const& entry : fs::directory_iterator(directory)) {
        assert(entry.path().extension() != ".tmp");
    }
    assert(directory.is_absolute() && fs::equivalent(directory.parent_path(), temp));
    fs::remove_all(directory);
}
