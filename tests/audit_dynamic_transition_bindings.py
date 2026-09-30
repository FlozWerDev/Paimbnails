"""Inventory UI and animation entry points from the bindings used by the build."""

import argparse
import hashlib
import json
import re
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLASS = re.compile(r"^class ([\w:]+)(?:\s*:\s*([^\n{]+))?\s*\{\n(.*?)^}", re.M | re.S)
METHOD = re.compile(
    r"^[ \t]*(?:virtual\s+|static\s+)?[^\n;/{}]*?\b([\w~]+)\(([^\n]*)\)"
    r"\s*(?:const\s*)?(?:=\s*([^;\n]+);|\{)", re.M
)
ENTRY = re.compile(
    r"^(?:keyBackClicked|keyDown|keyUp|show|showLayer|hideLayer|enterLayer|exitLayer|"
    r"layerVisible|layerHidden|enterAnimFinished|scene|addToMainScene|onBack|onClose|"
    r"onCancel|onExit|onEnter|onReturn|onMenu|onReplay|selected|unselected|activate|"
    r"onBtn[12]|onResume|close|open|popScene|popSceneWithTransition|pushScene|replaceScene|"
    r"dispatchKeyboardMSG|dispatchKeypadMSG|addChild|removeChild|removeAllChildrenWithCleanup)$"
    r"|^(?:animate|animation|fade|transition|run.*Animation|finish.*Animation)", re.I
)
FAMILIES = {
    "popup": "Popup del juego",
    "dropdown": "Panel desplegable",
    "blocking": "Panel bloqueante",
    "dialog": "Dialogo",
    "browser_overlay": "Escena / navegador superpuesto",
    "scene": "Navegacion de escenas",
    "game_editor": "Escena de gameplay / editor conservada",
    "loading": "Carga / animacion nativa conservada",
    "ui_component": "Componente / animacion nativa conservada",
    "engine": "Punto comun del motor",
    "visual_component": "Control o efecto / animacion nativa conservada",
}
ENGINE = {
    "cocos2d::CCDirector", "cocos2d::CCNode", "cocos2d::CCKeyboardDispatcher",
    "cocos2d::CCKeypadDispatcher", "cocos2d::CCMenuItem",
}


def bindings_directory():
    cache = ROOT / "build-win/CMakeCache.txt"
    version = re.search(r"^GEODE_GD_VERSION:[^=]+=([^\n]+)$", cache.read_text(), re.M)[1]
    path = ROOT / "build-win/_deps/bindings-src/bindings" / version.strip()
    if not (path / "Entry.bro").is_file():
        raise FileNotFoundError(f"Missing bindings for {version}: {path}")
    return path


