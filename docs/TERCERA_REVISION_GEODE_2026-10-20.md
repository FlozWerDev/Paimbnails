# Tercera revisión de Geode — 20 de octubre de 2026

Se encontraron y corrigieron **cuatro fallos adicionales**. La revisión se centró en la publicación de iconos compilados, las vistas previas automáticas y la integración con sus consumidores. Se conservan las correcciones de la [segunda revisión](CORRECCIONES_GEODE_2026-10-20.md). El SDK validado sigue siendo **Geode 5.11.2 / Geometry Dash 2.2081**.

## Hallazgos y correcciones

### 1. P1 — Una compilación fallida o descartada podía sobrescribir la salida válida

`IconCompiler::compile()` escribía cada PNG/plist directamente en `output`. Una falla al producir HD dejaba UHD sobrescrito y las demás calidades antiguas. La comprobación de revisión en `IconBuildService` ocurría después de esas escrituras; un worker descartado también podía alterar los archivos de una compilación más reciente.

La reproducción ejecuta la función real y simula un error de escritura HD. La versión anterior modificó archivos que debían permanecer intactos: [fallo reproducido](../build-clang/deep-review-2026-10-20/icon-output-before.log).

Ahora cada compilación escribe en un directorio privado independiente del proyecto. El hilo principal verifica existencia y revisión, publica el conjunto completo y actualiza los metadatos. Se restauran la salida y las marcas de compilación si falla el guardado, incluido el caso en que se escribió el proyecto y falló después su índice. Los resultados descartados y las compilaciones fallidas limpian sus temporales. Si el sistema bloquea la restauración, se conserva la copia anterior y se registra su ruta.

La nueva clase [StagedDirectory](../src/utils/StagedDirectory.hpp) conserva la propiedad de los temporales y permite confirmar o restaurar una publicación. Se prueban commit, abandono, destinos inválidos, fuente ausente, restauración bloqueada y recuperación posterior. La fixture `icon_output_publication` comprueba el compilador real con render/escritura de calidades simulados; `icon_compile_revision` comprueba cuándo se permite publicar y qué ocurre ante errores de persistencia. La fixture de directorios utiliza operaciones reales del sistema de archivos.

### 2. P2 — Las vistas previas convertían incorrectamente rutas Unicode

`AutoPreviewStore::dir()` convertía el path nativo de Geode a texto UTF-8 y lo reconstruía con el constructor de ruta estrecha de Windows. En la prueba con un directorio que contiene `ñ` y `中`, el resultado apuntaba a otra ruta.

Se conserva el `std::filesystem::path` nativo. No cambian el formato RGB ni la ubicación prevista de las vistas previas. [Reproducción anterior](../build-clang/deep-review-2026-10-20/autopreview-before.log).

### 3. P2 — Dos guardados automáticos compartían el mismo temporal

`AutoPreviewStore::save()` utilizaba siempre `<nivel>.rgb.tmp`. Dos escritores podían abrir, truncar, renombrar o eliminar el temporal del otro. La reproducción registró **12 fallos en 128 escrituras**; el último payload de esa ejecución quedó íntegro, por lo que no se afirma haber observado corrupción de píxeles en ella.

Ahora se enlaza el escritor atómico existente, con temporales únicos, y se serializan la publicación y la actualización del índice de presencia frente a `clearAll()`. Se verifica que las 128 escrituras terminen correctamente y que el archivo final contenga un frame completo de uno de los dos escritores. También se conserva la imagen y la caché si el destino está bloqueado.

### 4. P2 — Guardar una vista previa nueva podía devolver la textura antigua

La caché propia de `AutoPreviewStore` conservaba la textura anterior aunque se actualizara el archivo y se invalidaran las cachés externas. La siguiente consulta podía devolverla otra vez.

Cada guardado correcto marca el ID como pendiente de renovar. La siguiente consulta en el hilo principal retira la textura antigua y permite cargar la nueva. El worker solo modifica estado protegido por mutex; la prueba comprueba que no libere referencias gráficas. También se verificó que `ThumbnailLoader::invalidateLevel()` invalide la caché exterior de `LocalThumbs`.

## Validación final

