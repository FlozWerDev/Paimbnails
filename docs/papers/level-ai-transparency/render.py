from hashlib import sha256
from html import escape
from pathlib import Path
import json
import re


PAPER_DIR = Path(__file__).resolve().parent
ROOT = PAPER_DIR.parents[2]
ASSETS = "/request-assets/papers/ai-level-transparency"


def inline(value):
    value = escape(value)
    value = re.sub(r"\[([^\]]+)\]\((https?://[^\s)]+)\)", r'<a href="\2">\1</a>', value)
    value = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", value)
    return value


def render_blocks(lines):
    result = []
    index = 0
    while index < len(lines):
        line = lines[index]
        if not line.strip():
            index += 1
            continue
        if line.startswith("```"):
            kind = line[3:]
            index += 1
            code = []
            while index < len(lines) and not lines[index].startswith("```"):
                code.append(lines[index])
                index += 1
            if index == len(lines):
                raise ValueError("Unterminated code block")
            class_name = "equation" if kind == "equation" else "code-block"
            result.append(f'<pre class="{class_name}"><code>{escape(chr(10).join(code))}</code></pre>')
        elif line.startswith("| "):
            rows = []
            while index < len(lines) and lines[index].startswith("| "):
                rows.append([inline(cell.strip()) for cell in lines[index].strip().strip("|").split("|")])
                index += 1
            if len(rows) < 2 or not all(re.fullmatch(r"[-: ]+", cell) for cell in rows[1]):
                raise ValueError("Malformed table")
            head = "".join(f'<th scope="col">{cell}</th>' for cell in rows[0])
            body = "".join("<tr>" + "".join(f"<td>{cell}</td>" for cell in row) + "</tr>" for row in rows[2:])
            label = escape(re.sub(r"<[^>]+>", "", rows[0][0]), quote=True)
            result.append(f'<div class="table-scroll" tabindex="0" role="region" aria-label="Tabla: {label}"><table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table></div>')
            continue
        elif re.match(r"^\d+\. ", line):
            items = []
            while index < len(lines) and re.match(r"^\d+\. ", lines[index]):
                items.append("<li>" + inline(re.sub(r"^\d+\. ", "", lines[index])) + "</li>")
                index += 1
            result.append("<ol>" + "".join(items) + "</ol>")
            continue
        else:
            paragraph = [line]
            index += 1
            while index < len(lines) and lines[index].strip() and not lines[index].startswith(("```", "| ")):
                paragraph.append(lines[index])
                index += 1
            result.append("<p>" + inline(" ".join(paragraph)) + "</p>")
            continue
        index += 1
    return "\n".join(result)


