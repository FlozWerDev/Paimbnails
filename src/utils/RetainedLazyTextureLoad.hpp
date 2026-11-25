#pragma once
#include "FilesystemPath.hpp"

#include <Geode/ui/LazySprite.hpp>
#include <Geode/utils/function.hpp>
#include <cstddef>
#include <filesystem>
#include <memory>

namespace paimon::image {
class RetainedLazyTextureLoad final {
public:
    using Callback = geode::Function<void(cocos2d::CCTexture2D*, bool)>;

    RetainedLazyTextureLoad()
      : m_state(std::make_shared<State>()) {}
    RetainedLazyTextureLoad(RetainedLazyTextureLoad const&) = delete;
    RetainedLazyTextureLoad& operator=(RetainedLazyTextureLoad const&) = delete;
    ~RetainedLazyTextureLoad() {
        this->reset();
    }

    void reset() {
        ++m_state->requestToken;
        m_state->callback = nullptr;
        if (m_state->loader) {
            m_state->loader->cancelLoad();
            m_state->loader = nullptr;
        }
    }

    void loadFromFile(
        std::filesystem::path const& path,
        Callback callback,
        bool ignoreCache = false,
        geode::LazySprite::Format format = geode::LazySprite::Format::kFmtUnKnown
    ) {
        this->reset();

        auto state = m_state;
        auto loader = geode::LazySprite::create({1.f, 1.f}, false);
        if (!loader) {
            if (callback) callback(nullptr, false);
            return;
        }
        auto token = ++state->requestToken;
        state->loader = loader;
        state->callback = std::move(callback);
        // LazySprite keeps its load callback after completion. It must not own itself,
        // the State, or the caller's UI callback through that member.
        loader->setLoadCallback([
            weakState = std::weak_ptr<State>(state), token
        ](geode::Result<> result) mutable {
            auto state = weakState.lock();
            if (!state || token != state->requestToken) return;
            auto loader = state->loader; // retain through reentrant reset/load calls
            auto callback = std::move(state->callback);
            state->callback = nullptr;
            state->loader = nullptr;
            auto* texture = result && loader ? loader->getTexture() : nullptr;
            if (callback) callback(texture, texture != nullptr);
        });

        loader->loadFromFile(paimon::paths::toSdkPath(path), format, ignoreCache);
    }

private:
    struct State final {
        geode::Ref<geode::LazySprite> loader = nullptr;
        Callback callback;
        std::size_t requestToken = 0;
    };

    std::shared_ptr<State> m_state;
};
}
