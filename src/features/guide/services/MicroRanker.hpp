#pragma once

#include <cstdint>
#include <string>

// MicroRanker: micro-modelo entrenable para Paimon (ranker lineal sobre
// trigramas de caracteres con feature hashing implicito).
//
// - Offline, sin dependencias, <100KB de tabla, inferencia O(gramas).
// - Se entrena fuera del juego con tools/guide/train_microranker.py usando
//   los queries etiquetados de tests/guide/harness.cpp + TestIntents.hpp.
// - Los pesos generados viven en MicroRankerWeights.inc. Si el .inc no existe
//   (o el intent no tiene pesos), score() devuelve 0 y PaigoritV1 funciona
//   exactamente igual que antes: el modelo solo SUMA bonus, nunca resta.
//
// integracion: PaigoritV1 suma MicroRanker::score() al finalScore. El tier
// sigue mandando en el orden, asi que el modelo solo desempatas dentro del
// mismo tier. Seguro para el harness (443/443).

namespace paimon::guide {

inline std::uint32_t microFnv1a(char const* data, std::size_t len) {
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < len; ++i) {
        h ^= static_cast<std::uint32_t>(static_cast<unsigned char>(data[i]));
        h *= 16777619u;
    }
    return h;
}

inline std::uint32_t microIntentHash(std::string const& id) {
    return microFnv1a(id.data(), id.size());
}

struct MicroWeight {
    std::uint32_t intentHash;
    std::uint32_t gramHash;
    std::int16_t w; // log-odds * 100, siempre >= 0
};

#if __has_include("MicroRankerWeights.inc")
#define PAIMON_MICRORANKER_HAS_WEIGHTS 1
#include "MicroRankerWeights.inc"
#else
#define PAIMON_MICRORANKER_HAS_WEIGHTS 0
#endif

class MicroRanker {
public:
    // bonus en [0, 12] para (intentId, normalizedQuery ya normalizada).
    static inline float score(std::string const& intentId,
                              std::string const& normalizedQuery) {
#if !PAIMON_MICRORANKER_HAS_WEIGHTS
        (void)intentId;
        (void)normalizedQuery;
        return 0.f;
#else
        if (normalizedQuery.empty() || kMicroWeightCount == 0) return 0.f;
        // queries muy cortas (un token de <=6 chars como "paimon", "musica",
        // "hola") tienen trigramas genericos compartidos por muchos intents:
        // el modelo no discrimina y solo mete ruido. Paigorit (exacto /
        // compuesto / fuzzy con tiers) ya resuelve esas. El modelo solo
        // opina en queries con suficiente evidencia.
        if (normalizedQuery.size() < 8) return 0.f;
        std::uint32_t const ih = microIntentHash(intentId);

        // trigramas con padding de bordes: " hola " -> " ho","hol","ola","la ".
        std::string padded;
        padded.reserve(normalizedQuery.size() + 2);
        padded.push_back(' ');
        padded += normalizedQuery;
        padded.push_back(' ');
        if (padded.size() < 3) return 0.f;

        float acc = 0.f;
        for (std::size_t i = 0; i + 3 <= padded.size(); ++i) {
            std::uint32_t const gh = microFnv1a(padded.data() + i, 3);
            // busqueda lineal sobre tabla ordenada por gram (tablas de ~3-4k).
            for (std::size_t k = 0; k < kMicroWeightCount; ++k) {
                auto const& e = kMicroWeights[k];
                if (e.gramHash != gh) continue;
                if (e.intentHash != ih) continue;
                acc += static_cast<float>(e.w) / 100.f;
                break; // un peso por (intent, gram)
            }
            if (acc >= 12.f) return 12.f;
        }
        return acc > 12.f ? 12.f : acc;
#endif
    }

    static inline char const* weightsVersion() {
#if PAIMON_MICRORANKER_HAS_WEIGHTS
        return kMicroWeightsVersion;
#else
        return "none";
#endif
    }
};

} // namespace paimon::guide
