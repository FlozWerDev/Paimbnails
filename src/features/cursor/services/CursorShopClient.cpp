#include "CursorShopClient.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/utils/web.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>

using namespace geode::prelude;

namespace paimon::cursorshop {

namespace {

// Cloudflare rechaza user agents genericos en custom-cursor.com, asi que se
// manda uno de navegador completo.
constexpr char const* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";

constexpr char const* kRwBase = "https://www.rw-designer.com";
constexpr char const* kCcBase = "https://custom-cursor.com";

constexpr std::size_t kMaxHtmlBytes     = 8u * 1024 * 1024;
constexpr std::size_t kMaxDownloadBytes = 24u * 1024 * 1024;

constexpr int kRwSetsPerPage  = 40;
constexpr int kRwLoosePerPage = 100;
constexpr int kRwSearchPerPage = 50;
// La cuenta que da el buscador incluye iconos, asi que las ultimas paginas
// pueden venir sin cursores; mas alla de esto no merece la pena seguir.
constexpr int kMaxSearchPages = 40;

// Prefijo de la categoria sintetica de busqueda.
constexpr std::string_view kSearchPrefix = "search:";

// Las colecciones grandes de custom-cursor pasan de 600 packs; se recorta para
// no construir un grid interminable.
constexpr std::size_t kMaxListingItems = 600;

web::WebRequest makeRequest(int timeoutSeconds) {
    web::WebRequest req;
    req.userAgent(kUserAgent);
    req.header("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8");
    req.header("Accept-Language", "en-US,en;q=0.9");
    req.timeout(std::chrono::seconds(timeoutSeconds));
    req.followRedirects(true);
    return req;
}

std::string htmlDecode(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out.push_back(in[i]);
            continue;
        }
        auto semi = in.find(';', i + 1);
        if (semi == std::string_view::npos || semi - i > 8) {
            out.push_back('&');
            continue;
        }
        auto entity = in.substr(i + 1, semi - i - 1);
        if (entity == "amp")       out.push_back('&');
        else if (entity == "lt")   out.push_back('<');
        else if (entity == "gt")   out.push_back('>');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "nbsp") out.push_back(' ');
        else if (entity == "apos" || entity == "#39")  out.push_back('\'');
        else if (entity == "#34")  out.push_back('"');
        else if (entity == "#38")  out.push_back('&');
        else {
            out.push_back('&');
            continue;
        }
        i = semi;
    }
    return out;
}

std::string stripTags(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    bool inTag = false;
    for (char c : raw) {
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; out.push_back(' '); continue; }
        if (!inTag) out.push_back(c);
    }
    return out;
}

// Los .fnt del juego solo tienen ASCII imprimible: lo demas se descarta antes de
// llegar a una etiqueta.
std::string cleanLabel(std::string_view raw, std::size_t maxLen = 58) {
    auto decoded = htmlDecode(raw);
    std::string out;
    out.reserve(decoded.size());
    bool pendingSpace = false;
    for (unsigned char c : decoded) {
        if (c <= ' ' || c == 0x7f) {
            pendingSpace = !out.empty();
            continue;
        }
        if (c > 0x7e) continue;
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(static_cast<char>(c));
    }
    if (out.size() > maxLen) {
        out.resize(maxLen > 3 ? maxLen - 3 : maxLen);
        out += "...";
    }
    return out;
}

// Valor de `key="..."` buscando desde `from` sin pasar de `limit`.
std::string rawAttr(std::string const& h, std::size_t from, std::size_t limit, std::string_view key) {
    std::string needle(key);
    needle += "=\"";
    auto at = h.find(needle, from);
    if (at == std::string::npos || at >= limit) return "";
    auto start = at + needle.size();
    auto end = h.find('"', start);
    if (end == std::string::npos || end > limit) return "";
    return h.substr(start, end - start);
}

