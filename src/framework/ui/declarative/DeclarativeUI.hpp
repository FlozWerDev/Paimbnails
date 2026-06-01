#pragma once

// DeclarativeUI.hpp — Motor de UI declarativo de Paimbnails.
//
// Describe arboles de nodos como datos (Spec o JSON) y los construye con un
// factory por tipo + appliers de atributos. Inspirado en patrones generales
// de UI data-driven; implementacion propia.
//
//   Spec spec{ "CCLabelBMFont", "my-label", attrs, {} };
//   auto* node = dec::build(spec, parent);
//
// o desde JSON:
//
//   auto* node = dec::build(dec::Spec::fromJson(json), parent);

#include <matjson.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cocos2d { class CCNode; }

namespace paimon::ui::dec {

// Descripcion declarativa de un nodo.
struct Spec {
    std::string type;                                    // "CCNode", "CCLabelBMFont", ...
    std::string id;                                      // opcional (node id)
    matjson::Value attributes = matjson::Value::object();
    std::vector<Spec> children;

    static Spec fromJson(matjson::Value const& json);
};

// type string -> creador del nodo base.
using Creator = std::function<cocos2d::CCNode*(matjson::Value const& attrs)>;

// Registro de tipos (patron factory). Inicializa tipos por defecto en el
// primer uso.
class Factory {
public:
    static Factory& get();
    void registerType(std::string_view type, Creator creator);
    cocos2d::CCNode* create(std::string_view type, matjson::Value const& attrs);

private:
    void ensureDefaults();
    bool m_ready = false;
    std::unordered_map<std::string, Creator> m_creators;
};

// Aplica atributos a un nodo ya creado. 'parent' se usa para posiciones
// ancladas (anchor relativo al padre); si es null, se ancla a la pantalla.
void applyAttributes(cocos2d::CCNode* node, matjson::Value const& attrs,
                     cocos2d::CCNode* parent = nullptr);

// Construye el arbol de un Spec. Si 'parent' != null, agrega el nodo raiz a el.
cocos2d::CCNode* build(Spec const& spec, cocos2d::CCNode* parent = nullptr);

// Busca un descendiente por query basada en node IDs:
//   "a > b"  -> hijo directo 'b' del hijo 'a'
//   "a b"    -> descendiente recursivo 'b' bajo 'a'
cocos2d::CCNode* query(cocos2d::CCNode* root, std::string_view path);

} // namespace paimon::ui::dec
