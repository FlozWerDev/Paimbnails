#include "QuickHubButtonCapture.hpp"

#include "QuickHubManager.hpp"
#include "../ui/QuickButtonPopup.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CreatorLayer.hpp>
#include <Geode/binding/GJGarageLayer.hpp>
#include <Geode/binding/GauntletSelectLayer.hpp>
#include <Geode/binding/LevelSelectLayer.hpp>
#include <Geode/binding/MenuLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <typeinfo>

#ifndef _WIN32
#include <cxxabi.h>
#endif

using namespace geode::prelude;

namespace paimon::quickhub {
namespace {

// -------------------------------------------------------------------------
// Identificacion de nodos
// -------------------------------------------------------------------------

std::string classNameOf(CCObject* obj) {
    if (!obj) return {};
#ifdef _WIN32
    std::string name = typeid(*obj).name();
    if (name.rfind("class ", 0) == 0) name.erase(0, 6);
    if (name.rfind("struct ", 0) == 0) name.erase(0, 7);
#else
    int status = 0;
    std::string name;
    if (char* demangled = abi::__cxa_demangle(typeid(*obj).name(), nullptr, nullptr, &status);
        status == 0 && demangled) {
        name = demangled;
        free(demangled);
    } else {
        name = typeid(*obj).name();
    }
#endif
    // Geode's $modify genera clases derivadas; quedarse con el nombre base.
    if (auto colons = name.rfind("::"); colons != std::string::npos) {
        name.erase(0, colons + 2);
    }
    return name;
}

// CCMenu y los contenedores de scroll heredan de CCLayer, asi que hay que
// descartarlos explicitamente: no son pantallas, estan dentro de una.
bool isGenericLayerClass(std::string const& cls) {
    return cls.empty() || cls == "CCLayer" || cls == "CCLayerColor" ||
           cls == "CCLayerRGBA" || cls == "CCLayerGradient" || cls == "CCNode" ||
           cls == "CCScene" || cls == "CCMenu" || cls == "ScrollLayer" ||
           cls == "ContentLayer" || cls == "CCScrollLayerExt" || cls == "CCContentLayer" ||
           cls == "BoomScrollLayer" || cls == "ListLayer" || cls == "TableView";
}

// Capa de GD que contiene el boton (MenuLayer, LevelInfoLayer, un popup...).
// Se coge la mas externa: la de dentro suele ser el cuerpo de un scroll, y la
// pantalla de verdad esta arriba del todo.
std::string ownerLayerClass(CCNode* node) {
    std::string outermost;
    for (auto* current = node; current; current = current->getParent()) {
        if (!typeinfo_cast<CCLayer*>(current)) continue;
        auto cls = classNameOf(current);
        if (!isGenericLayerClass(cls)) outermost = std::move(cls);
    }
    return outermost;
}

// Capa principal de la escena: el primer CCLayer con clase propia.
std::string sceneLayerClass(CCScene* scene) {
    if (!scene) return {};
    auto* children = scene->getChildren();
    if (!children) return {};
    for (unsigned int i = 0; i < children->count(); ++i) {
        auto* child = typeinfo_cast<CCLayer*>(children->objectAtIndex(i));
        if (!child) continue;
        auto cls = classNameOf(child);
        if (!isGenericLayerClass(cls)) return cls;
    }
    return {};
}

// Los primeros botones capturados guardaron "CCMenu" como pantalla. Ignorar esos
// valores evita que el filtro por capa los descarte para siempre.
std::string usableOwnerClass(CustomQuickButton const& def) {
    return isGenericLayerClass(def.ownerClass) ? std::string() : def.ownerClass;
}

std::string currentSceneClass() {
    auto* director = CCDirector::get();
    return sceneLayerClass(director ? director->getRunningScene() : nullptr);
}

// -------------------------------------------------------------------------
// Iconos
// -------------------------------------------------------------------------

// Varios nombres pueden apuntar al mismo recorte del atlas. Elegir siempre el
// mismo (mas corto, luego alfabetico) evita que el icono cambie entre partidas.
bool betterFrameName(std::string_view candidate, std::string_view current) {
    if (current.empty()) return true;
    bool candGD = candidate.ends_with("_001.png");
    bool currGD = current.ends_with("_001.png");
    if (candGD != currGD) return candGD;
    if (candidate.size() != current.size()) return candidate.size() < current.size();
    return candidate < current;
}

// Indice inverso recorte -> nombre. Resolver un sprite recorriendo el cache de
// frames cuesta una pasada entera; durante un barrido de escena eso se hace
// cientos de veces, asi que se construye la tabla una vez y se reusa. Se
// reconstruye cuando el juego carga hojas nuevas.
struct FrameKey {
    CCTexture2D* texture = nullptr;
    int x = 0, y = 0, w = 0, h = 0;
    bool rotated = false;

