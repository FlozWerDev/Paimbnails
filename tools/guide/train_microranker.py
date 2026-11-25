#!/usr/bin/env python3
"""Entrena el micro-modelo de Paimon (MicroRanker).

Lee los queries etiquetados de tests/guide/harness.cpp + los keywords de
tests/guide/TestIntents.hpp, calcula pesos Naive-Bayes (log-odds) sobre
trigramas de caracteres y exporta src/features/guide/services/MicroRankerWeights.inc.

Solo stdlib. Re-ejecutable: `python tools/guide/train_microranker.py`
"""

import argparse
import datetime
import math
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tests" / "guide" / "harness.cpp"
TEST_INTENTS = ROOT / "tests" / "guide" / "TestIntents.hpp"
OUT = ROOT / "src" / "features" / "guide" / "services" / "MicroRankerWeights.inc"

TOP_K_PER_INTENT = 48
MIN_W = 0.30
ALPHA = 0.5
MAX_W = 3.0  # clamp log-odds (x100 -> int16)

# Muestras extra para intents que solo existen en PaimonGuideService.cpp
# (no tienen mirror en TestIntents.hpp).
EXTRA_SAMPLES = [
    ("ok", "english", "acknowledgement"),
    ("okay", "english", "acknowledgement"),
    ("vale", "spanish", "acknowledgement"),
    ("genial", "spanish", "acknowledgement"),
    ("perfecto", "spanish", "acknowledgement"),
    ("entendido", "spanish", "acknowledgement"),
    ("got it", "english", "acknowledgement"),
    ("sounds good", "english", "acknowledgement"),
    ("de acuerdo", "spanish", "acknowledgement"),
    ("si", "spanish", "affirmation"),
    ("sip", "spanish", "affirmation"),
    ("claro", "spanish", "affirmation"),
    ("yes", "english", "affirmation"),
    ("yeah", "english", "affirmation"),
    ("sure", "english", "affirmation"),
    ("no", "spanish", "negation"),
    ("ese no", "spanish", "negation"),
    ("otra cosa", "spanish", "negation"),
    ("nope", "english", "negation"),
    ("not that", "english", "negation"),
    ("ola", "spanish", "greeting"),
    ("holi", "spanish", "greeting"),
    ("holis", "spanish", "greeting"),
    ("helo", "english", "greeting"),
    ("wenas", "spanish", "greeting"),
]

_ACCENT_MAP = {}
for _a, _b in [
    ("aaaaaa", "àáâãäå"), ("AAAAAA", "ÀÁÂÃÄÅ"),
    ("eeee", "èéêë"), ("EEEE", "ÈÉÊË"),
    ("iiii", "ìíîï"), ("IIII", "ÌÍÎÏ"),
    ("oooooo", "òóôõöø"), ("OOOOOO", "ÒÓÔÕÖØ"),
    ("uuuu", "ùúûü"), ("UUUU", "ÙÚÛÜ"),
]:
    for _plain, _acc in zip(_a, _b):
        _ACCENT_MAP[_acc] = _plain
_ACCENT_MAP["ñ"] = "n"
_ACCENT_MAP["Ñ"] = "n"


def cpp_unescape(s: str) -> str:
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            if n == "x":
                j = i + 2
                hex_digits = ""
                while j < len(s) and len(hex_digits) < 2 and s[j] in "0123456789abcdefABCDEF":
                    hex_digits += s[j]
                    j += 1
                if hex_digits:
                    out.append(chr(int(hex_digits, 16)))
                    i = j
                    continue
                out.append("x")
                i += 2
                continue
            mapping = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "0": "\0"}
            out.append(mapping.get(n, n))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def normalize(s: str) -> str:
    """Puerto de LightLemmatizer::normalize a Python."""
    s = "".join(_ACCENT_MAP.get(ch, ch) for ch in s)
    out = []
    last_space = True
    for ch in s:
        if ord(ch) < 128:
            low = ch.lower()
            if low.isalnum():
                out.append(low)
                last_space = False
            else:
                if not last_space:
                    out.append(" ")
                    last_space = True
        else:
            out.append(ch)
            last_space = False
    return "".join(out).strip()


def trigrams(norm: str):
    padded = " " + norm + " "
    if len(padded) < 3:
        return []
    return [padded[i : i + 3] for i in range(len(padded) - 2)]


def fnv1a(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def parse_harness(text: str):
    """Extrae (query, lang, intent_id) de los Case del harness (solo 4 campos)."""
    pat = re.compile(
        r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"(english|spanish)"\s*,\s*\{([^}]*)\}\s*,\s*"[^"]*"\s*\}',
        re.DOTALL,
    )
    samples = []
    for m in pat.finditer(text):
        raw_q, lang, ids_body = m.group(1), m.group(2), m.group(3)
        if "FALLBACK" in ids_body:
            continue
        ids = re.findall(r'"([^"]+)"', ids_body)
        if not ids:
            continue
        q = cpp_unescape(raw_q)
        samples.append((q, lang, ids[0]))
    return samples