// Texto de un elemento localizado por el comienzo de su etiqueta de apertura.
// `openPrefix` puede traer ya el '>' de cierre o quedarse a medias en un
// atributo. Corta en el cierre de esa misma etiqueta, asi que si el elemento
// lleva hijos hay que pasar el resultado por stripTags.
std::string rawTagText(std::string const& h, std::size_t from, std::size_t limit, std::string_view openPrefix) {
    if (openPrefix.size() < 2 || openPrefix.front() != '<') return "";
    auto at = h.find(openPrefix, from);
    if (at == std::string::npos || at >= limit) return "";

    std::size_t nameEnd = 1;
    while (nameEnd < openPrefix.size() &&
           std::isalnum(static_cast<unsigned char>(openPrefix[nameEnd]))) {
        ++nameEnd;
    }
    if (nameEnd == 1) return "";
    std::string closeTag = "</" + std::string(openPrefix.substr(1, nameEnd - 1)) + ">";

    std::size_t start = at + openPrefix.size();
    if (openPrefix.back() != '>') {
        auto gt = h.find('>', start);
        if (gt == std::string::npos || gt > limit) return "";
        start = gt + 1;
    }

    auto end = h.find(closeTag, start);
    if (end == std::string::npos || end > limit) return "";
    return h.substr(start, end - start);
}

std::string absolute(char const* base, std::string const& path) {
    if (path.empty()) return "";
    if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) return path;
    if (path.front() == '/') return std::string(base) + path;
    return std::string(base) + "/" + path;
}

// Los .ani traen varios fotogramas; el resto son estaticos.
bool looksAnimated(std::string const& url) {
    auto end = url.find_last_of("?#");
    auto path = end == std::string::npos ? url : url.substr(0, end);
    if (path.size() < 4) return false;
    return geode::utils::string::toLower(path.substr(path.size() - 4)) == ".ani";
}

// rw-designer marca el rol de cada cursor con una clase; los que tienen
// equivalente en el mod se sugieren solos.
bool suggestedForRwRole(std::string_view role, CursorState& out) {
    if (role == "curarrow") { out = CursorState::Idle;     return true; }
    if (role == "curlink")  { out = CursorState::Hover;    return true; }
    if (role == "curtext")  { out = CursorState::Text;     return true; }
    if (role == "curunav")  { out = CursorState::Disabled; return true; }
    if (role == "curmove")  { out = CursorState::Move;     return true; }
    return false;
}

// Ultima pagina del paginador de rw-designer, deducida del mayor offset enlazado.
int rwPageCount(std::string const& html, std::string_view prefix, int step) {
    std::string needle = "href=\"/cursor-library/";
    needle += prefix;
    int maxOffset = 0;
    std::size_t at = 0;
    while ((at = html.find(needle, at)) != std::string::npos) {
        std::size_t i = at + needle.size();
        std::size_t digitsFrom = i;
        int value = 0;
        while (i < html.size() && html[i] >= '0' && html[i] <= '9' && value < 100000000) {
            value = value * 10 + (html[i] - '0');
            ++i;
        }
        if (i > digitsFrom && i < html.size() && html[i] == '"') {
            maxOffset = std::max(maxOffset, value);
        }
        at = i;
    }
    return std::max(1, maxOffset / step + 1);
}

ListingPage parseRwSets(std::string const& html, int page) {
    ListingPage out;
    out.page = page;
    out.pageCount = rwPageCount(html, "set-", kRwSetsPerPage);

    std::string_view const anchor = "<a class=\"item\" href=\"/cursor-set/";
    std::size_t at = 0;
    while ((at = html.find(anchor, at)) != std::string::npos && out.items.size() < kMaxListingItems) {
        auto slugStart = at + anchor.size();
        auto slugEnd = html.find('"', slugStart);
        if (slugEnd == std::string::npos) break;
        at = slugEnd;

        auto limit = html.find(anchor, slugEnd);
        if (limit == std::string::npos) limit = html.size();

        Listing item;
        item.store = Store::RwDesigner;
        item.id = html.substr(slugStart, slugEnd - slugStart);
        if (item.id.empty()) continue;

        item.name = cleanLabel(rawTagText(html, slugEnd, limit, "<span class=\"setname\">"));
        if (item.name.empty()) item.name = cleanLabel(item.id);
        item.author = cleanLabel(stripTags(rawTagText(html, slugEnd, limit, "<a class=\"name\"")), 24);
        item.extra = cleanLabel(rawTagText(html, slugEnd, limit, "<span class=\"downloads\""), 12);
        item.thumbUrl = std::string(kRwBase) + "/cursor-teaser/" + item.id + ".png";
        out.items.push_back(std::move(item));
    }
    return out;
}