    bool operator<(FrameKey const& other) const {
        return std::tie(texture, x, y, w, h, rotated) <
               std::tie(other.texture, other.x, other.y, other.w, other.h, other.rotated);
    }
};

FrameKey keyForFrame(CCSpriteFrame* frame) {
    auto rect = frame->getRect();
    return {
        frame->getTexture(),
        static_cast<int>(std::lround(rect.origin.x)),
        static_cast<int>(std::lround(rect.origin.y)),
        static_cast<int>(std::lround(rect.size.width)),
        static_cast<int>(std::lround(rect.size.height)),
        frame->isRotated(),
    };
}

std::map<FrameKey, std::string> const& frameNameIndex() {
    static std::map<FrameKey, std::string> index;
    static unsigned int builtFrom = 0;

    auto* cache = CCSpriteFrameCache::sharedSpriteFrameCache();
    auto* frames = cache ? cache->m_pSpriteFrames : nullptr;
    if (!frames) return index;
    if (!index.empty() && frames->count() == builtFrom) return index;

    index.clear();
    builtFrom = frames->count();
    for (auto [name, frame] : frames->asExt<std::string_view, CCSpriteFrame>()) {
        if (!frame) continue;
        auto& slot = index[keyForFrame(frame)];
        if (betterFrameName(name, slot)) slot = std::string(name);
    }
    return index;
}

std::string frameNameForSprite(CCSprite* sprite) {
    if (!sprite) return {};
    auto* displayed = sprite->displayFrame();
    if (!displayed || !displayed->getTexture()) return {};

    auto const& index = frameNameIndex();
    auto found = index.find(keyForFrame(displayed));
    return found == index.end() ? std::string() : found->second;
}

// El sprite propio manda; si no resuelve, gana el hijo visualmente dominante.
// Asi un CCMenuItemSpriteExtra devuelve su icono real en vez de una esquina
// suelta del nine-slice de su fondo.
void collectIconCandidate(CCNode* node, std::string& bestName, float& bestArea) {
    if (!node || typeinfo_cast<CCLabelBMFont*>(node)) return;

    if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
        auto size = sprite->getContentSize();
        float area = size.width * size.height;
        if (area > 36.f && area > bestArea) {
            if (auto name = frameNameForSprite(sprite); !name.empty()) {
                bestName = std::move(name);
                bestArea = area;
            }
        }
    }

    if (auto* children = node->getChildren()) {
        for (unsigned int i = 0; i < children->count(); ++i) {
            collectIconCandidate(typeinfo_cast<CCNode*>(children->objectAtIndex(i)), bestName, bestArea);
        }
    }
}

std::string findIconFrame(CCNode* node) {
    if (!node) return {};
    if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
        if (auto own = frameNameForSprite(sprite); !own.empty()) return own;
    }
    std::string best;
    float bestArea = 0.f;
    collectIconCandidate(node, best, bestArea);
    return best;
}

std::string iconFrameOfItem(CCMenuItem* item) {
    auto* spriteItem = typeinfo_cast<CCMenuItemSprite*>(item);
    return spriteItem ? findIconFrame(spriteItem->getNormalImage()) : std::string();
}