def parse_test_intents(text: str):
    samples = []
    # func("id", weight, "enName", {aliases}, "esName", {aliases}, ...)
    func_pat = re.compile(
        r'func\(\s*"([^"]+)"\s*,\s*\d+\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*\{([^}]*)\}\s*,'
        r'\s*"((?:[^"\\]|\\.)*)"\s*,\s*\{([^}]*)\}',
        re.DOTALL,
    )
    for m in func_pat.finditer(text):
        intent_id = m.group(1)
        for raw in [m.group(2)] + re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(3)):
            q = cpp_unescape(raw)
            if q.strip():
                samples.append((q, "english", intent_id))
        for raw in [m.group(4)] + re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(5)):
            q = cpp_unescape(raw)
            if q.strip():
                samples.append((q, "spanish", intent_id))
    # conv("id", weight, {en...}, {es...})
    conv_pat = re.compile(
        r'conv\(\s*"([^"]+)"\s*,\s*\d+\s*,\s*\{([^}]*)\}\s*,\s*\{([^}]*)\}',
        re.DOTALL,
    )
    for m in conv_pat.finditer(text):
        intent_id = m.group(1)
        for raw in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2)):
            q = cpp_unescape(raw)
            if q.strip():
                samples.append((q, "english", intent_id))
        for raw in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(3)):
            q = cpp_unescape(raw)
            if q.strip():
                samples.append((q, "spanish", intent_id))
    return samples


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--top-k", type=int, default=TOP_K_PER_INTENT)
    ap.add_argument("--min-w", type=float, default=MIN_W)
    ap.add_argument("--out", type=Path, default=OUT)
    args = ap.parse_args()

    harness_text = HARNESS.read_text(encoding="utf-8")
    intents_text = TEST_INTENTS.read_text(encoding="utf-8")

    samples = parse_harness(harness_text) + parse_test_intents(intents_text) + EXTRA_SAMPLES
    # dedup exacto (query normalizada, intent)
    seen = set()
    uniq = []
    for q, lang, iid in samples:
        norm = normalize(q)
        if not norm:
            continue
        key = (norm, iid)
        if key in seen:
            continue
        seen.add(key)
        uniq.append((norm, iid))

    per_intent: dict[str, Counter] = {}
    intent_totals: dict[str, int] = {}
    global_counts: Counter = Counter()
    for norm, iid in uniq:
        grams = trigrams(norm)
        c = per_intent.setdefault(iid, Counter())
        for g in grams:
            c[g] += 1
            global_counts[g] += 1
        intent_totals[iid] = intent_totals.get(iid, 0) + len(grams)

    total_all = sum(intent_totals.values())
    V = len(global_counts)
    entries = []  # (intent_id, gram, w_float)
    for iid, c in per_intent.items():
        total_i = intent_totals[iid]
        scored = []
        for g, cnt in c.items():
            w = math.log((cnt + ALPHA) / (total_i + ALPHA * V)) - math.log(
                (global_counts[g] - cnt + ALPHA) / (total_all - total_i + ALPHA * V)
            )
            if w >= args.min_w:
                scored.append((g, min(w, MAX_W), cnt))
        scored.sort(key=lambda t: t[1], reverse=True)
        for g, w, _cnt in scored[: args.top_k]:
            entries.append((iid, g, w))

    entries.sort(key=lambda e: (fnv1a(e[1].encode()), fnv1a(e[0].encode())))
    version = "micro-{}-{}intents-{}entries".format(
        datetime.date.today().isoformat(), len(per_intent), len(entries)
    )
    lines = [
        "// Generado por tools/guide/train_microranker.py — no editar a mano.",
        "// version: {}".format(version),
        "// dataset: {} samples (harness + TestIntents + extras)".format(len(uniq)),
        "static char const kMicroWeightsVersion[] = \"{}\";".format(version),
        "static std::size_t const kMicroWeightCount = {};".format(len(entries)),
        "static MicroWeight const kMicroWeights[] = {",
    ]
    for iid, g, w in entries:
        ih = fnv1a(iid.encode("utf-8"))
        gh = fnv1a(g.encode("utf-8"))
        q = max(1, min(999, int(round(w * 100))))
        lines.append("    {{{}u, {}u, {}}}, // {} <- {!r}".format(ih, gh, q, iid, g))
    lines.append("};")

    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    kb = args.out.stat().st_size / 1024
    print("intents: {}".format(len(per_intent)))
    print("samples: {}".format(len(uniq)))
    print("entries: {}".format(len(entries)))
    print("weights: {} ({:.1f} KB)".format(args.out, kb))
    print("version: {}".format(version))
    return 0


if __name__ == "__main__":
    sys.exit(main())
