# Paimon guide matcher tests

Host-side gate test for the guide's "thinking" algorithm (`PaigoritV1` +
`LightLemmatizer`). It compiles the real algorithm sources directly (no Geode
SDK needed) and runs a labeled query set covering exact names, aliases,
synonyms, typos, natural phrasing, conversational chat, accented/uppercase
input, compound disambiguation, and non-matches.

## Run

```
tests\guide\run_tests.bat
```

Exit code 0 = all cases pass. Failures print the query, the matched intent, and
the expected set.

## Layout

- `harness.cpp` — replicates the service's normalize/tokenize, runs `PaigoritV1`,
  reports accuracy. Add new cases to the `cases` table.
- `TestIntents.hpp` — fixture mirroring `PopupRegistry` (functional) and
  `PaimonGuideService` (conversational) intents. Keep in sync if registry
  weights/keywords change.
- `stubs/Geode/Geode.hpp` — empty stub so the guide headers compile standalone.

## How the matcher ranks (summary)

Qualified intents are ranked by a match-quality **tier** first, then by
`weight * qualityFactor + bonuses` within the tier:

- tier 4: full query equals a keyword (user typed the exact name/alias)
- tier 3: exact word match or compound (all words of a multi-word keyword present)
- tier 2: strong fuzzy (anchored >= 95 or phrase >= 97)
- tier 1: typo-level token match (>= 80) or solid phrase match (>= 88)
- tier 0: weak

Tiering means a strong match to a light intent beats a weak match to a heavy
one. Phrase-level (substring) matches need a higher bar (88) than token-level
typos (80), and keywords shorter than 4 chars only match as whole tokens, which
keeps short aliases like `yo`/`qh`/`bg` from matching inside unrelated words.

## Beyond navigation (the "small AI" layers)

- **Mod-knowledge intents** (`PaimonGuideService`): answer questions *about*
  Paimbnails itself - what it is, who made it, is it free, how to install, the
  feature list, support. Feature count and version are filled in dynamically so
  they never drift.
- **Multi-topic detection** (`PaigoritV1::splitTopics`): a query like
  "cursor and discord" is split on conjunctions (and / y / e); each segment is
  matched independently and, when two or more strong functional topics are
  found, Paimon names all of them and opens the first. Tested in the
  `multi-topic` section of the harness.
- **Search phrases** (soft NLU): each popup can list problem / how-to phrases
  (`searchPhrasesByLang`) scored with a cap so they never beat an exact name.
  Example: "no se ven miniaturas" → thumbnail-settings.
- **Category browse**: phrases like "cosas de musica" / "profile stuff" list
  every popup in that category with chips.
- **Recommendations**: ambiguous matches, multi-topic, category browse, and
  near-miss fallbacks fill `GuideAnswer::recommendations` for dynamic chat chips.