std::string findLabelText(CCNode* node) {
    if (!node) return {};
    if (auto* bm = typeinfo_cast<CCLabelBMFont*>(node)) {
        if (auto const* s = bm->getString()) {
            if (std::string str = s; !str.empty()) return str;
        }
    }
    if (auto* ttf = typeinfo_cast<CCLabelTTF*>(node)) {
        if (auto const* s = ttf->getString()) {
            if (std::string str = s; !str.empty()) return str;
        }
    }
    if (auto* children = node->getChildren()) {
        for (unsigned int i = 0; i < children->count(); ++i) {
            auto text = findLabelText(typeinfo_cast<CCNode*>(children->objectAtIndex(i)));
            if (!text.empty()) return text;
        }
    }
    return {};
}

// -------------------------------------------------------------------------
// Rutas hasta el nodo
// -------------------------------------------------------------------------

bool isActuallyVisible(CCNode* node) {
    for (auto* current = node; current; current = current->getParent()) {
        if (!current->isVisible()) return false;
    }
    return true;
}

// Un CCMenu deshabilitado ignora los toques de todos sus hijos.
bool isInteractable(CCMenuItem* item) {
    if (!item || !item->isEnabled()) return false;
    if (!isActuallyVisible(item)) return false;
    for (auto* current = item->getParent(); current; current = current->getParent()) {
        if (auto* menu = typeinfo_cast<CCMenu*>(current)) {
            if (!menu->isEnabled()) return false;
        }
    }
    return true;
}

std::vector<int> makeNodePath(CCNode* node, CCScene* scene) {
    std::vector<int> reversePath;
    for (auto* current = node; current && current != scene; current = current->getParent()) {
        auto* parent = current->getParent();
        auto* children = parent ? parent->getChildren() : nullptr;
        if (!children) return {};
        unsigned int index = children->indexOfObject(current);
        if (index == CC_INVALID_INDEX) return {};
        reversePath.push_back(static_cast<int>(index));
    }
    std::ranges::reverse(reversePath);
    return reversePath;
}

// Ruta de node ids. Los niveles sin id quedan vacios y actuan de comodin, asi
// que otros mods pueden insertar contenedores sin romper la ruta.
std::vector<std::string> makeIdPath(CCNode* node, CCScene* scene) {
    std::vector<std::string> reversePath;
    for (auto* current = node; current && current != scene; current = current->getParent()) {
        reversePath.push_back(current->getID());
    }
    std::ranges::reverse(reversePath);
    return reversePath;
}

CCNode* resolvePath(CCScene* scene, std::vector<int> const& path) {
    if (path.empty()) return nullptr;
    CCNode* current = scene;
    for (int index : path) {
        auto* children = current ? current->getChildren() : nullptr;
        if (!children || index < 0 || index >= static_cast<int>(children->count())) return nullptr;
        current = typeinfo_cast<CCNode*>(children->objectAtIndex(index));
    }
    return current;
}

bool isInteractable(CCMenuItem* item);

// Busca en profundidad el boton cuya cadena de ids coincide con idPath. Si el
// nodo que hay al final de una rama no sirve, sigue buscando por las demas.
CCMenuItem* resolveIdPath(CCNode* root, std::vector<std::string> const& path, size_t depth) {
    if (!root) return nullptr;
    if (depth >= path.size()) {
        auto* item = typeinfo_cast<CCMenuItem*>(root);
        return item && isInteractable(item) ? item : nullptr;
    }

    auto const& want = path[depth];
    auto* children = root->getChildren();
    if (!children) return nullptr;

    for (unsigned int i = 0; i < children->count(); ++i) {
        auto* child = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
        if (!child) continue;
        // Un tramo vacio en la ruta original acepta cualquier nodo intermedio.
        if (!want.empty() && child->getID() != want) continue;
        if (auto* found = resolveIdPath(child, path, depth + 1)) return found;
    }
    return nullptr;
}

CCPoint worldCenterOf(CCNode* node) {
    if (!node) return CCPointZero;
    auto size = node->getContentSize();
    return node->convertToWorldSpace(ccp(size.width * 0.5f, size.height * 0.5f));
}

CCPoint normalizedCenterOf(CCNode* node) {
    auto world = worldCenterOf(node);
    auto win = CCDirector::get()->getWinSize();
    if (win.width <= 0.f || win.height <= 0.f) return ccp(-1.f, -1.f);
    return ccp(world.x / win.width, world.y / win.height);
}

