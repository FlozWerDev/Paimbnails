#pragma once

#include "GuideIntents.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// PopupRegistry.hpp
//
// Registro central de popups y layers del mod, pensado como BASE DE
// CONOCIMIENTO para Paimon. La idea es que Paimon aprenda lo que existe en
// la mod a partir de los NOMBRES REALES de los popups (los que aparecen en
// la barra de titulo de cada uno), en lugar de listas manuales de keywords.
//
// Cada entrada tiene:
//   - id estable (para logging y matching)
//   - displayName por idioma: el titulo REAL del popup (EN/ES)
//   - aliases por idioma: sinonimos comunes que el usuario podria usar
//     (ej. "pfp" para "Profile Photo Editor")
//   - category: bucket logico (background / music / profile / capture /...)
//   - weight: peso para desambiguacion (ver Paigorit V1)
//   - open(): lambda que abre el popup correspondiente
//   - description: lo que Paimon dice antes de llevarte al popup
//
// Asi cuando el usuario pregunta "profile background", Paigorit busca en el
// registro un popup cuyo displayName matchee mejor: encuentra
// "Profile Background" (ProfileBgPickerPopup) y lo abre. No hay que mantener
// listas paralelas de keywords.
//
// Para preguntas amplias como "como uso emotes" o "fondos", la categoria
// permite agrupar popups relacionados y que Paimon ofrezca el principal.
// ─────────────────────────────────────────────────────────────────────────────

namespace paimon::guide {

class PaimonGuideChatPopup;

// Categorias logicas de popups. Sirven para que el usuario pregunte de
// forma generica ("musica") y se mande al popup principal de esa categoria.
enum class PopupCategory {
    None,
    Background,
    Music,
    Profile,
    Capture,
    Cursor,
    Pet,
    Discord,
    Forum,
    Emote,
    Transition,
    Layout,
    Volume,
    Cache,
    Update,
    Language,
    QuickHub,
    Thumbnail,
    Help,
};

struct PopupEntry {
    std::string id;
    PopupCategory category = PopupCategory::None;
    int weight = 80;

    // Titulo real del popup, indexado por idioma.
    // El primero que matchee define lo que Paimon usa como base de matching.
    // Si no existe el idioma activo, fallback a "english".
    std::unordered_map<std::string, std::string> displayNameByLang;

    // Alias / sinonimos que el usuario podria usar pero no aparecen en el
    // titulo. Indexado por idioma. Ej. "pfp", "avatar" para Profile Photo Editor.
    std::unordered_map<std::string, std::vector<std::string>> aliasesByLang;

    // Mensaje corto que Paimon dice antes de llevarte. Indexado por idioma.
    std::unordered_map<std::string, std::string> descriptionByLang;

    // Lambda que abre el popup. Si es null, Paimon solo describe.
    std::function<void(PaimonGuideChatPopup* popup)> open = nullptr;

    GuideAnimation animation = GuideAnimation::Point;
};

class PopupRegistry {
public:
    static PopupRegistry& get();

    // Devuelve todas las entradas registradas (read-only).
    std::vector<PopupEntry> const& entries() const { return m_entries; }

    // Re-construye el registro. Llamado al iniciar el servicio. Si el
    // idioma activo cambia en runtime no es necesario re-construir, ya
    // que displayNameByLang tiene todas las entradas precargadas.
    void rebuild();

    // Convierte una entrada en un GuideIntent equivalente, listo para que
    // PaigoritV1::run lo procese. Los keywordsByLang se construyen como
    // [displayName + aliases] por idioma. weight y category influyen en el
    // peso final del intent.
    static GuideIntent toIntent(PopupEntry const& entry);

private:
    PopupRegistry();
    void registerAll();

    std::vector<PopupEntry> m_entries;
};

} // namespace paimon::guide
