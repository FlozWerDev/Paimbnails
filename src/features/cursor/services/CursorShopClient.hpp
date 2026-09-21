#pragma once
// Tienda de cursores: solo se descarga lo que el usuario instala, nunca rastreo masivo.
// All callbacks return on main thread.

#include <Geode/Geode.hpp>
#include "CursorManager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::cursorshop {

enum class Store {
    RwDesigner   = 0,
    CustomCursor = 1,
};

inline constexpr int kStoreCount = 2;

struct Category {
    std::string id;
    std::string name;
    // Las secciones paginadas piden una pagina nueva al servidor; el resto
    // devuelve todo de golpe y la tienda pagina en local.
    bool paged = false;
    // Fichas por peticion. rw-designer redondea el offset a multiplos de este
    // numero, asi que no se puede pedir menos. 0 = catalogo entero de una vez.
    int fetchSize = 0;
};

// Ficha del grid: un set de rw-designer, un cursor suelto del junkyard o un
// pack de custom-cursor.
struct Listing {
    Store store = Store::RwDesigner;
    std::string id;
    std::string name;
    std::string author;
    std::string extra;      // descargas, numero de cursores...
    std::string thumbUrl;
    // Los cursores sueltos ya saben su fichero, no hace falta abrir la ficha.
    std::string directUrl;
    bool single = false;
    // .ani: al instalarlo se convierte en GIF y se mueve de verdad.
    bool animated = false;
};

struct ListingPage {
    std::vector<Listing> items;
    int page = 0;
    int pageCount = 1;
};

struct DetailCursor {
    std::string name;
    std::string previewUrl;
    std::string downloadUrl;
    // custom-cursor sirve algunos packs en dos tamaños; se intenta el grande y
    // se cae al de la pagina si no existe.
    std::string fallbackUrl;
    // rw-designer marca el rol de cada cursor y custom-cursor separa flecha de
    // puntero, asi que casi siempre se puede sugerir un estado.
    CursorState suggested = CursorState::Idle;
    bool hasSuggested = false;
    bool animated = false;
};

struct Detail {
    std::string name;
    std::string author;
    std::string description;
    // Canonical page this detail was read from (empty when unknown). The UI
    // offers it as "Ver original" so authors get the visit.
    std::string sourceUrl;
    std::vector<DetailCursor> cursors;
};

class ShopClient final {
public:
    using ListingCallback  = geode::CopyableFunction<void(geode::Result<ListingPage>)>;
    using DetailCallback   = geode::CopyableFunction<void(geode::Result<Detail>)>;
    using BytesCallback    = geode::CopyableFunction<void(geode::Result<std::vector<std::uint8_t>>)>;
    using CategoryCallback = geode::CopyableFunction<void(std::vector<Category>)>;

    static char const* storeName(Store store);
    static char const* storeCredit(Store store);

    static std::vector<Category> builtinCategories(Store store);

    // rw-designer tiene buscador propio; el de custom-cursor esta bloqueado y
    // hay que recorrer sus colecciones a mano.
    static bool supportsSearch(Store store);
    // Categoria sintetica que fetchListing reconoce y resuelve como busqueda.
    static Category searchCategory(Store store, std::string const& query);
    // Añade las colecciones recientes de custom-cursor a las fijas. En
    // rw-designer no hay categorias, asi que responde con las fijas.
    static void fetchCategories(Store store, CategoryCallback cb);

    static void fetchListing(Store store, Category const& category, int page, ListingCallback cb);
    static void fetchDetail(Listing const& listing, DetailCallback cb);

    // Solo desde una accion explicita del usuario.
    static void download(std::string const& url, BytesCallback cb);
    static void download(std::string const& url, std::string const& fallbackUrl, BytesCallback cb);

    static std::string filenameFor(std::string const& url, std::string const& fallbackStem);

private:
    ShopClient() = delete;
};

} // namespace paimon::cursorshop