// -------------------------------------------------------------------------
// Localizar el boton bajo el cursor
// -------------------------------------------------------------------------

// El de mas arriba gana: los hijos se recorren al reves porque cocos los ordena
// por z-order, asi un boton de un popup manda sobre el de la capa de debajo.
CCMenuItem* findButtonAt(CCNode* node, CCPoint worldPoint) {
    if (!node || !node->isVisible()) return nullptr;

    if (auto* children = node->getChildren()) {
        for (int i = static_cast<int>(children->count()) - 1; i >= 0; --i) {
            auto* child = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
            if (auto* found = findButtonAt(child, worldPoint)) return found;
        }
    }

    auto* item = typeinfo_cast<CCMenuItem*>(node);
    if (!item || !isInteractable(item)) return nullptr;
    auto size = item->getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) return nullptr;
    auto local = item->convertToNodeSpace(worldPoint);
    return CCRect(0.f, 0.f, size.width, size.height).containsPoint(local) ? item : nullptr;
}

// -------------------------------------------------------------------------
// Reencontrar el boton guardado
// -------------------------------------------------------------------------

// Dos puntuaciones separadas. `identity` solo suma con senales que distinguen a
// ESTE boton de sus hermanos; `total` incluye ademas el contexto (pantalla,
// receptor del callback, menu padre), que comparten todos los botones de la
// misma capa. Exigir ambas evita activar el boton de al lado cuando el guardado
// ya no esta en pantalla.
constexpr long kMinIdentity = 140;
constexpr long kMinConfidence = 320;

struct Score {
    long identity = 0;
    long total = 0;

    bool accepted() const { return identity >= kMinIdentity && total >= kMinConfidence; }
};

Score evaluateCandidate(CCMenuItem* item, CustomQuickButton const& def) {
    Score score;
    auto addIdentity = [&](long points) { score.identity += points; score.total += points; };

    // Un boton no cambia de tipo de pantalla. Los node ids si se repiten entre
    // capas, asi que esto se descarta antes de puntuar nada.
    if (auto wanted = usableOwnerClass(def); !wanted.empty()) {
        auto owner = ownerLayerClass(item);
        if (owner.empty()) score.total -= 60;
        else if (owner != wanted) return score;
        else score.total += 180;
    }

    if (!def.targetNodeId.empty() && item->getID() == def.targetNodeId) addIdentity(900);

    if (!def.labelText.empty()) {
        auto text = findLabelText(item);
        if (!text.empty() && text == def.labelText) addIdentity(380);
    }

    if (!def.icon.empty() && iconFrameOfItem(item) == def.icon) addIdentity(200);
    if (def.tag != 0 && item->getTag() == def.tag) addIdentity(140);

    // La posicion desempata entre botones gemelos (flechas, filas de una lista).
    if (def.relX >= 0.f && def.relY >= 0.f) {
        auto rel = normalizedCenterOf(item);
        float dx = rel.x - def.relX;
        float dy = rel.y - def.relY;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 0.01f)      addIdentity(220);
        else if (dist < 0.04f) addIdentity(150);
        else if (dist < 0.12f) score.total += 70;
        else if (dist > 0.45f) score.total -= 90;
    }

    if (!def.listenerClass.empty()) {
        auto listener = classNameOf(item->m_pListener);
        if (!listener.empty() && listener == def.listenerClass) score.total += 260;
        else if (!listener.empty()) score.total -= 200; // otro receptor: otro boton
    }

    if (!def.itemClass.empty() && classNameOf(item) == def.itemClass) score.total += 90;
    if (!def.parentId.empty()) {
        auto* parent = item->getParent();
        if (parent && parent->getID() == def.parentId) score.total += 120;
    }

    return score;
}

// Senales que evaluateCandidate sabe puntuar. Sin ninguna, barrer la escena no
// puede dar un resultado fiable.
bool hasAnyIdentity(CustomQuickButton const& def) {
    return !def.targetNodeId.empty() || !def.labelText.empty() || !def.icon.empty() ||
           def.tag != 0 || (def.relX >= 0.f && def.relY >= 0.f);
}