ListingPage parseRwLoose(std::string const& html, int page) {
    ListingPage out;
    out.page = page;
    out.pageCount = rwPageCount(html, "", kRwLoosePerPage);

    std::string_view const anchor = "<div id=\"cellcu";
    std::size_t at = 0;
    while ((at = html.find(anchor, at)) != std::string::npos && out.items.size() < kMaxListingItems) {
        auto idStart = at + anchor.size();
        auto idEnd = html.find('"', idStart);
        if (idEnd == std::string::npos) break;
        at = idEnd;

        auto limit = html.find(anchor, idEnd);
        if (limit == std::string::npos) limit = html.size();

        auto id = html.substr(idStart, idEnd - idStart);
        if (id.empty()) continue;

        auto href = rawAttr(html, idEnd, limit, "href");
        if (href.rfind("/cursor-download/", 0) != 0) continue;

        Listing item;
        item.store = Store::RwDesigner;
        item.id = id;
        item.single = true;
        item.directUrl = absolute(kRwBase, href);
        item.animated = looksAnimated(href);
        item.thumbUrl = std::string(kRwBase) + "/cursor-view/" + id + ".png";

        auto alt = rawAttr(html, idEnd, limit, "alt");
        constexpr std::string_view kPreviewSuffix = " Preview";
        if (alt.size() > kPreviewSuffix.size() &&
            alt.compare(alt.size() - kPreviewSuffix.size(), kPreviewSuffix.size(), kPreviewSuffix) == 0) {
            alt.resize(alt.size() - kPreviewSuffix.size());
        }
        item.name = cleanLabel(alt, 30);
        if (item.name.empty()) item.name = "cursor" + id;
        out.items.push_back(std::move(item));
    }
    return out;
}

Detail parseRwDetail(std::string const& html, std::string const& slug) {
    Detail out;
    out.name = cleanLabel(stripTags(rawTagText(html, 0, html.size(), "<h1")));
    if (out.name.empty()) out.name = cleanLabel(slug);
    out.author = cleanLabel(stripTags(rawTagText(html, 0, html.size(), "<a class=\"name\"")), 24);

    auto ratingAt = html.find("class=\"rating\"");
    if (ratingAt != std::string::npos) {
        out.description = cleanLabel(stripTags(rawTagText(html, ratingAt, html.size(), "<p>")), 140);
    }

    std::string_view const anchor = "<div id=\"cellcu";
    std::size_t at = 0;
    while ((at = html.find(anchor, at)) != std::string::npos) {
        auto idStart = at + anchor.size();
        auto idEnd = html.find('"', idStart);
        if (idEnd == std::string::npos) break;
        at = idEnd;

        auto limit = html.find(anchor, idEnd);
        if (limit == std::string::npos) limit = html.size();

        auto id = html.substr(idStart, idEnd - idStart);
        auto href = rawAttr(html, idEnd, limit, "href");
        if (id.empty() || href.rfind("/cursor-download/", 0) != 0) continue;

        DetailCursor cursor;
        cursor.downloadUrl = absolute(kRwBase, href);
        cursor.animated = looksAnimated(href);
        cursor.previewUrl = std::string(kRwBase) + "/cursor-view/" + id + ".png";

        auto klass = rawAttr(html, idEnd, limit, "class");
        auto space = klass.rfind(' ');
        if (space != std::string::npos) {
            cursor.hasSuggested = suggestedForRwRole(
                std::string_view(klass).substr(space + 1), cursor.suggested);
        }

        auto alt = rawAttr(html, idEnd, limit, "alt");
        constexpr std::string_view kPreviewSuffix = " Preview";
        if (alt.size() > kPreviewSuffix.size() &&
            alt.compare(alt.size() - kPreviewSuffix.size(), kPreviewSuffix.size(), kPreviewSuffix) == 0) {
            alt.resize(alt.size() - kPreviewSuffix.size());
        }
        cursor.name = cleanLabel(alt, 26);
        if (cursor.name.empty()) cursor.name = "cursor" + id;
        out.cursors.push_back(std::move(cursor));
    }
    return out;
}