| Comprobación | Resultado |
| --- | --- |
| Suite portable completa | **61/61 aprobadas** |
| Bancos C++ incluidos | **25**, compilados y ejecutados con ASan/UBSan |
| Fixtures de comportamiento incluidas | **26/26**, con ASan/UBSan; no se suman de nuevo a las 61 |
| Bindings reales | **3/3 aprobadas** |
| clang-tidy en esta pasada | **4 unidades**: AutoPreviewStore, IconCompiler, IconBuildService y MoreIconsBridge; sin errores |
| Windows | Compilación incremental y enlace correctos; `/W4` conservado en 20 unidades modificadas entre ambas pasadas |
| Paquete | Geode CLI: correcto; **636 entradas**, DLL verificada |

La prueba que simulaba una restauración bloqueada falló inicialmente porque el archivo usado como obstáculo no forzó el error esperado en este Windows. Se aisló la operación, se cambió a un directorio no vacío y se volvieron a ejecutar las 26 fixtures y la suite portable completa. Se conserva el [resultado anterior](../build-clang/deep-review-2026-10-20/portable-before-fixture-adjustment.log) y el [diagnóstico](../build-clang/deep-review-2026-10-20/directory-diagnostic.log); no se ocultó ni omitió el fallo.

Los artefactos Zstd y Deflate equivalentes tienen los mismos nombres, tamaños y CRC de entradas. Se comprobaron todos los CRC del Deflate y que su DLL coincide con la recién compilada. Python 3.13 no descomprime ZIP 93; no se presenta esta comparación como una descompresión completa del Zstd. [Verificación del paquete](../build-clang/deep-review-2026-10-20/package-verification.json).

## Alcance y límites

Se reutilizó el grafo Ninja configurado para el SDK instalado, por la restricción de escritura de Geode CLI durante la reconfiguración descrita en la revisión anterior. No se modificó CMakeLists ni se instaló el mod. No se ejecutaron Geometry Dash, Android, Apple, GPU ni CI remoto. Los sanitizadores instrumentan los bancos independientes, con dobles del motor donde corresponde.

La sustitución de directorios es reversible durante esta operación, pero no es una transacción durable ante un corte de energía o terminación forzada. Las restauraciones y limpiezas pueden encontrar errores del sistema de archivos; la copia anterior se preserva cuando no puede restaurarse.

El trabajo conserva los contratos de propiedad y del hilo principal de la documentación oficial de [async](https://github.com/geode-sdk/docs/blob/main/tutorials/async.md) y [Ref/WeakRef](https://docs.geode-sdk.org/tutorials/utils/), contrastados con el SDK local [5.11.2](https://github.com/geode-sdk/geode/releases/tag/v5.11.2).

## Evidencias y entrega

- [Suite portable](../build-clang/deep-review-2026-10-20/portable.json), [26 fixtures](../build-clang/deep-review-2026-10-20/fixtures.json), [bindings](../build-clang/deep-review-2026-10-20/bindings.json) y [análisis estático](../build-clang/deep-review-2026-10-20/static-final.json).
- [Log portable completo](../build-clang/deep-review-2026-10-20/portable-complete.log), [build](../build-clang/deep-review-2026-10-20/build.log), [método de build](../build-clang/deep-review-2026-10-20/build-method.json) y [empaquetado](../build-clang/deep-review-2026-10-20/package.log).
- [Diff de esta pasada](../build-clang/deep-review-2026-10-20/changes.diff), [originales](../build-clang/deep-review-2026-10-20/before/) e [inventario final](../build-clang/deep-review-2026-10-20/final-inventory.json).
- [Paquete Windows actualizado](../build-clang/flozwer.paimbnails2.geode), SHA-256 `db1544aeb05255212089355432a6e88116b923e7b20cbb3e7d45c878eae18e9a`.

Para repetir la validación:

```powershell
& './build-clang/tools/python/python.exe' tests/run_regressions.py --suite portable --build-dir build-clang --cxx 'C:/Program Files/LLVM/bin/clang++.exe' --sanitize
& './build-clang/tools/python/python.exe' tests/run_regressions.py --suite bindings --build-dir build-clang --sdk "$env:GEODE_SDK"
```