struct ButtonMatch {
    CCMenuItem* item = nullptr;
    Score score;
};

void scanBestButton(CCNode* node, CustomQuickButton const& def, ButtonMatch& best) {
    if (!node || !node->isVisible()) return;

    if (auto* children = node->getChildren()) {
        for (unsigned int i = 0; i < children->count(); ++i) {
            scanBestButton(typeinfo_cast<CCNode*>(children->objectAtIndex(i)), def, best);
        }
    }

    auto* item = typeinfo_cast<CCMenuItem*>(node);
    if (!item || !isInteractable(item)) return;

    auto score = evaluateCandidate(item, def);
    if (!best.item || score.total > best.score.total) {
        best.score = score;
        best.item = item;
    }
}

CCMenuItem* locateButton(CustomQuickButton const& def) {
    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!scene) return nullptr;

    // 1. Ruta de ids: sobrevive a que otros mods anadan o quiten nodos. Solo
    // sirve si el propio boton tenia id; si no, la ruta es todo comodines y
    // caeria en el primer nodo que pillase.
    if (!def.idPath.empty() && !def.idPath.back().empty()) {
        if (auto* item = resolveIdPath(scene, def.idPath, 0)) {
            // Los ids se repiten entre pantallas; comprobar que la capa cuadra.
            auto wanted = usableOwnerClass(def);
            if (wanted.empty() || ownerLayerClass(item) == wanted) return item;
        }
    }

    // 2. Barrido completo puntuando toda la identidad guardada.
    if (hasAnyIdentity(def)) {
        ButtonMatch best;
        scanBestButton(scene, def, best);
        if (best.item && best.score.accepted()) return best.item;
    }

    // 3. Ultimo recurso: la ruta de indices original.
    if (auto* item = typeinfo_cast<CCMenuItem*>(resolvePath(scene, def.nodePath))) {
        if (isInteractable(item)) {
            // Sin identidad guardada no hay nada que verificar; con ella, exigir
            // que el nodo de esa posicion sea de verdad el mismo boton.
            if (!hasAnyIdentity(def) || evaluateCandidate(item, def).accepted()) return item;
        }
    }

    return nullptr;
}

// -------------------------------------------------------------------------
// Navegar a la pantalla donde vive el boton
// -------------------------------------------------------------------------

CCScene* buildSceneForClass(std::string const& cls) {
    if (cls == "MenuLayer")           return MenuLayer::scene(false);
    if (cls == "CreatorLayer")        return CreatorLayer::scene();
    if (cls == "GJGarageLayer")       return GJGarageLayer::scene();
    if (cls == "LevelSelectLayer")    return LevelSelectLayer::scene(0);
    if (cls == "GauntletSelectLayer") return GauntletSelectLayer::scene(0);
    return nullptr;
}

// Tras cambiar de escena el boton no existe hasta que la transicion termina, asi
// que se reintenta unos frames antes de rendirse.
class PendingActivation : public CCNode {
public:
    static void start(CustomQuickButton def) {
        auto* self = get();
        self->m_def = std::move(def);
        self->m_elapsed = 0.f;
        self->setTicking(true);
    }

    static void cancel() {
        get()->setTicking(false);
    }

private:
    static PendingActivation* get() {
        static PendingActivation* s_instance = nullptr;
        if (!s_instance) {
            s_instance = new PendingActivation();
            s_instance->init();
            s_instance->retain();
        }
        return s_instance;
    }

    void setTicking(bool on) {
        if (m_ticking == on) return;
        auto* director = CCDirector::get();
        auto* scheduler = director ? director->getScheduler() : nullptr;
        if (!scheduler) return;

        m_ticking = on;
        if (on) {
            scheduler->scheduleSelector(
                schedule_selector(PendingActivation::onUpdate), this, 0.f, false);
        } else {
            scheduler->unscheduleSelector(
                schedule_selector(PendingActivation::onUpdate), this);
        }
    }