// "Showing items 1-50 of 1369 items..." o "Showing all 44 items...".
int rwSearchPageCount(std::string const& html) {
    auto at = html.find("Showing items");
    if (at == std::string::npos) return 1;
    auto ofAt = html.find(" of ", at);
    if (ofAt == std::string::npos) return 1;

    long long total = 0;
    for (std::size_t i = ofAt + 4; i < html.size() && html[i] >= '0' && html[i] <= '9'; ++i) {
        total = total * 10 + (html[i] - '0');
        if (total > 10000000) break;
    }
    if (total <= 0) return 1;

    auto pages = static_cast<int>((total + kRwSearchPerPage - 1) / kRwSearchPerPage);
    return std::clamp(pages, 1, kMaxSearchPages);
}

// Los resultados del buscador mezclan sets, cursores sueltos y sets de iconos;
// estos ultimos se descartan.
ListingPage parseRwMixed(std::string const& html, int page) {
    ListingPage out;
    out.page = page;
    out.pageCount = rwSearchPageCount(html);

    constexpr std::string_view kBlock  = "class=\"itemteaser";
    constexpr std::string_view kSetRef = "<a class=\"item\" href=\"/cursor-set/";
    constexpr std::string_view kDlRef  = "<a class=\"download\" href=\"/cursor-download/";
    constexpr std::string_view kPreviewSuffix = " Preview";

    std::size_t at = 0;
    while ((at = html.find(kBlock, at)) != std::string::npos && out.items.size() < kMaxListingItems) {
        auto blockStart = at + kBlock.size();
        at = blockStart;

        auto limit = html.find(kBlock, blockStart);
        if (limit == std::string::npos) limit = html.size();

        Listing item;
        item.store = Store::RwDesigner;

        auto setAt = html.find(kSetRef, blockStart);
        if (setAt != std::string::npos && setAt < limit) {
            auto slugStart = setAt + kSetRef.size();
            auto slugEnd = html.find('"', slugStart);
            if (slugEnd == std::string::npos || slugEnd > limit) continue;

            item.id = html.substr(slugStart, slugEnd - slugStart);
            if (item.id.empty()) continue;
            item.name = cleanLabel(rawTagText(html, slugEnd, limit, "<span class=\"setname\">"));
            if (item.name.empty()) item.name = cleanLabel(item.id);
            item.author = cleanLabel(stripTags(rawTagText(html, slugEnd, limit, "<a class=\"name\"")), 24);
            item.extra = cleanLabel(rawTagText(html, slugEnd, limit, "<span class=\"downloads\""), 12);
            item.thumbUrl = std::string(kRwBase) + "/cursor-teaser/" + item.id + ".png";
            out.items.push_back(std::move(item));
            continue;
        }

        // Sin enlace de descarga no es un cursor (sets de iconos, por ejemplo).
        auto dlAt = html.find(kDlRef, blockStart);
        if (dlAt == std::string::npos || dlAt >= limit) continue;

        auto hrefStart = dlAt + std::string_view("<a class=\"download\" href=\"").size();
        auto hrefEnd = html.find('"', hrefStart);
        if (hrefEnd == std::string::npos || hrefEnd > limit) continue;
        auto href = html.substr(hrefStart, hrefEnd - hrefStart);

        // /cursor-download/{id}/{fichero}
        auto idStart = std::string_view("/cursor-download/").size();
        auto idEnd = href.find('/', idStart);
        if (idEnd == std::string::npos) continue;
        auto id = href.substr(idStart, idEnd - idStart);
        if (id.empty()) continue;

        item.id = id;
        item.single = true;
        item.directUrl = absolute(kRwBase, href);
        item.animated = looksAnimated(href);
        item.thumbUrl = std::string(kRwBase) + "/cursor-view/" + id + ".png";

        auto alt = rawAttr(html, hrefEnd, limit, "alt");
        if (alt.size() > kPreviewSuffix.size() &&
            alt.compare(alt.size() - kPreviewSuffix.size(), kPreviewSuffix.size(), kPreviewSuffix) == 0) {
            alt.resize(alt.size() - kPreviewSuffix.size());
        }
        item.name = cleanLabel(alt, 30);
        if (item.name.empty()) item.name = "cursor" + id;
        out.items.push_back(std::move(item));
    }
    return out;
}

