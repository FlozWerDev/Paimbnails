"""Audit installed GD bitmap fonts against the engine's OS language enum.

Font coverage is not a translation catalogue. This checks representative script
characters and records which existing engine locales the fonts can display.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


SAMPLES = {
    "ENGLISH": "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
    "CHINESE": "中文汉字漢字",
    "FRENCH": "àâæçéèêëîïôœùûüÿÀÂÆÇÉÈÊËÎÏÔŒÙÛÜŸ",
    "ITALIAN": "àèéìòùÀÈÉÌÒÙ",
    "GERMAN": "äöüßÄÖÜẞ",
    "SPANISH": "áéíóúüñ¿¡ÁÉÍÓÚÜÑ",
    "DUTCH": "áéíóúàèëïöüĳÁÉÍÓÚÀÈËÏÖÜĲ",
    "RUSSIAN": "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя",
    "KOREAN": "한국어한글",
    "JAPANESE": "日本語ひらがなカタカナ",
    "HUNGARIAN": "áéíóöőúüűÁÉÍÓÖŐÚÜŰ",
    "PORTUGUESE": "áâãàçéêíóôõúüÁÂÃÀÇÉÊÍÓÔÕÚÜ",
    "ARABIC": "العربية",
}


def read_glyphs(path):
    text = path.read_text(encoding="utf-8-sig")
    return {int(value) for value in re.findall(r"\bchar\s+id=\s*(\d+)", text)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--bindings", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    common_path = args.sdk / "loader/include/Geode/cocos/platform/CCCommon.h"
    common = common_path.read_text(encoding="utf-8-sig")
    enum = re.search(r"enum\s+LanguageType\s*\{([^}]+)\}", common)
    if not enum:
        raise ValueError(f"LanguageType enum not found in {common_path}")
    locales = re.findall(r"kLanguage(\w+)", enum[1])
    # Enum spelling is mixed-case; retain the declared order in the result.
    samples = {locale: SAMPLES[locale.upper()] for locale in locales}
    bro_path = args.bindings / "bindings/2.2091/GeometryDash.bro"
    bro = bro_path.read_text(encoding="utf-8-sig")
    fonts = {}
    coverage = {}
    for name in ("bigFont", "goldFont", "chatFont"):
        path = args.resources / f"{name}.fnt"
        glyphs = read_glyphs(path)
        fonts[name] = {
            "unique_glyphs": len(glyphs),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
        for locale, sample in samples.items():
            missing = sorted({ord(char) for char in sample} - glyphs)
            coverage.setdefault(locale, {})[name] = {
                "missing_sample_codepoints": [f"U+{code:04X}" for code in missing],
                "sample_covered": not missing,
            }
    report = {
        "scope": "Representative font coverage; not GD translations or an exhaustive list of languages",
        "sources": {"os_enum": str(common_path), "bindings": str(bro_path), "fonts": str(args.resources)},
        "unicode_input_field_declared": "bool m_unicodeText" in bro,
        "system_locale_count": len(locales),
        "fonts": fonts,
        "sample_coverage": coverage,
        "additional_chatfont_compatible_locales": [
            locale for locale in locales
            if locale.upper() not in ("ENGLISH", "SPANISH")
            and coverage[locale]["chatFont"]["sample_covered"]
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    print(f"OS locales: {len(locales)}")
    for name, font in fonts.items():
        print(f"{name}: {font['unique_glyphs']} unique glyphs")
    print("Additional compatible samples: " + ", ".join(report["additional_chatfont_compatible_locales"]))
    print(f"Details: {args.output}")


if __name__ == "__main__":
    main()