    void onUpdate(float dt) {
        if (paimon::isRuntimeShuttingDown()) {
            setTicking(false);
            return;
        }

        m_elapsed += dt;

        if (auto* item = locateButton(m_def)) {
            setTicking(false);
            item->activate();
            return;
        }

        constexpr float kTimeout = 3.f;
        if (m_elapsed >= kTimeout) {
            setTicking(false);
            std::string message = fmt::format("No encontre \"{}\" en {}.", m_def.name,
                                              friendlyScreenName(m_def.sceneClass));
            PaimonNotify::create(message.c_str(), NotificationIcon::Warning)->show();
        }
    }

    CustomQuickButton m_def;
    float m_elapsed = 0.f;
    bool m_ticking = false;
};

std::string humanName(std::string value) {
    if (auto slash = value.rfind('/'); slash != std::string::npos) value.erase(0, slash + 1);
    if (value.ends_with(".png")) value.resize(value.size() - 4);
    for (char& ch : value) {
        if (ch == '-' || ch == '_') ch = ' ';
    }
    // Los ids de GD acaban en "_001"; sobra en un nombre visible.
    if (value.ends_with(" 001")) value.resize(value.size() - 4);
    while (!value.empty() && value.back() == ' ') value.pop_back();
    if (value.empty()) return "Boton rapido";
    value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
    return value;
}

} // namespace

bool handleQuickButtonRightClick() {
    if (QuickButtonPopup::isOpen()) return true;
    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!scene) return false;

    auto* button = findButtonAt(scene, geode::cocos::getMousePos());
    if (!button) return false;

    auto* spriteItem = typeinfo_cast<CCMenuItemSprite*>(button);
    std::string icon = spriteItem ? findIconFrame(spriteItem->getNormalImage()) : std::string();
    std::string nodeId = button->getID();
    std::string label = findLabelText(button);

    std::string suggestedSource = !label.empty() ? label
                                : !nodeId.empty() ? nodeId
                                                  : icon;

    CustomQuickButton candidate;
    candidate.name          = humanName(suggestedSource);
    candidate.id            = QuickHubManager::get().makeUniqueCustomId(candidate.name);
    candidate.targetNodeId  = std::move(nodeId);
    candidate.icon          = std::move(icon);
    candidate.labelText     = std::move(label);
    candidate.nodePath      = makeNodePath(button, scene);
    candidate.idPath        = makeIdPath(button, scene);
    candidate.tag           = button->getTag();
    candidate.itemClass     = classNameOf(button);
    candidate.listenerClass = classNameOf(button->m_pListener);
    candidate.ownerClass    = ownerLayerClass(button);
    candidate.sceneClass    = sceneLayerClass(scene);
    if (auto* parent = button->getParent()) candidate.parentId = parent->getID();

    auto rel = normalizedCenterOf(button);
    candidate.relX = rel.x;
    candidate.relY = rel.y;

    if (auto* popup = QuickButtonPopup::create(std::move(candidate))) popup->show();
    return true;
}

bool isCustomQuickButtonReachable(std::string const& id) {
    auto definition = QuickHubManager::get().getCustomButton(id);
    if (!definition) return false;
    return locateButton(*definition) != nullptr;
}

bool activateCustomQuickButton(std::string const& id) {
    auto definition = QuickHubManager::get().getCustomButton(id);
    if (!definition) return false;

    auto* director = CCDirector::get();
    if (!director || !director->getRunningScene() || paimon::isRuntimeShuttingDown()) return true;

    if (auto* item = locateButton(*definition)) {
        item->activate();
        return true;
    }

    // No esta aqui: si sabemos construir su pantalla, vamos y lo pulsamos alli.
    if (isNavigableScreen(definition->sceneClass) &&
        currentSceneClass() != definition->sceneClass) {
        if (auto* scene = buildSceneForClass(definition->sceneClass)) {
            PendingActivation::start(*definition);
            director->replaceScene(scene);
            return true;
        }
    }

    PendingActivation::cancel();
    std::string message = definition->sceneClass.empty()
        ? fmt::format("\"{}\" no esta en esta pantalla.", definition->name)
        : fmt::format("\"{}\" vive en {}. Abre esa pantalla.", definition->name,
                      friendlyScreenName(definition->sceneClass));
    PaimonNotify::create(message.c_str(), NotificationIcon::Warning)->show();
    return true;
}

} // namespace paimon::quickhub
