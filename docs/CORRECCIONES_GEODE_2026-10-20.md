# Correcciones y segunda revisión — 20 de octubre de 2026

**Seguimiento:** esta es la segunda revisión. La [tercera revisión](TERCERA_REVISION_GEODE_2026-10-20.md) documenta cuatro fallos posteriores y el paquete actualizado. Los resultados que siguen son históricos.

Se corrigieron los **ocho hallazgos de la revisión inicial**, se añadieron regresiones del comportamiento esperado y se realizó otra revisión de los cambios. También se corrigieron errores de navegación tras un guardado fallido y referencias de imágenes importadas que podían contener rutas externas. La pasada completa detectó además un fallo intermitente de publicación de archivos en Windows; se corrigió y se repitió la validación. La revisión se contrastó con el SDK local **Geode 5.11.2 / Geometry Dash 2.2081**.

El [informe inicial](REVISION_GEODE_2026-10-20.md) conserva las reproducciones del código anterior. Los resultados de este documento corresponden a las correcciones. Esta copia no contiene `.git`; se guardaron los originales y un diff para poder revisar y revertir los cambios.

## Correcciones verificadas

| Hallazgo inicial | Comportamiento corregido | Regresión |
| --- | --- | --- |
| 1. IDs importados y nombres `.`/`..` | Los dos stores generan un ID local independiente y reservan su directorio sin sobrescribir. Validan un único componente portable, nombres reservados y confinamiento antes de cargar, escribir o borrar. El índice no incorpora IDs inseguros. | `project_storage_regression`, `project_store_identity` |
| 2. Colisiones `Cube`/`cube` en Windows | Los nombres visibles ya no determinan la carpeta. Las identidades nuevas son independientes y los accesos a IDs antiguos usan la grafía canónica del índice en Windows. | `project_store_identity` |
| 3. Compilación que sobrescribía ediciones recientes | Se captura una revisión de persistencia; al terminar se rechazan proyectos editados o borrados. Solo se actualizan los metadatos del proyecto vigente y se propagan los errores de guardado. Los callbacks de UI se liberan en el hilo principal, también al cerrar el juego. | `icon_compile_revision` |
| 4. Dependencias que no se encontraban en CI | El resolver compartido usa las fuentes elegidas por CMake, el entorno `CPM_SOURCE_CACHE`, la caché de la acción oficial y `_deps`. También se probó una caché restaurada con varios hashes. | `dependency_paths_regression` |
| 5. Ciclos de referencias en cargas y popups | `RetainedLazyTextureLoad` conserva la operación desde su estado y el callback del sprite usa una referencia débil. Completar/cancelar libera el callback del usuario; se mantiene una referencia temporal durante llamadas reentrantes. Se corrigieron también Modly e IconNamePopup. | `lazy_texture_regression` |
| 6. Miniaturas antiguas publicadas en una solicitud nueva | Cada render lleva un token único; invalidar retira su autoridad para entregar. Los callbacks de UI permanecen en el hilo principal y los errores no publican un render incompleto. | `icon_thumbnail_generation` |
| 7. Galería oculta al cambiar de foto | Cada solicitud usa un LazySprite nuevo; se retira el anterior y se comprueba la generación y la vida del popup antes de mostrar el resultado. | `mod_gallery_navigation` |
| 8. Contradicción SDK/CI y auditoría sin ejecutar | CI usa `sdk: given`, ejecuta la auditoría de bindings después de compilar y activa las advertencias del proyecto. Se desactiva LTO en la acción, de acuerdo con el CMake del mod. | Suite `bindings`: 3/3 |

La segunda revisión añadió estas correcciones con regresiones:

- **Guardados fallidos:** Icon Maker y Texture Studio conservan el editor y el mensaje de error si no pueden guardar antes de salir, probar, aplicar, exportar o abrir Fusion. `editor_save_failure` comprueba que no se navegue después de ese fallo.
- **Referencias de imágenes:** la deserialización de IconProject rechaza rutas absolutas, separadores, `.`/`..` y otros componentes no portables en los archivos de formas/rellenos. `icon_asset_paths` comprueba la validación real.

