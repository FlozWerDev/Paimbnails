#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/cocos.hpp>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cocos2d { class CCTexture2D; }

namespace paimon::autopreview {

class AutoPreviewStore {
public:
    static AutoPreviewStore& get();
    bool has(int32_t levelID);
    // generated preview for this level. Converts to RGB888 internally. Atomic
    // write (tmp + rename). Thread-safe; returns false on any failure.
    bool save(int32_t levelID, uint8_t const* rgba, uint32_t width, uint32_t height);
    cocos2d::CCTexture2D* loadTexture(int32_t levelID);
    void clearAll();
    bool wasAttempted(int32_t levelID) const;
    void markAttempted(int32_t levelID);
    int generatedThisSession() const { return m_generatedThisSession; }
    void noteGenerated() { ++m_generatedThisSession; }

private:
    AutoPreviewStore() = default;

    std::filesystem::path dir() const;
    std::filesystem::path pathFor(int32_t levelID) const;
    void ensureScanned();

    void cacheTexture(int32_t levelID, cocos2d::CCTexture2D* tex);
    cocos2d::CCTexture2D* cachedTexture(int32_t levelID);

    mutable std::mutex m_mutex;
    bool m_scanned = false;
    std::unordered_set<int32_t> m_present;
    std::unordered_set<int32_t> m_attempted;
    // Atomic: noteGenerated() is called from the AutoPreview background worker
    // (AutoPreviewGenerator) while the main-thread queue reads/resets it.
    std::atomic<int> m_generatedThisSession{0};

    static constexpr size_t MAX_TEX_CACHE = 6;
    std::mutex m_texMutex;
    std::unordered_map<int32_t, cocos2d::CCTexture2D*> m_texCache;
    std::deque<int32_t> m_texLru;
};

} // namespace paimon::autopreview
