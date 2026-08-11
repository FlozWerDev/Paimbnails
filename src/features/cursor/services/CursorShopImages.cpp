#include "CursorShopImages.hpp"
#include "CursorShopClient.hpp"
#include "../../../utils/ImageLoadHelper.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::cursorshop {

namespace {

// Descargas simultaneas. Con las 40 de una pagina a la vez la mayoria se
// quedaba sin resolver.
constexpr int kMaxConcurrent = 4;
// Un corte suelto se reintenta una vez antes de marcar la URL como fallida.
constexpr int kMaxAttempts = 2;
// Al pasarse se tira el mapa entero: los sprites ya montados retienen su propia
// textura, asi que lo unico que se pierde es el atajo de cache.
constexpr std::size_t kMaxCachedTextures = 400;

constexpr int kPlaceholderTag = 0x5401;

} // namespace

ShopImages& ShopImages::get() {
    static ShopImages instance;
    return instance;
}

CCTexture2D* ShopImages::fetch(std::string const& url, Callback cb) {
    if (url.empty()) return nullptr;

    if (auto found = m_cache.find(url); found != m_cache.end()) {
        return found->second.data();
    }
    if (m_failed.count(url)) {
        // Se responde ya para que quien pidio la imagen pueda marcar el fallo.
        if (cb) cb(nullptr);
        return nullptr;
    }

    bool alreadyWanted = m_pending.count(url) > 0;
    auto& waiters = m_pending[url];
    if (cb) waiters.push_back(std::move(cb));

    if (!alreadyWanted) {
        m_queue.push_back(url);
        pump();
    }
    return nullptr;
}

void ShopImages::pump() {
    while (m_active < kMaxConcurrent && !m_queue.empty()) {
        auto url = m_queue.front();
        m_queue.pop_front();

        // Pudo resolverse o descartarse mientras esperaba turno.
        if (!m_pending.count(url) || m_cache.count(url)) continue;

        ++m_active;
        ++m_attempts[url];

        ShopClient::download(url, [url](Result<std::vector<std::uint8_t>> res) {
            auto& self = ShopImages::get();
            if (self.m_active > 0) --self.m_active;

            CCTexture2D* texture = nullptr;
            if (res) {
                auto const& bytes = res.unwrap();
                auto image = ImageLoadHelper::loadWithSTBFromMemory(bytes.data(), bytes.size(), false);
                if (image.success && image.texture) {
                    if (self.m_cache.size() >= kMaxCachedTextures) self.m_cache.clear();
                    self.m_cache[url] = image.texture;
                    texture = image.texture;
                    // El mapa se queda con su propia referencia.
                    image.texture->release();
                }
            }

            if (!texture && self.m_pending.count(url) && self.m_attempts[url] < kMaxAttempts) {
                self.m_queue.push_back(url);
                self.pump();
                return;
            }

            if (!texture) {
                log::debug("[CursorShop] thumbnail failed after {} attempt(s): {}",
                    self.m_attempts[url], url);
            }
            self.finish(url, texture);
            self.pump();
        });
    }
}

void ShopImages::finish(std::string const& url, CCTexture2D* texture) {
    std::vector<Callback> waiters;
    if (auto pending = m_pending.find(url); pending != m_pending.end()) {
        waiters = std::move(pending->second);
        m_pending.erase(pending);
    }
    m_attempts.erase(url);
    if (!texture) m_failed.insert(url);

    for (auto& waiter : waiters) {
        if (waiter) waiter(texture);
    }
}

void ShopImages::clear() {
    // m_active no se toca: las descargas en vuelo lo bajaran al volver.
    m_cache.clear();
    m_failed.clear();
    m_pending.clear();
    m_attempts.clear();
    m_queue.clear();
}

void ShopImages::forgetFailures() {
    m_failed.clear();
}

void mountThumb(CCNode* holder, std::string const& url, float maxWidth, float maxHeight) {
    if (!holder || url.empty()) return;

    auto box = holder->getContentSize();

    // El marcador va antes de pedir la imagen: si la URL ya se dio por fallida,
    // fetch responde en el acto y hace falta que exista para poder marcarlo.
    if (auto* placeholder = CCLabelBMFont::create("...", "bigFont.fnt")) {
        placeholder->setTag(kPlaceholderTag);
        placeholder->setScale(0.3f);
        placeholder->setOpacity(90);
        placeholder->setPosition({box.width / 2.f, box.height / 2.f});
        holder->addChild(placeholder, 1);
    }

    auto place = [holder = Ref<CCNode>(holder), maxWidth, maxHeight](CCTexture2D* texture) {
        if (!holder->getParent()) return;
        if (auto* placeholder = holder->getChildByTag(kPlaceholderTag)) {
            if (!texture) {
                static_cast<CCLabelBMFont*>(placeholder)->setString("?");
                return;
            }
            placeholder->removeFromParent();
        }
        if (!texture) return;

        auto* sprite = CCSprite::createWithTexture(texture);
        if (!sprite) return;
        auto size = sprite->getContentSize();
        if (size.width > 0.f && size.height > 0.f) {
            sprite->setScale(std::min(maxWidth / size.width, maxHeight / size.height));
        }
        auto box = holder->getContentSize();
        sprite->setPosition({box.width / 2.f, box.height / 2.f});
        holder->addChild(sprite, 1);
    };

    if (auto* cached = ShopImages::get().fetch(url, place)) place(cached);
}

} // namespace paimon::cursorshop