def labs():
    return '''<div class="lab" id="analysis-panel" aria-busy="false">
<div class="lab-label">Laboratorio 01 / Inspector de cadena</div><h3>Qué se observa y qué no se puede atribuir</h3>
<p>Pega una cadena de nivel o carga un archivo de texto. Todo el análisis ocurre en este navegador. Se admite texto descomprimido y Base64/Base64url con gzip o zlib; los contenedores .bro, XML y JSON no están implementados.</p>
<div class="actions" aria-label="Ejemplos sintéticos"><button type="button" data-fixture="grid">Ejemplo: rejilla</button><button type="button" data-fixture="importer">Ejemplo: marca del importador</button><button type="button" data-fixture="triggers">Ejemplo: triggers</button></div>
<label class="control-label" for="level-input">Cadena del nivel</label><textarea id="level-input" rows="7" spellcheck="false" placeholder="kS38,1;1,211,2,30.000,3,15.000,21,100,128,1.0000,129,0.5000;" aria-describedby="input-limits"></textarea>
<label class="control-label" for="level-file">Archivo local de texto (opcional)</label><input class="file-input" type="file" id="level-file" accept=".txt,.gmd,.level,text/plain">
<p class="fine" id="input-limits">Límites: 8 MiB de entrada · 32 MiB descomprimidos · 100.000 objetos · 30 s. Una cadena fuera de este subconjunto puede ser rechazada aunque otro parser la admita.</p>
<div class="actions"><button type="button" class="primary" id="analyze">Analizar localmente</button><button type="button" id="cancel-analysis" disabled>Cancelar</button><button type="button" id="download-report" disabled>Descargar informe JSON</button><button type="button" id="clear-input">Borrar entrada e informe</button></div>
<output class="status" id="analysis-status" aria-live="polite">No hay un nivel cargado. Puedes empezar con un ejemplo sintético.</output>
<div id="analysis-results" hidden><div class="results-head"><strong>Participación de IA: indeterminada</strong><p>Sin corpus calibrado ni recibo de procedencia, las regularidades de la cadena no permiten atribuir IA.</p></div>
<div class="result-grid"><div class="result-card"><span>Objetos analizados</span><strong id="object-count"></strong></div><div class="result-card"><span>IDs distintos</span><strong id="id-count"></strong></div><div class="result-card"><span>Orden de claves dominante</span><strong id="key-order"></strong></div><div class="result-card"><span>Reglas del importador</span><strong id="importer-state"></strong></div></div>
<p id="mark-explanation"></p><p class="fine" id="metrics-summary"></p>
<figure><canvas class="map" id="object-map" role="img" aria-label="Mapa de una muestra de centros de objetos; no es un render del nivel"></canvas><figcaption>Posiciones, con Y hacia arriba. Hasta 2.500 objetos muestreados uniformemente por índice. Cian: IDs de primitivas del importador; ámbar: otros IDs. El color no indica IA.</figcaption></figure>
<details open><summary>Distribución de IDs</summary><div class="table-scroll" tabindex="0" role="region" aria-label="Distribución de IDs de objetos"><table><thead><tr><th scope="col">ID</th><th scope="col">Cantidad</th><th scope="col">Proporción</th></tr></thead><tbody id="id-histogram"></tbody></table></div></details>
<details><summary>Alineación a rejillas ancladas en el origen</summary><div class="table-scroll" tabindex="0" role="region" aria-label="Alineación de posiciones"><table><thead><tr><th scope="col">Paso GD</th><th scope="col">Alineados</th><th scope="col">Proporción</th></tr></thead><tbody id="grid-results"></tbody></table></div></details>
<details><summary>Evidencias compatibles con la marca (hasta 32)</summary><div class="table-scroll" tabindex="0" role="region" aria-label="Evidencias de compatibilidad del importador"><table><thead><tr><th scope="col">Regla</th><th scope="col">Índices desde 0</th><th scope="col">Observación</th></tr></thead><tbody id="evidence-results"></tbody></table></div></details>
<details><summary>Hashes e informe completo</summary><pre class="hashes" id="hash-summary"></pre><pre class="json-report" id="report-json"></pre></details></div>
<noscript><p>El inspector requiere JavaScript. El paper y sus archivos de referencia se pueden leer y descargar sin ejecutarlo.</p></noscript></div>
<div class="lab" id="base-rate-lab"><div class="lab-label">Laboratorio 02 / Simulación hipotética</div><h3>Un positivo depende de la población</h3><p>Estos parámetros son ficticios. El resultado muestra la tasa base en una población de 10.000 casos; no mide el rendimiento de Paimbnails ni la probabilidad del nivel cargado.</p>
<div class="range-grid"><label for="prior">Prevalencia de IA<output id="prior-value" for="prior"></output><input type="range" id="prior" min="0" max="100" step="0.1" value="1"></label><label for="sensitivity">Sensibilidad hipotética<output id="sensitivity-value" for="sensitivity"></output><input type="range" id="sensitivity" min="0" max="100" step="0.1" value="90"></label><label for="false-positive">Falsos positivos hipotéticos<output id="false-positive-value" for="false-positive"></output><input type="range" id="false-positive" min="0" max="30" step="0.1" value="1"></label></div>
<div class="base-rate-bar" aria-hidden="true"><span id="true-positive-bar"></span><span id="false-positive-bar"></span></div><div class="legend"><span><i aria-hidden="true"></i>Verdaderos positivos</span><span class="false"><i aria-hidden="true"></i>Falsos positivos</span></div><output id="base-rate-result" class="status" aria-live="polite"></output><p class="fine">PPV = π × TPR / [π × TPR + (1 − π) × FPR]. Si no hay positivos esperados, PPV queda indefinido.</p></div>'''


