#pragma once

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
        auto token = ++state->requestToken;
        state->loader = loader;

        loader->setLoadCallback([
            state,
            token,
            loaderRef = geode::Ref<geode::LazySprite>(loader),
            callback = std::move(callback)
        ](geode::Result<> result) mutable {
            if (token != state->requestToken) {
                return;
            }

            auto* texture = result ? loaderRef->getTexture() : nullptr;
            bool success = texture != nullptr;
            state->loader = nullptr;

            if (callback) {
                callback(texture, success);
            }
        });

        loader->loadFromFile(path, format, ignoreCache);
    }

private:
    struct State final {
        geode::Ref<geode::LazySprite> loader = nullptr;
        std::size_t requestToken = 0;
    };

    std::shared_ptr<State> m_state;
};
}