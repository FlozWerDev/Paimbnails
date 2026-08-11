#pragma once
// Miniaturas remotas de la tienda.
//
// Una pagina son 40 fichas, asi que las peticiones van por una cola con un tope
// de descargas a la vez: lanzarlas todas de golpe dejaba la mayoria de celdas
// en blanco. Cada URL se pide una sola vez, con un reintento antes de darla por
// perdida, y las texturas se guardan mientras la tienda siga abierta.

#include <Geode/Geode.hpp>

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace paimon::cursorshop {

class ShopImages final {
public:
    using Callback = geode::CopyableFunction<void(cocos2d::CCTexture2D*)>;

    static ShopImages& get();

    // Devuelve la textura si ya esta cacheada. Si no, encola la descarga y llama
    // a cb cuando llegue (nullptr si acaba fallando).
    cocos2d::CCTexture2D* fetch(std::string const& url, Callback cb);

    void clear();
    // Permite que las URLs fallidas se vuelvan a intentar.
    void forgetFailures();

private:
    ShopImages() = default;

    void pump();
    void finish(std::string const& url, cocos2d::CCTexture2D* texture);

    std::unordered_map<std::string, geode::Ref<cocos2d::CCTexture2D>> m_cache;
    std::unordered_map<std::string, std::vector<Callback>> m_pending;
    std::unordered_map<std::string, int> m_attempts;
    std::deque<std::string> m_queue;
    std::unordered_set<std::string> m_failed;
    int m_active = 0;
};

// Cuelga la miniatura de `holder` cuando llega, escalada para caber. Mientras
// tanto deja un marcador, que pasa a "?" si la descarga no sale.
void mountThumb(cocos2d::CCNode* holder, std::string const& url,
                float maxWidth, float maxHeight);

} // namespace paimon::cursorshop
