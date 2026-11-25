// MenuMusic library persistence guards (src/features/menu-music/services/
// MenuMusicLibrary.cpp): a corrupt library.json leaves m_loaded=false with
// m_loadFailed set, and save() must refuse to persist the resulting empty
// in-memory state over the file on disk (crash #7). Publication itself is
// atomic via paimon::file::writeAtomically (tmp+rename, real implementation
// linked). MenuMusicLibrary needs Geode bindings at runtime, so this test
// exercises the exact guard predicates against a scratch library file.

#include "../src/utils/AtomicFileWrite.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Minimal model of the MenuMusicLibrary load/save state machine.
struct LibraryModel {
    bool m_loaded = false;
    bool m_loadFailed = false;
    std::vector<std::string> m_tracks;
    std::vector<std::string> m_playlists;

    // Mirrors MenuMusicLibrary::load() failure branches: oversize/unreadable
    // or unparsable file -> loaded stays false, loadFailed set, retryable.
    // Here `parsed` stands in for the matjson parse outcome (geode file read):
    // false = corrupt JSON content.
    void load(bool fileExists, bool tooLarge, bool parsed) {
        if (!fileExists) {
            m_loaded = true;
            m_loadFailed = false;
            return;
        }
        if (tooLarge) {
            m_loadFailed = true;
            return;
        }
        if (!parsed) {
            m_loadFailed = true;
            return;
        }
        m_loaded = true;
        m_loadFailed = false;
    }

    // Mirrors the save() guard: never persist an empty library over a file
    // we failed to read. Returns false when the save is refused.
    bool save(fs::path const& file, std::string const& text) {
        if (m_loadFailed && m_tracks.empty() && m_playlists.empty()) {
            return false;
        }
        return paimon::file::writeAtomically(file, text);
    }
};

std::string readAll(fs::path const& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeAll(fs::path const& path, std::string const& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    assert(out);
}

fs::path freshDir() {
    auto dir = fs::temp_directory_path() /
        ("paimbnails-menumusic-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    return dir;
}

} // namespace

int main() {
    auto dir = freshDir();
    auto const library = dir / "library.json";

    // 1. corrupt JSON -> not loaded, load failed; save() refuses to wipe it.
    {
        LibraryModel model;
        writeAll(library, "{not valid json!!!");
        model.load(/*fileExists=*/true, /*tooLarge=*/false, /*parsed=*/false);
        assert(!model.m_loaded);
        assert(model.m_loadFailed);
        assert(!model.save(library, R"({"version":2,"tracks":[],"playlists":[]})"));
        assert(readAll(library) == "{not valid json!!!");
    }

    // 2. oversize/unreadable file -> same refusal, file untouched.
    {
        LibraryModel model;
        writeAll(library, "{not valid json!!!");
        model.load(/*fileExists=*/true, /*tooLarge=*/true, /*parsed=*/false);
        assert(!model.m_loaded && model.m_loadFailed);
        assert(!model.save(library, R"({"version":2})"));
        assert(readAll(library) == "{not valid json!!!");
    }

    // 3. once the library actually holds content, saving proceeds (tmp+rename,
    //    no leftovers) even while the load-failure flag is still set.
    {
        LibraryModel model;
        model.load(/*fileExists=*/true, /*tooLarge=*/false, /*parsed=*/false);
        model.m_tracks.push_back("recovered-track");
        std::string valid = R"({"version":2,"tracks":[{"id":"recovered-track"}]})";
        assert(model.save(library, valid));
        assert(readAll(library) == valid);
        for (auto const& entry : fs::directory_iterator(dir)) {
            assert(entry.path().extension() != ".tmp");
        }
    }

    // 4. missing file -> fresh library loads cleanly and saves normally.
    {
        LibraryModel model;
        model.load(/*fileExists=*/false, /*tooLarge=*/false, /*parsed=*/false);
        assert(model.m_loaded && !model.m_loadFailed);
        assert(model.save(dir / "fresh.json", R"({"version":2})"));
        assert(readAll(dir / "fresh.json") == R"({"version":2})");
    }

    // 5. atomic publication under contention: two writers, final content is
    //    always exactly one of the two payloads, never a mix, no .tmp remains.
    {
        auto const target = dir / "contended.json";
        std::string first(4096, 'a'), second(4096, 'b');
        assert(paimon::file::writeAtomically(target, first));
        constexpr int kWrites = 64;
        std::thread writer([&] {
            for (int i = 0; i < kWrites; ++i) {
                assert(paimon::file::writeAtomically(target, first));
            }
        });
        for (int i = 0; i < kWrites; ++i) {
            assert(paimon::file::writeAtomically(target, second));
        }
        writer.join();
        auto final = readAll(target);
        assert(final == first || final == second);
        for (auto const& entry : fs::directory_iterator(dir)) {
            assert(entry.path().extension() != ".tmp");
        }
    }

    fs::remove_all(dir);
}