std::string urlEncode(std::string const& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0f]);
        }
    }
    return out;
}

ListingPage parseCcPacks(std::string const& html, int page) {
    ListingPage out;
    out.page = page;
    out.pageCount = 1;

    std::string_view const anchor = "<h3 class=\"item-name\">";
    std::size_t at = 0;
    while ((at = html.find(anchor, at)) != std::string::npos && out.items.size() < kMaxListingItems) {
        auto blockStart = at + anchor.size();
        at = blockStart;

        auto limit = html.find(anchor, blockStart);
        if (limit == std::string::npos) limit = html.size();

        auto href = rawAttr(html, blockStart, limit, "href");
        if (href.rfind("/en/collection/", 0) != 0) continue;

        Listing item;
        item.store = Store::CustomCursor;
        item.id = href;
        item.name = cleanLabel(stripTags(rawTagText(html, blockStart, limit, "<a ")));
        if (item.name.empty()) continue;

        auto imgAt = html.find("class=\"item-img\"", blockStart);
        if (imgAt != std::string::npos && imgAt < limit) {
            item.thumbUrl = rawAttr(html, imgAt, limit, "src");
        }
        out.items.push_back(std::move(item));
    }
    return out;
}

std::vector<Category> parseCcCollections(std::string const& html) {
    std::vector<Category> out;
    std::string_view const anchor = "<a class=\"c-img\" href=\"";
    std::size_t at = 0;
    while ((at = html.find(anchor, at)) != std::string::npos) {
        auto hrefStart = at + anchor.size();
        auto hrefEnd = html.find('"', hrefStart);
        if (hrefEnd == std::string::npos) break;
        at = hrefEnd;

        auto limit = html.find(anchor, hrefEnd);
        if (limit == std::string::npos) limit = html.size();

        auto href = html.substr(hrefStart, hrefEnd - hrefStart);
        if (href.rfind("/en/collection/", 0) != 0) continue;

        Category cat;
        cat.id = href;
        cat.name = cleanLabel(rawTagText(html, hrefEnd, limit, "<h3 class=\"c-name\">"), 26);
        if (cat.name.empty()) cat.name = cleanLabel(href.substr(std::string_view("/en/collection/").size()), 26);
        out.push_back(std::move(cat));
    }
    return out;
}

// custom-cursor sirve algunas artes de pack tambien a tamaño completo, quitando
// el segmento /32/ de la ruta.
std::string ccLargeVariant(std::string const& url) {
    if (url.find("/db/") == std::string::npos) return "";
    auto at = url.find("/32/");
    if (at == std::string::npos) return "";
    return url.substr(0, at) + "/" + url.substr(at + 4);
}

