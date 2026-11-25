#include "IconThumbs.hpp"

#include "../data/IconAnatomy.hpp"
#include "../engine/PieceRenderer.hpp"
#include "../persist/IconPaths.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include <exception>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;

namespace paimon::icon_maker {

namespace {

constexpr int kThumbSize = 128;

// back to front, so the glow sits behind the body and the white details on top.
std::vector<std::string> composeOrder(AnatomyDef const& def) {
    int part = def.partCount > 1 ? 1 : 0;
    std::vector<std::string> keys;
    for (char const* key : {"glow", "tertiary", "secondary", "main", "extra"}) {
        for (auto const& slot : def.slots) {
            if (slot.key == key) keys.push_back(slotStorageKey(part, key));
        }
    }
    return keys;
}

}  // anonymous namespace

IconThumbs& IconThumbs::get() {
    static IconThumbs instance;
    return instance;
}

void IconThumbs::touch(std::string const& id) {
    m_cacheOrder.remove(id);
    m_cacheOrder.push_back(id);
}

void IconThumbs::cacheTexture(std::string const& id, CCTexture2D* texture) {
    if (!texture) return;
    m_cache[id] = texture;
    touch(id);
    while (m_cache.size() > kMaxCachedThumbs) {
        m_cache.erase(m_cacheOrder.front());
        m_cacheOrder.pop_front();
    }
}

void IconThumbs::request(std::string const& projectId, ReadyCallback onReady) {
    if (projectId.empty() || !onReady) return;
    if (auto it = m_cache.find(projectId); it != m_cache.end()) {
        geode::Ref<CCTexture2D> texture = it->second;
        touch(projectId);
        onReady(texture);
        return;
    }
    if (auto it = m_pending.find(projectId); it != m_pending.end()) {
        it->second.callbacks.push_back(std::move(onReady));
        return;
    }
    auto loaded = IconProjectStore::get().loadProject(projectId);
    if (!loaded) { onReady(nullptr); return; }
    auto project = loaded.unwrap();
    auto const* def = anatomyFor(project.type);
    if (!def) { onReady(nullptr); return; }

    auto token = ++m_nextToken;
    m_pending.emplace(projectId, PendingRender{token, {std::move(onReady)}});
    auto keys = composeOrder(*def);
    auto imagesDir = IconPaths::imagesDir(projectId);
    bool started = paimon::ThreadTracker::get().spawn([projectId, project, keys, imagesDir, token]() {
        ts::ImageBuffer composite;
        try {
            composite = ts::ImageBuffer(kThumbSize, kThumbSize);
            for (auto const& key : keys) {
                if (auto r = PieceRenderer::renderSlot(project, key, kThumbSize, imagesDir)) {
                    ts::SpritePreviewRenderer::compositeOver(composite, r.unwrap());
                }
            }
        } catch (std::exception const& error) {
            composite = ts::ImageBuffer();
            log::warn("[icon-maker] thumbnail render failed: {}", error.what());
        }
        Loader::get()->queueInMainThread(
            [projectId, token, composite = std::move(composite)]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                auto& self = IconThumbs::get();
                auto node = self.m_pending.find(projectId);
                if (node == self.m_pending.end() || node->second.token != token) return;
                auto callbacks = std::move(node->second.callbacks);
                self.m_pending.erase(node);
                // A callback may clear the cache; retain during the whole delivery.
                geode::Ref<CCTexture2D> texture = composite.empty()
                    ? nullptr : ts::SpritePreviewRenderer::createTexture(composite);
                self.cacheTexture(projectId, texture);
                for (auto const& cb : callbacks) cb(texture);
            });
    });
    if (!started) {
        auto node = m_pending.find(projectId);
        if (node == m_pending.end() || node->second.token != token) return;
        auto callbacks = std::move(node->second.callbacks);
        m_pending.erase(node);
        for (auto const& cb : callbacks) cb(nullptr);
    }
}

void IconThumbs::invalidate(std::string const& projectId) {
    m_cache.erase(projectId);
    m_cacheOrder.remove(projectId);
    m_pending.erase(projectId);
}

void IconThumbs::clear() {
    m_cache.clear();
    m_cacheOrder.clear();
    m_pending.clear();
}

}  // namespace paimon::icon_maker