def collect(directory):
    classes, sources = {}, []
    entry = directory / "Entry.bro"
    for filename in ["Entry.bro", *re.findall(r"#include <([^>]+)>", entry.read_text())]:
        path = directory / filename
        source = path.read_text()
        sources.append({"path": str(path.relative_to(ROOT)), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        for match in CLASS.finditer(source):
            name, parents, body = match.groups()
            methods = []
            for method in METHOD.finditer(body):
                if not ENTRY.search(method[1]):
                    continue
                bindings = method[3] or "inline body"
                methods.append({"name": method[1], "parameters": method[2], "bindings": bindings,
                                "line": source.count("\n", 0, match.start(3) + method.start()) + 1})
            classes[name] = {
                "name": name, "bases": [p.strip() for p in (parents or "").split(",") if p.strip()],
                "source": str(path.relative_to(ROOT)), "line": source.count("\n", 0, match.start()) + 1,
                "methods": methods,
            }
    return classes, sources


def ancestors(name, classes, seen=None):
    seen = set() if seen is None else seen
    if name in seen:
        return seen
    seen.add(name)
    for parent in classes.get(name, {}).get("bases", []):
        ancestors(parent, classes, seen)
    return seen


def family(name, ancestry, methods):
    if name in ENGINE:
        return "engine"
    for base, category in (("FLAlertLayer", "popup"), ("GJDropDownLayer", "dropdown"),
                           ("SlideInLayer", "dropdown"), ("CCBlockLayer", "blocking"), ("DialogLayer", "dialog")):
        if base in ancestry:
            return category
    if name == "LevelBrowserLayer":
        return "browser_overlay"
    if ancestry & {"GJBaseGameLayer", "EditorUI", "UILayer"}:
        return "game_editor"
    if ancestry & {"LoadingLayer", "LoadingCircle"}:
        return "loading"
    if any(method["name"] == "scene" for method in methods):
        return "scene"
    if "cocos2d::CCLayer" in ancestry:
        return "ui_component"
    return "visual_component"


def inventory(directory=None):
    directory = directory or bindings_directory()
    classes, sources = collect(directory)
    candidates = []
    for name, item in classes.items():
        lineage = ancestors(name, classes)
        is_layer = "cocos2d::CCLayer" in lineage
        if name not in ENGINE and not is_layer and not ("cocos2d::CCNode" in lineage and item["methods"]):
            continue
        item = dict(item)
        item["family"] = family(name, lineage, item["methods"])
        item["ancestors"] = sorted(lineage - {name})
        candidates.append(item)
    shared = defaultdict(list)
    for item in candidates:
        for method in item["methods"]:
            match = re.search(r"\bwin (0x[\da-f]+)\b", method["bindings"])
            if match:
                shared[match[1]].append(f'{item["name"]}::{method["name"]}')
    return {
        "version": directory.name, "sources": sources, "total_classes": len(classes),
        "candidate_count": len(candidates), "families": dict(sorted(Counter(c["family"] for c in candidates).items())),
        "classes": sorted(candidates, key=lambda c: (c["family"], c["name"])),
        "shared_windows_addresses": {address: sorted(set(methods)) for address, methods in sorted(shared.items())
                                     if len(set(methods)) > 1},
    }


def markdown(data):
    lines = [
        "# Inventario de Dynamic Transition", "",
        f'Fuente: `build-win/_deps/bindings-src/bindings/{data["version"]}/Entry.bro` y todos sus includes.', "",
        f'Se revisaron **{data["total_classes"]} clases** y se extrajeron **{data["candidate_count"]} candidatos** '
        "de interfaz, controles y efectos. Este inventario describe cobertura por herencia y puntos de entrada; "
        "la validacion visual en Geometry Dash sigue pendiente.", "",
        "Regenerar sin compilar: `python3 tests/audit_dynamic_transition_bindings.py --write`.", "",
        "## Cobertura implementada", "",
        "- Escenas: `replaceScene`, `pushScene`, `popScene`, `popSceneWithTransition`; regreso por Escape o Volver.",
        "- Popups, paneles desplegables, bloqueantes y dialogos: insercion y retirada del nodo, incluso desde overrides de cierre.",
        "- `LevelBrowserLayer` admite tambien el modo superpuesto (`m_isOverlay`).",
        "- Aperturas especificas: las implementaciones de `show` con direccion Win distinta se interceptan una vez; "
        "los metodos compartidos no reciben hooks duplicados.",
        "- `EndLevelLayer` y `RetryLevelLayer` incluyen su `showLayer` propio. Opciones usa `GJDropDownLayer`.",
        "- `SlideInLayer::showLayer/hideLayer` tiene bindings solo en macOS; alli se adapta su animacion. "
        "La insercion y retirada comun no depende de esas direcciones.",
        "- Los componentes, carga, monedas, particulas, texto, scroll y objetos del nivel conservan sus animaciones propias. "
        "Dynamic Transition se aplica a la navegacion y a los paneles completos.",
        "- Los popups marcados de Paimon usan Dynamic Popups. Los popups de Geode de otros mods son opcionales.", "",
        "| Familia | Clases |", "| --- | ---: |",
    ]
    lines.extend(f'| {FAMILIES[key]} | {count} |' for key, count in data["families"].items())
    for category in FAMILIES:
        items = [item for item in data["classes"] if item["family"] == category]
        if not items:
            continue
        lines += ["", f"## {FAMILIES[category]}", "", "| Clase | Metodos propios detectados |", "| --- | --- |"]
        for item in items:
            methods = ", ".join(f"`{name}`" for name in sorted({m["name"] for m in item["methods"]})) or "Heredados"
            lines.append(f'| `{item["name"]}` | {methods} |')
    lines += ["", "## Direcciones compartidas en Windows", "",
              "La lista completa con parametros, archivo, linea y bindings por plataforma esta en "
              "`DYNAMIC_TRANSITION_BINDINGS.json`. Estas direcciones explican la eleccion de hooks comunes.", "",
              "| Direccion | Metodos |", "| --- | --- |"]
    for address, methods in data["shared_windows_addresses"].items():
        if any(method.endswith("::show") or method.endswith("::showLayer") for method in methods):
            lines.append(f'| `{address}` | {", ".join(f"`{method}`" for method in methods)} |')
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="Write the JSON and Markdown inventory to docs")
    args = parser.parse_args()
    data = inventory()
    if args.write:
        (ROOT / "docs/DYNAMIC_TRANSITION_BINDINGS.json").write_text(json.dumps(data, indent=2) + "\n")
        (ROOT / "docs/DYNAMIC_TRANSITION_BINDINGS.md").write_text(markdown(data))
    print(f'Geometry Dash {data["version"]}: {data["total_classes"]} classes, '
          f'{data["candidate_count"]} UI/animation candidates. {data["families"]}')


if __name__ == "__main__":
    main()