- **Selecciones antiguas de icono:** se filtran IDs inseguros guardados antes de esta validación y se protege la carga de hojas antes de resolver su ruta. La nueva frontera de rutas devuelve un error seguro en lugar de provocar una excepción en el hook del jugador. `icon_active_selection` comprueba el fallback sin acceso al disco para IDs inválidos.
- **Escrituras simultáneas en Windows:** se reprodujo `MoveFileExW` con error 5 cuando dos hilos publicaban el mismo destino, aun con temporales distintos y cerrados. Se serializa únicamente la publicación dentro del proceso; la escritura de temporales continúa en paralelo. Se conservan los rechazos de archivos bloqueados y directorios. La regresión ampliada pasó 20 veces, con dos escritores de 128 operaciones en cada pasada (**5.120 publicaciones concurrentes sin fallos**). [Prueba de estrés](../build-clang/fixes-review-2026-10-20/atomic-stress.json).

Los stores conservan los proyectos antiguos con IDs válidos. No se migraron ni modificaron los datos instalados del usuario. No es posible reconstruir automáticamente datos que ya hubieran sido sobrescritos antes de estas correcciones. Un fallo físico al eliminar archivos se informa; el índice y el borrado de directorios no constituyen una transacción del sistema de archivos.

## Optimizaciones aplicadas

- **GIF:** se mueven los vectores de frames al insertarlos, evitando copias completas. El banco del código real, con 80 frames de 512×512, Clang `-O2` y cinco muestras tras calentamiento, pasó de **88.6 ms a 50.0 ms** de mediana (**43.6% menos**). El hash de todos los píxeles coincide: `16692164471478913475`. La validación concurrente introduce ruido; esta medición es un caso CPU sintético y no demuestra una mejora de FPS. [Datos del banco](../build-clang/fixes-review-2026-10-20/gif-benchmark.json).
- **Miniaturas:** caché LRU limitada a 128 texturas en escritorio y 64 en Android/iOS; son aproximadamente 8/4 MiB de píxeles RGBA de 128×128, sin contar overhead. La fixture solicita 200 entradas y comprueba el límite y la regeneración de entradas expulsadas. Se limpia durante el cierre.
- **FramePixelCache:** guarda los metadatos junto a los píxeles y evita recorrer el atlas en un acierto; la consulta del presupuesto comparte el mutex de las escrituras. Se conserva el bloqueo durante la carga del atlas: separar esa operación requiere medir contención y validar su publicación concurrente.

## Resultados de la segunda revisión

| Validación | Resultado |
| --- | --- |
| Suite portable completa | **61/61 comprobaciones aprobadas** |
| Bancos C++ incluidos en portable | **25**, compilados y ejecutados con ASan/UBSan |
| Fixtures de comportamiento incluidas en portable | **23/23**, con ASan/UBSan; no se suman otra vez a las 61 |
| Auditoría de bindings reales del build | **3/3 aprobadas** |
| clang-tidy con argumentos del build Windows | **31 unidades C++**, sin errores ni timeouts |
| Compilación y enlace Windows | **Correctos**, SDK 5.11.2; `/W4` en 18 unidades modificadas |
| Empaquetado con Geode CLI | **Correcto**, paquete Windows `v1.2.0` |

Se revisaron los avisos del analizador: padding, asignaciones sin uso, listeners deliberadamente permanentes del SDK y diagnósticos en las bibliotecas fmt/MSVC. No se presentan como una compilación libre de advertencias ni como defectos de ejecución demostrados. La revisión no encontró otro fallo reproducible pendiente en los flujos corregidos.