def main():
    text = (PAPER_DIR / "paper.md").read_text()
    matches = list(re.finditer(r"^## (\d+)\. (.+)$", text, re.MULTILINE))
    assert len(matches) == 37
    content, links = [], []
    for index, match in enumerate(matches):
        number, title = match.groups()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        body = render_blocks(text[match.end():end].splitlines())
        if number == "33":
            body += labs()
        content.append(f'<section class="section" id="s{number}" aria-labelledby="heading-{number}"><div class="section-label">{number} / PTL</div><h2 id="heading-{number}">{escape(title)}</h2>{body}</section>')
        links.append(f'<li><a href="#s{number}"><span class="toc-number">{number}</span><span>{escape(title)}</span></a></li>')
    diagram = '''<figure class="diagram"><svg viewBox="0 0 440 320" role="img" aria-label="La cadena produce observaciones. La procedencia permite documentar operaciones. La incertidumbre permanece explícita."><g fill="none" stroke="#35575d"><path d="M20 10V310M60 10V310M100 10V310M140 10V310M180 10V310M220 10V310M260 10V310M300 10V310M340 10V310M380 10V310M420 10V310M10 30H430M10 70H430M10 110H430M10 150H430M10 190H430M10 230H430M10 270H430M10 310H430" opacity=".3"/></g><g font-family="monospace" font-size="12"><rect x="22" y="28" width="396" height="50" rx="3" fill="#122e32" stroke="#79e6db"/><text x="42" y="51" fill="#79e6db">01 / CADENA DEL NIVEL</text><text x="42" y="68" fill="#b8c9ca" font-size="10">1,211,2,30.000,3,15.000,21,100; …</text><path d="M220 80V110M220 108H112V130M220 108H327V130" stroke="#79e6db" fill="none"/><rect x="22" y="132" width="182" height="70" fill="#102629" stroke="#38585e"/><rect x="236" y="132" width="182" height="70" fill="#102629" stroke="#38585e"/><text x="39" y="158" fill="#e8efed">PATRONES</text><text x="39" y="181" fill="#a2b7b9" font-size="10">Geometría · grupos</text><text x="252" y="158" fill="#e8efed">PROCEDENCIA</text><text x="252" y="181" fill="#a2b7b9" font-size="10">Declaración · recibos</text><path d="M112 204V226H220M327 204V226H220V250" stroke="#79e6db" fill="none"/><rect x="22" y="253" width="396" height="51" fill="#12352c" stroke="#79e6db"/><text x="42" y="275" fill="#79e6db">INFORME CON ALCANCE</text><text x="42" y="293" fill="#b8c9ca" font-size="10">Observado · documentado · indeterminado</text></g></svg><figcaption>Arquitectura propuesta. La cadena no revela el pensamiento interno de un modelo.</figcaption></figure>'''
    toc = "<ol>" + "".join(links) + "</ol>"
    downloads = f'''<div class="downloads"><a href="{ASSETS}/paper.md" download>Paper completo en Markdown</a><a href="{ASSETS}/analyzer.js" download>Núcleo del analizador</a><a href="{ASSETS}/analysis-worker.js" download>Worker</a><a href="{ASSETS}/provenance.schema.json" download>Schema de procedencia propuesto</a><a href="{ASSETS}/source-audit.json">Inventario y hashes del código inspeccionado</a></div>'''
    html = f'''<!DOCTYPE html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Transparencia de IA en niveles de Geometry Dash | Paimbnails</title><meta name="description" content="Paper sobre análisis de cadenas de niveles, procedencia e incertidumbre del uso de IA. Algoritmo propuesto, 35 ecuaciones e inspector local; transparencia sin restringir la creación."><meta name="theme-color" content="#071114"><link rel="canonical" href="https://flozwer.org/paimbnails/papers/ai-level-transparency"><meta property="og:title" content="Transparencia de IA en niveles de Geometry Dash"><meta property="og:description" content="Análisis de cadenas, procedencia y límites de atribución. Propuesta metodológica con inspector local, para documentar el uso de IA."><meta property="og:type" content="article"><meta property="og:url" content="https://flozwer.org/paimbnails/papers/ai-level-transparency"><meta property="og:image" content="https://flozwer.org/paimbnails/logo.png"><meta name="twitter:card" content="summary"><link rel="icon" href="/paimbnails/logo.png"><link rel="stylesheet" href="{ASSETS}/styles.css?v=20261012"><script defer src="{ASSETS}/lab.js?v=20261012"></script></head><body><a class="skip-link" href="#paper-content">Saltar al contenido</a><header class="topbar"><nav class="wrap nav-inner" aria-label="Navegación principal"><a class="brand" href="/paimbnails"><img src="/paimbnails/logo.png" alt="" width="28" height="28">Paimbnails</a><div class="nav-links"><a href="/paimbnails/papers">Papers</a><a href="/paimbnails">Proyecto</a><a href="#analysis-panel">Laboratorio</a><span class="language-note">ES · EDICIÓN ORIGINAL</span></div></nav></header>
<main class="wrap"><header class="hero"><div><div class="eyebrow">Paper 02 / Procedencia y transparencia</div><h1>Transparencia de IA en niveles de Geometry Dash.</h1><p class="deck">Qué puede decir la cadena de un nivel sobre su creación, cómo documentar el uso de IA y cuándo la respuesta correcta es «indeterminado».</p><div class="hero-meta"><span class="tag accent">Propuesta metodológica · prototipo 0.1</span><span class="tag">12 oct 2026</span><span class="tag">Lectura ≈ 45 min</span><span class="tag">Español · edición original</span></div><div class="hero-actions"><a class="button primary" href="#s01">Leer el paper</a><a class="button" href="#analysis-panel">Abrir inspector</a><button id="print-paper" type="button">Imprimir / Guardar PDF</button></div></div>{diagram}</header>
<div class="principle"><strong>Crear con IA es una opción legítima.</strong> Este sistema busca documentar contribuciones y herramientas con su alcance. No propone bloqueos, sanciones ni reducción de visibilidad.</div>
<div class="facts"><div><strong>37</strong><span>secciones del método</span></div><div><strong>35</strong><span>ecuaciones numeradas</span></div><div><strong>2</strong><span>laboratorios interactivos</span></div><div><strong>Local</strong><span>la cadena permanece en tu navegador</span></div></div>
<details class="mobile-toc"><summary>Índice · 37 secciones</summary><nav aria-label="Índice móvil">{toc}</nav></details><div class="layout"><aside class="toc"><p class="toc-title">ÍNDICE / PTL 0.1</p><nav aria-label="Índice del paper">{toc}</nav></aside><article class="body" id="paper-content"><p class="article-status">Prototipo de extracción disponible. Calibración de autoría IA y verificación de recibos pendientes de investigación.</p>{''.join(content)}{downloads}</article></div></main><footer class="footer"><div class="wrap"><p>Paimbnails · Documentación técnica abierta<br>Geometry Dash pertenece a RobTop Games.</p><p><a href="/paimbnails/papers">Todos los papers</a> · <a href="/paimbnails/papers/image-to-objects">Imagen a objetos</a><br>La asistencia IA en este documento se declara en sus referencias.</p></div></footer></body></html>'''
    (PAPER_DIR / "web/index.html").write_text(html)
    files = [
        "src/features/gif-import/services/GifObjectEmitter.cpp",
        "src/features/gif-import/services/ImageWatermark.cpp",
        "src/features/gif-import/services/ImageWatermark.hpp",
        "src/features/gif-import/services/GifImportPipeline.cpp",
        "src/hooks/LevelInfoLayer.cpp",
        "src/features/editor-physics/services/PhysicsTriggerEmitter.cpp",
        "src/features/collab-editor/CollabManager.cpp",
    ]
    audit = {
        "date": "2026-10-12", "gitRevision": "a2419d80371e113e4f274c2998262f40dd91839f",
        "method": "Static source inspection; no mod compilation or runtime survival study.",
        "files": [{"path": name, "sha256": sha256((ROOT / name).read_bytes()).hexdigest()} for name in files],
        "paperSha256": sha256(text.encode()).hexdigest(),
        "analyzerSha256": sha256((PAPER_DIR / "web/analyzer.js").read_bytes()).hexdigest(),
        "empiricalAIClassificationResults": None,
    }
    (PAPER_DIR / "source-audit.json").write_text(json.dumps(audit, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"sections": len(matches), "words": len(text.split()), "htmlBytes": len(html.encode()), "status": "rendered"}))


if __name__ == "__main__":
    main()
