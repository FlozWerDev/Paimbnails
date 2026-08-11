#include "IconThumbs.hpp"

#include "../data/IconAnatomy.hpp"
#include "../engine/PieceRenderer.hpp"
#include "../persist/IconPaths.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"

using namespace geode::prelude;
namespace ts = paimon::texture_studio;

namespace paimon::icon_maker {

namespace {

constexpr int kThumbSize = 128;

// Back to front, so the glow sits behind the body and the white details on top.
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

void IconThumbs::request(std::string const& projectId, ReadyCallback onReady) {
    if (projectId.empty() || !onReady) return;

    if (auto it = m_cache.find(projectId); it != m_cache.end()) {
        onReady(it->second);
        return;
    }

    // A render is already in flight: ride along instead of doing it twice.
    if (auto it = m_pending.find(projectId); it != m_pending.end()) {
        it->second.push_back(std::move(onReady));
        return;
    }

    auto loaded = IconProjectStore::get().loadProject(projectId);
    if (!loaded) return;
    auto project = loaded.unwrap();

    auto const* def = anatomyFor(project.type);
    if (!def) return;

    m_pending[projectId].push_back(std::move(onReady));

    auto keys = composeOrder(*def);
    auto imagesDir = IconPaths::imagesDir(projectId);

    paimon::ThreadTracker::get().spawn([projectId, project, keys, imagesDir]() {
        ts::ImageBuffer composite(kThumbSize, kThumbSize);
        for (auto const& key : keys) {
            if (auto r = PieceRenderer::renderSlot(project, key, kThumbSize, imagesDir)) {
                ts::SpritePreviewRenderer::compositeOver(composite, r.unwrap());
            }
        }

        Loader::get()->queueInMainThread(
            [projectId, composite = std::move(composite)]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                auto& self = IconThumbs::get();

                auto node = self.m_pending.find(projectId);
                if (node == self.m_pending.end()) return;  // invalidated meanwhile
                auto callbacks = std::move(node->second);
                self.m_pending.erase(node);

                auto* texture = ts::SpritePreviewRenderer::createTexture(composite);
                if (!texture) return;
                self.m_cache[projectId] = texture;
                for (auto const& cb : callbacks) cb(texture);
            });
    });
}

void IconThumbs::invalidate(std::string const& projectId) {
    m_cache.erase(projectId);
    m_pending.erase(projectId);
}

void IconThumbs::clear() {
    m_cache.clear();
    m_pending.clear();
}

}  // namespace paimon::icon_maker