Una ejecución previa agotó el límite de 120 segundos al compilar el banco de la guía con sanitizadores mientras se analizaba y compilaba en paralelo. Se amplió exclusivamente su límite de compilación a 240 segundos y se volvió a ejecutar la suite completa. Se conservan el timeout, el fallo posterior de escritura simultánea y el resultado final corregido; no se convirtió ningún fallo en `SKIP`.

El paquete Zstd contiene 636 entradas. Se verificó la integridad CRC de todas las entradas de un paquete Deflate equivalente, que sus tamaños/CRC coinciden con el Zstd y que su DLL coincide con la recién enlazada. El CLI generó el Zstd correctamente; Python 3.13 no descomprime su método ZIP 93, por lo que esa comparación no se presenta como una descompresión completa del Zstd. [Verificación del paquete](../build-clang/fixes-review-2026-10-20/package-verification.json).

## Límites de esta validación

Se compiló con el grafo Ninja ya configurado para este SDK. La reconfiguración normal invocaba Geode CLI, que intentó escribir su configuración global fuera del workspace permitido. Se reutilizó el grafo de dependencias y se recompilaron las unidades afectadas; no se cambió CMakeLists ni se instaló el mod. El [método de build](../build-clang/fixes-review-2026-10-20/build-method.json) y el [script](../build-clang/fixes-review-2026-10-20/build_configured.py) explican el procedimiento. Esto no sustituye un build limpio de CI.

No se ejecutaron Geometry Dash, Android, Apple, pruebas GPU ni el workflow remoto. Los sanitizadores cubren los bancos independientes, con dobles del motor/red cuando corresponden, no todo el proceso del juego. Quedan por validar en el juego los recorridos visuales de importación/duplicación/borrado, guardado con disco no disponible, cambio rápido de foto/miniatura y cierre con cargas activas, además de la convivencia con otros mods.

## Contraste con Geode y evidencias

Se conservaron los contratos de Cocos de propiedad y ejecución en el hilo principal, contrastados con el SDK instalado y la documentación oficial de [async](https://github.com/geode-sdk/docs/blob/main/tutorials/async.md) y [Ref/WeakRef](https://docs.geode-sdk.org/tutorials/utils/). La política de SDK y caché de CI sigue los inputs y el entorno de la [acción oficial](https://github.com/geode-sdk/build-geode-mod/blob/main/action.yml). El ABI validado es el de la [release 5.11.2](https://github.com/geode-sdk/geode/releases/tag/v5.11.2); el proyecto separado de GD 2.2091 queda fuera de este trabajo.

- [Resultados portables](../build-clang/fixes-review-2026-10-20/portable.json), [fixtures](../build-clang/fixes-review-2026-10-20/fixtures.json), [bindings](../build-clang/fixes-review-2026-10-20/bindings.json) y [análisis estático](../build-clang/fixes-review-2026-10-20/static-final.json).
- [Log de la suite completa](../build-clang/fixes-review-2026-10-20/portable-complete.log), [build](../build-clang/fixes-review-2026-10-20/build.log) y [paquete](../build-clang/fixes-review-2026-10-20/package-final.log).
- [Diff de los cambios](../build-clang/fixes-review-2026-10-20/changes.diff), [originales](../build-clang/fixes-review-2026-10-20/before/) e [inventario final con hashes](../build-clang/fixes-review-2026-10-20/final-inventory.json).
- [Paquete Windows de esta revisión](../build-clang/deep-review-2026-10-20/before-flozwer.paimbnails2.geode). SHA-256: `5b52286fdd2fe73778544bfaeed70f5a57a85038dbda97ff9f40dd81ce56d76c`.

Para repetir las suites principales:

```powershell
& './build-clang/tools/python/python.exe' tests/run_regressions.py --suite portable --build-dir build-clang --cxx 'C:/Program Files/LLVM/bin/clang++.exe' --sanitize
& './build-clang/tools/python/python.exe' tests/run_regressions.py --suite bindings --build-dir build-clang --sdk "$env:GEODE_SDK"
```
