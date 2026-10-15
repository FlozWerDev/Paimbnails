# Transparencia de IA en niveles

Paper técnico en español y prototipo local para flozwer.org. El núcleo calcula rasgos de cadenas de Geometry Dash y compatibilidad con la marca del importador de Paimbnails. No atribuye IA, identifica modelos ni verifica firmas; esas capas se especifican como investigación pendiente.

- `paper.md`: texto original, 37 secciones y 35 ecuaciones.
- `web/index.html`: edición web estática con dos laboratorios.
- `web/analyzer.js`: parser, descompresión acotada, métricas y hashes.
- `web/analysis-worker.js`: ejecución cancelable fuera del hilo de interfaz.
- `web/lab.js`: inspector, descarga JSON y simulador de tasa base.
- `provenance.schema.json`: estructura propuesta de manifiestos; no valida veracidad ni firmas.
- `source-audit.json`: revisión y hashes de los archivos del mod inspeccionados.

Regenerar la edición web y el inventario de fuentes:

```sh
python3 docs/papers/level-ai-transparency/render.py
```

Verificar el analizador con Node y APIs web disponibles:

```sh
node docs/papers/level-ai-transparency/check-analyzer.mjs
```

Estos comandos no compilan el mod. El parser admite un subconjunto documentado: cadena descomprimida o Base64/Base64url con gzip/zlib, hasta 8 MiB de entrada, 32 MiB descomprimidos y 100.000 objetos. El formato `.bro` no se declara compatible sin especificación y fixtures.

Ruta del paper: `/paimbnails/papers/ai-level-transparency`. Los assets se sirven desde `/request-assets/papers/ai-level-transparency/`. La edición original está en español; los enlaces del catálogo se traducen a español, inglés y alemán.

El análisis se ejecuta en el navegador sin solicitudes de red con el nivel. El CSP del paper limita conexión y ejecución al alcance necesario. Sus fixtures son sintéticos; no sirven para estimar precisión de detección de IA.