// El bloque de demostracion pinta la flecha en el contenedor y el puntero en el
// boton de dentro, asi que el orden en el HTML da el rol de cada imagen.
Detail parseCcDetail(std::string const& html, std::string const& fallbackName) {
    Detail out;
    out.name = cleanLabel(stripTags(rawTagText(html, 0, html.size(), "<h1")));
    if (out.name.empty()) out.name = fallbackName;
    out.description = cleanLabel(
        stripTags(rawTagText(html, 0, html.size(), "<div class=\"single-content-text\">")), 140);

    auto blockAt = html.find("class=\"single-content-b\"");
    if (blockAt == std::string::npos) return out;
    auto blockEnd = html.find("class=\"single-b-c\"", blockAt);
    if (blockEnd == std::string::npos) blockEnd = html.size();

    struct Slot {
        char const* name;
        CursorState state;
    };
    constexpr Slot kSlots[] = {
        {"Flecha",  CursorState::Idle},
        {"Puntero", CursorState::Hover},
    };

    std::size_t at = blockAt;
    for (auto const& slot : kSlots) {
        auto urlAt = html.find("cursor: url(", at);
        if (urlAt == std::string::npos || urlAt >= blockEnd) break;
        auto urlStart = urlAt + std::string_view("cursor: url(").size();
        auto urlEnd = html.find(')', urlStart);
        if (urlEnd == std::string::npos || urlEnd > blockEnd) break;
        at = urlEnd;

        auto url = htmlDecode(html.substr(urlStart, urlEnd - urlStart));
        if (url.empty()) continue;

        DetailCursor cursor;
        cursor.name = slot.name;
        cursor.suggested = slot.state;
        cursor.hasSuggested = true;
        cursor.previewUrl = url;

        auto large = ccLargeVariant(url);
        if (large.empty()) {
            cursor.downloadUrl = url;
        } else {
            cursor.downloadUrl = large;
            cursor.fallbackUrl = url;
        }
        out.cursors.push_back(std::move(cursor));
    }
    return out;
}

void fetchHtml(std::string const& url, int timeoutSeconds,
               geode::CopyableFunction<void(Result<std::string>)> cb) {
    WebHelper::dispatch(makeRequest(timeoutSeconds), "GET", url,
                        [cb, url](web::WebResponse res) mutable {
        if (!res.ok()) {
            cb(Err("La tienda respondio {}", res.code()));
            return;
        }
        auto body = res.string().unwrapOr("");
        if (body.empty()) {
            cb(Err("Respuesta vacia"));
            return;
        }
        if (body.size() > kMaxHtmlBytes) {
            cb(Err("Respuesta demasiado grande"));
            return;
        }
        cb(Ok(std::move(body)));
    });
}

} // namespace

char const* ShopClient::storeName(Store store) {
    switch (store) {
        case Store::CustomCursor: return "Custom-Cursor";
        case Store::RwDesigner:
        default:                  return "RW-Designer";
    }
}

char const* ShopClient::storeCredit(Store store) {
    switch (store) {
        case Store::CustomCursor: return "custom-cursor.com";
        case Store::RwDesigner:
        default:                  return "rw-designer.com";
    }
}

std::vector<Category> ShopClient::builtinCategories(Store store) {
    if (store == Store::RwDesigner) {
        return {
            {"sets",  "Sets destacados",  true, kRwSetsPerPage},
            {"loose", "Cursores sueltos", true, kRwLoosePerPage},
        };
    }
    return {
        {"/en/packs", "Novedades", false},
        {"/en/tops",  "Mas populares", false},
        {"/en/collection/geometry-dash",           "Geometry Dash", false},
        {"/en/collection/games",                   "Videojuegos", false},
        {"/en/collection/minecraft",               "Minecraft", false},
        {"/en/collection/among-us",                "Among Us", false},
        {"/en/collection/roblox",                  "Roblox", false},
        {"/en/collection/anime",                   "Anime", false},
        {"/en/collection/cartoons",                "Dibujos", false},
        {"/en/collection/pokemon",                 "Pokemon", false},
        {"/en/collection/super-mario",             "Mario", false},
        {"/en/collection/five-nights-at-freddys",  "FNAF", false},
        {"/en/collection/undertale-and-deltarune", "Undertale", false},
        {"/en/collection/animals",                 "Animales", false},
        {"/en/collection/sport",                   "Deportes", false},
        {"/en/collection/holidays",                "Fiestas", false},
        {"/en/collection/hello-kitty",             "Hello Kitty", false},
        {"/en/collection/harry-potter",            "Harry Potter", false},
    };
}

