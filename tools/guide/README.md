# Guía — micro-modelo de Paimon (MicroRanker)

Micro-modelo entrenable para la Paimon Guide: un ranker lineal sobre
trigramas de caracteres (Naive-Bayes log-odds, cuantizado a int16).

- **78 KB** de tabla aprox. en binario, inferencia <0.1 ms, offline, sin dependencias.
- Solo **suma** bonus (+0..12) al `finalScore` de PaigoritV1 y **no opina** en
  queries de <8 chars (ahí mandan las reglas exactas). Los tiers siguen
  decidiendo el orden: el modelo solo desempatas dentro del mismo tier.

## Reentrenar

```sh
python tools/guide/train_microranker.py
```

Lee `tests/guide/harness.cpp` (queries etiquetados) + `tests/guide/TestIntents.hpp`
(keywords por intent) + `EXTRA_SAMPLES` del propio script, y regenera
`src/features/guide/services/MicroRankerWeights.inc`.

Opciones: `--top-k` (trigramas por intent, def. 48), `--min-w` (peso mínimo,
def. 0.3), `--out`.

## Verificar (hay que seguir al 100%)

```sh
clang++ -std=c++20 -O1 \
  -I"build/cpm-cache/rapidfuzz/<hash>" -I"tests/guide/stubs" \
  tests/guide/harness.cpp -o /tmp/paimon-harness.exe && /tmp/paimon-harness.exe

clang++ -std=c++20 -O1 \
  -I"build/cpm-cache/rapidfuzz/<hash>" -I"tests/guide/stubs" \
  tests/guide/conversation_harness.cpp -o /tmp/paimon-conv.exe && /tmp/paimon-conv.exe
```

El hash de rapidfuzz se ve con: `ls build/cpm-cache/rapidfuzz/`
(también sirven `tests/guide/run_tests.bat` y `run_conversation_tests.bat`
desde cmd con `clang-cl` en PATH).

## Añadir conocimiento

1. Añade el caso a `tests/guide/harness.cpp` (grupo `bilingual`, `ack`, etc.).
2. Si es un intent nuevo, añádelo a `PaimonGuideService::registerIntents()` **y**
   a su mirror en `tests/guide/TestIntents.hpp` (el contrato del repo).
3. Reentrena y verifica. Si el harness falla, el modelo o los keywords están
   robando queries: prefiere keywords multi-palabra sobre adjetivos sueltos
   (un token exacto siempre es tier 3).
