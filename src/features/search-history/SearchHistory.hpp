#pragma once
//
// Data model + persistence for search-history entries.
// Port of "Search History" by hiimjasmine00 (MIT), adapted to Paimbnails conventions.
//

#include <matjson.hpp>
#include <cstdint>
#include <string>
#include <vector>

class GJSearchObject;

namespace paimon::searchhistory {

// Una entrada del historial: la query + el snapshot completo de filtros.
struct Entry {
    int64_t time = 0;            // epoch seconds (para mostrar la fecha)
    int type = 0;                // 0 = niveles, 1 = listas, 2 = usuarios
    std::string query;
    std::vector<int> difficulties;
    std::vector<int> lengths;
    bool uncompleted = false;
    bool completed = false;
    bool featured = false;
    bool original = false;
    bool twoPlayer = false;
    bool coins = false;
    bool epic = false;
    bool legendary = false;
    bool mythic = false;
    bool song = false;
    bool customSong = false;
    int songID = 0;
    int demonFilter = 0;
    bool noStar = false;
    bool star = false;

    // Dos entradas son "la misma" si comparten dia, tipo, query y filtros.
    bool operator==(const Entry& other) const;
    // Texto corto con los filtros activos (para el subtitulo de la celda).
    std::string summary() const;
};

// Mas reciente primero. Vive en memoria; se persiste en load()/save().
extern std::vector<Entry> history;

// Inserta (o re-promueve) una busqueda al frente del historial.
void add(GJSearchObject* search, std::vector<int> difficulties, std::vector<int> lengths, int type);
void remove(int index);
void clear();
void load();
void save();

} // namespace paimon::searchhistory

template <>
struct matjson::Serialize<paimon::searchhistory::Entry> {
    static geode::Result<paimon::searchhistory::Entry> fromJson(const matjson::Value&);
    static matjson::Value toJson(const paimon::searchhistory::Entry&);
};