void ShopClient::fetchCategories(Store store, CategoryCallback cb) {
    auto builtin = builtinCategories(store);
    if (store != Store::CustomCursor) {
        cb(std::move(builtin));
        return;
    }

    fetchHtml(std::string(kCcBase) + "/en/collections", 20,
              [cb, builtin](Result<std::string> res) mutable {
        if (!res) {
            cb(std::move(builtin));
            return;
        }
        for (auto& extra : parseCcCollections(res.unwrap())) {
            auto known = std::find_if(builtin.begin(), builtin.end(),
                [&](Category const& c) { return c.id == extra.id; });
            if (known == builtin.end()) builtin.push_back(std::move(extra));
        }
        cb(std::move(builtin));
    });
}

bool ShopClient::supportsSearch(Store store) {
    return store == Store::RwDesigner;
}

Category ShopClient::searchCategory(Store store, std::string const& query) {
    Category category;
    category.id = std::string(kSearchPrefix) + query;
    category.name = "Resultados: " + query;
    category.paged = supportsSearch(store);
    category.fetchSize = category.paged ? kRwSearchPerPage : 0;
    return category;
}

void ShopClient::fetchListing(Store store, Category const& category, int page, ListingCallback cb) {
    int safePage = std::max(0, page);

    if (category.id.rfind(kSearchPrefix, 0) == 0) {
        auto query = category.id.substr(kSearchPrefix.size());
        if (!supportsSearch(store) || query.empty()) {
            cb(Err("Esta tienda no tiene buscador"));
            return;
        }

        // La primera pagina sale por /cursor-library, que filtra a cursores; el
        // paginador de la web salta a /gallery y ahi hay que añadir "cursors"
        // para que no se cuele de todo.
        auto url = safePage == 0
            ? fmt::format("{}/cursor-library?search={}", kRwBase, urlEncode(query))
            : fmt::format("{}/gallery?search={}&page={}", kRwBase,
                          urlEncode(query + " cursors"), safePage + 1);

        fetchHtml(url, 25, [cb, safePage](Result<std::string> res) mutable {
            if (!res) {
                cb(Err("{}", res.unwrapErr()));
                return;
            }
            auto parsed = parseRwMixed(res.unwrap(), safePage);
            if (parsed.items.empty()) {
                cb(Err("Sin resultados"));
                return;
            }
            cb(Ok(std::move(parsed)));
        });
        return;
    }

    if (store == Store::RwDesigner) {
        bool loose = category.id == "loose";
        int step = loose ? kRwLoosePerPage : kRwSetsPerPage;
        auto url = fmt::format("{}/cursor-library/{}{}", kRwBase, loose ? "" : "set-", safePage * step);

        fetchHtml(url, 25, [cb, loose, safePage](Result<std::string> res) mutable {
            if (!res) {
                cb(Err("{}", res.unwrapErr()));
                return;
            }
            auto html = res.unwrap();
            auto parsed = loose ? parseRwLoose(html, safePage) : parseRwSets(html, safePage);
            if (parsed.items.empty()) {
                cb(Err("rw-designer no devolvio cursores"));
                return;
            }
            cb(Ok(std::move(parsed)));
        });
        return;
    }

    auto url = std::string(kCcBase) + category.id;
    fetchHtml(url, 25, [cb](Result<std::string> res) mutable {
        if (!res) {
            cb(Err("{}", res.unwrapErr()));
            return;
        }
        auto parsed = parseCcPacks(res.unwrap(), 0);
        if (parsed.items.empty()) {
            cb(Err("custom-cursor no devolvio packs"));
            return;
        }
        cb(Ok(std::move(parsed)));
    });
}

