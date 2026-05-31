#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// LightLemmatizer.hpp
//
// Modulo ligero de "comprension" para Paigorit V1. NO depende de librerias
// externas; todo es ASCII + UTF-8 estricto.
//
// Tres tareas:
//
//   1. Stopwords: palabras vacias del ingles/espanol que no aportan al
//      matching. Se eliminan de la query antes de comparar (asi
//      "donde configuro el cursor" se reduce a "configuro cursor", lo que
//      evita falsos matches con keywords tipo "el", "la", "donde").
//
//   2. Stemming basico (lematizacion por sufijos): reduce variaciones
//      morfologicas comunes a una raiz aproximada. Ejemplos:
//          - backgrounds  -> background
//          - configurar   -> configur
//          - configurando -> configur
//          - musicas      -> music (con caida final de 'a')
//      No es Porter-stemmer, pero captura ~80% de los casos comunes en
//      consultas de usuarios sin necesidad de tablas Unicode.
//
//   3. Sinonimos / aliases: mapa simple de palabras a su "forma canonica".
//      Ejemplos:
//          - pic    -> picture
//          - pfp    -> profile picture
//          - cancion-> musica
//          - bg     -> background
//      Permite que el usuario escriba en jerga y aun asi matchee.
//
// La interfaz expone `expand(token)` que devuelve la lista de variantes
// canonicas (la raiz + sinonimos resueltos). Paigorit usa esta lista para
// puntuar contra las keywords del intent.
// ─────────────────────────────────────────────────────────────────────────────

namespace paimon::guide {

class LightLemmatizer {
public:
    // True si el token es una stopword (en cualquier idioma soportado).
    static bool isStopword(std::string const& tokenLower);

    // Aplica stemming basico: recorta sufijos comunes en/es y devuelve la
    // raiz aproximada. Si el token es muy corto (<4 chars) lo devuelve
    // tal cual.
    static std::string stem(std::string const& tokenLower);

    // Expande un token a una lista canonica:
    //   1) si es stopword -> {} (lista vacia)
    //   2) si tiene sinonimo -> [sinonimo, stem(sinonimo)]
    //   3) sino -> [token, stem(token)]
    // Devuelve la lista deduplicada. Sirve para que el matcher tenga MAS
    // formas posibles del token al comparar.
    static std::vector<std::string> expand(std::string const& tokenLower);

    // Filtra una lista de tokens removiendo stopwords. Util para preparar
    // la query antes de pasarla a Paigorit.
    static std::vector<std::string> removeStopwords(std::vector<std::string> const& tokens);

    // Tokenizar y filtrar de una sola vez (helper).
    // Asume que `normalizedLower` ya esta normalizado (sin acentos, lowercase).
    static std::vector<std::string> tokenizeNoStopwords(std::string const& normalizedLower);

private:
    // Tabla estatica de stopwords compartidas entre EN/ES (al estar
    // normalizadas en ASCII sin acentos no hay colisiones graves).
    static std::unordered_set<std::string> const& stopwords();

    // Tabla estatica de sinonimos: clave -> valor canonico.
    static std::unordered_map<std::string, std::string> const& synonyms();
};

} // namespace paimon::guide