void ShopClient::fetchDetail(Listing const& listing, DetailCallback cb) {
    // Un cursor suelto ya trae todo lo que hace falta.
    if (listing.single && !listing.directUrl.empty()) {
        Detail detail;
        detail.name = listing.name;
        DetailCursor cursor;
        cursor.name = listing.name;
        cursor.previewUrl = listing.thumbUrl;
        cursor.downloadUrl = listing.directUrl;
        cursor.animated = listing.animated;
        detail.cursors.push_back(std::move(cursor));
        cb(Ok(std::move(detail)));
        return;
    }

    if (listing.store == Store::RwDesigner) {
        auto slug = listing.id;
        fetchHtml(std::string(kRwBase) + "/cursor-set/" + slug, 25,
                  [cb, slug](Result<std::string> res) mutable {
            if (!res) {
                cb(Err("{}", res.unwrapErr()));
                return;
            }
            auto detail = parseRwDetail(res.unwrap(), slug);
            if (detail.cursors.empty()) {
                cb(Err("Ese set no trae cursores descargables"));
                return;
            }
            cb(Ok(std::move(detail)));
        });
        return;
    }

    auto name = listing.name;
    fetchHtml(std::string(kCcBase) + listing.id, 25,
              [cb, name](Result<std::string> res) mutable {
        if (!res) {
            cb(Err("{}", res.unwrapErr()));
            return;
        }
        auto detail = parseCcDetail(res.unwrap(), name);
        if (detail.cursors.empty()) {
            cb(Err("Ese pack no trae imagenes"));
            return;
        }
        cb(Ok(std::move(detail)));
    });
}

void ShopClient::download(std::string const& url, BytesCallback cb) {
    download(url, "", std::move(cb));
}

void ShopClient::download(std::string const& url, std::string const& fallbackUrl, BytesCallback cb) {
    WebHelper::dispatch(makeRequest(40), "GET", url,
                        [cb, fallbackUrl](web::WebResponse res) mutable {
        auto failed = !res.ok();
        auto data = failed ? ByteVector{} : res.data();
        if (!failed && !data.empty() && data.size() <= kMaxDownloadBytes) {
            cb(Ok(std::vector<std::uint8_t>(data.begin(), data.end())));
            return;
        }
        if (!failed && data.size() > kMaxDownloadBytes) {
            cb(Err("El archivo pesa demasiado"));
            return;
        }
        if (fallbackUrl.empty()) {
            cb(Err("La descarga fallo ({})", res.code()));
            return;
        }
        ShopClient::download(fallbackUrl, "", cb);
    });
}

std::string ShopClient::filenameFor(std::string const& url, std::string const& fallbackStem) {
    auto slash = url.find_last_of('/');
    auto name = slash == std::string::npos ? url : url.substr(slash + 1);
    auto query = name.find('?');
    if (query != std::string::npos) name.resize(query);

    // Los enlaces de rw-designer traen los espacios como %20.
    std::string decoded;
    decoded.reserve(name.size());
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '%' && i + 2 < name.size() &&
            std::isxdigit(static_cast<unsigned char>(name[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(name[i + 2]))) {
            decoded.push_back(static_cast<char>(std::stoi(name.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            decoded.push_back(name[i]);
        }
    }
    name = std::move(decoded);

    auto dot = name.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < name.size() && name.size() - dot <= 6) {
        auto stem = cleanLabel(name.substr(0, dot), 40);
        if (!stem.empty()) return stem + name.substr(dot);
        return fallbackStem + name.substr(dot);
    }
    return fallbackStem + ".png";
}

} // namespace paimon::cursorshop
