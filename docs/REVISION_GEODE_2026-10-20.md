# Revisión de Geode y del código — 20 de octubre de 2026

En la revisión inicial se encontraron **ocho defectos**: tres con riesgo de pérdida de datos, un fallo de resolución de dependencias en CI, fugas de memoria, dos errores de resultados/carga de imágenes y una auditoría del SDK que aborta por una contradicción con el workflow. Se documentan las condiciones necesarias; las pruebas aisladas no equivalen a una sesión del juego.

**Estado histórico: diagnóstico previo a las correcciones.** Los resultados de este documento describen el código anterior. Los ocho hallazgos fueron corregidos; los cambios, las regresiones y los límites de la segunda revisión están en [Correcciones y segunda revisión](CORRECCIONES_GEODE_2026-10-20.md). Se conserva aquí la evidencia original de los fallos. Lo que sigue no describe el estado actual del código.

## Alcance y verificación

- Inventario global: **1.458 archivos C/C++ de `src`, 354.309 líneas**, incluidos los encabezados stb incorporados. Búsquedas de patrones y trazado manual de rutas seleccionadas de persistencia, imágenes, callbacks, hilos, hooks, audio/vídeo y arranque. No se afirma una inspección manual de cada línea o de cada módulo.
- Comparación por hashes con el inventario de la revisión anterior, sin repetir sus defectos ya corregidos. Esta copia no tiene `.git`, por lo que no se atribuyen cambios a commits.
- **20 unidades C++ analizadas con clang-tidy y los argumentos reales del build Windows**: cero errores y cero timeouts; 34 avisos brutos revisados. Los avisos de padding, bindings o listeners deliberadamente permanentes no se cuentan automáticamente como bugs.
- **56/56 comprobaciones portables aprobadas**, incluidas la compilación y ejecución de 23 bancos C++ con ASan/UBSan y las 16 fixtures existentes de callbacks/comportamiento incluidas en la suite.
- **Bindings: 2/3 comprobaciones aprobadas**. La auditoría general aborta por el hallazgo 8. Una auditoría independiente de sus contratos comprobó **262 headers, 94 bases `$modify`, 145 objetivos de prioridad y los pins de arc/asp**, sin cambiar ni omitir el fallo de la suite original.
- Seis bancos C++ nuevos, con ASan/UBSan, confirman los defectos de rutas, identidad en Windows, snapshot de compilación, ciclo de referencias, generación de miniaturas y reutilización de la galería. Un banco Python confirma el problema de rutas de dependencias de CI.

Los bancos nuevos terminan con código 0 cuando **reproducen el defecto actual**. No son regresiones que demuestren el comportamiento deseado ni correcciones aprobadas. Extraen funciones actuales de producción o incluyen el header real y simulan únicamente las dependencias necesarias. Los hashes de las funciones extraídas están en [extracted-methods.json](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/extracted-methods.json).

No se recompiló/empaquetó/instaló el mod completo, no se abrió Geometry Dash y no se enviaron solicitudes a sus servidores privados. No se ejecutaron Android, Apple ni pruebas GPU. Los sanitizadores cubren los bancos independientes, no el proceso completo del juego.

## Hallazgos por prioridad

### 1. P1 — Los proyectos importados pueden dirigir escrituras y borrados fuera de su carpeta

**Texture Studio:** [ProjectShare.cpp:39](F:/code/Paimbnails-main/Paimbnails-main/src/features/texture-studio/persist/ProjectShare.cpp:39) conserva el `id` del JSON. La rama de proyectos live pasa por `normalize()`, que no valida ese ID, y llama a `createSlot()`. [SlotStore.cpp:168](F:/code/Paimbnails-main/Paimbnails-main/src/features/texture-studio/persist/SlotStore.cpp:168) solo exige que no esté vacío. [SlotPaths.cpp:39](F:/code/Paimbnails-main/Paimbnails-main/src/features/texture-studio/persist/SlotPaths.cpp:39) concatena el ID como una ruta; [SlotStore.cpp:277](F:/code/Paimbnails-main/Paimbnails-main/src/features/texture-studio/persist/SlotStore.cpp:277) usa ese mismo destino en `remove_all()` al borrar el proyecto.

Un ID relativo con `..` puede salir de `texture-studio/slots`. La importación escribe `project.json` en el destino resuelto; borrar después el proyecto puede eliminar recursivamente esa carpeta. Tampoco se rechazan rutas absolutas.

**Icon Maker:** [IconShare.cpp:73](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/services/IconShare.cpp:73) elimina el ID importado, pero lo vuelve a derivar del nombre. [IconPaths.cpp:65](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/persist/IconPaths.cpp:65) permite que `.` y `..` sean el nombre completo. Un archivo `.paimbicon` con un ID original válido y nombre `..` llega a `createProject()` con esa identidad. [IconProjectStore.cpp:297](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/persist/IconProjectStore.cpp:297) puede borrar el directorio raíz de Icon Maker. El filtro de nombres del diálogo normal excluye puntos; **esa protección no se aplica a la importación**.

**Evidencia:** [import_paths.log](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/import_paths.log) ejecuta los métodos reales de resolución y borrado con dobles mínimos del store. Se verificaron primero los destinos absolutos dentro de un sandbox nuevo. El ID de Texture Studio alcanzó una carpeta ficticia ajena al slot; el nombre `..` de Icon Maker alcanzó la raíz ficticia y eliminó otro proyecto ficticio. No se importó un ZIP mediante la UI ni se tocaron datos del usuario; el recorrido de deserialización/importación se contrastó en el código.

**Corrección:** generar una identidad local independiente del nombre y del ID importado; rechazar IDs que no sean un componente seguro. Comprobar que todo destino normalizado permanezca dentro del directorio de slots antes de escribir o borrar. Centralizar esa regla en la frontera de persistencia, incluida la carga de índices y las importaciones. Añadir regresiones para `.`/`..`, rutas absolutas, separadores y proyectos válidos.

### 2. P1 — Dos iconos cuyos nombres solo cambian en mayúsculas comparten archivos en Windows

Ubicaciones: [IconProjectStore.cpp:138](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/persist/IconProjectStore.cpp:138), [IconProjectStore.cpp:178](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/persist/IconProjectStore.cpp:178) y [IconPaths.cpp:65](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/persist/IconPaths.cpp:65).

`sanitizeFilename()` conserva mayúsculas y `exists()` compara strings de forma sensible a ellas. Por tanto, `makeUniqueId()` acepta `Cube` y `cube` como proyectos distintos. En un directorio Windows normal ambos resuelven al mismo `project.json`. La segunda creación sobrescribe la primera; borrar uno afecta los archivos del otro. La caché de proyectos en memoria puede ocultar inicialmente la colisión.

**Evidencia:** [icon_case_collision.log](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/icon_case_collision.log) usa las funciones reales de saneado, unicidad y resolución. En este sistema de archivos Windows, `filesystem::equivalent()` confirma el mismo destino; la segunda escritura ficticia cambia el contenido leído desde el primer ID. No se ejecutó el editor real. La condición depende de la sensibilidad a mayúsculas del sistema de archivos.

**Corrección:** usar IDs internos nuevos y estables, separados del nombre visible. Si se conserva la identidad por nombre, la comprobación de colisiones debe considerar la semántica real del sistema de archivos y los archivos ya existentes, además del índice. Prever recuperación de proyectos que ya compartan un destino.

### 3. P1 — Una compilación de iconos sobrescribe cambios guardados mientras estaba pendiente

Ubicaciones: [IconBuildService.cpp:17](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/services/IconBuildService.cpp:17) y [IconBuildService.cpp:35](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/services/IconBuildService.cpp:35). Rutas de usuario: [IconEditorLayer.cpp:351](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/ui/IconEditorLayer.cpp:351) y [IconEditorLayer.cpp:1537](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/ui/IconEditorLayer.cpp:1537).

`runCompile()` captura una copia completa del proyecto. Al terminar el worker cambia sus metadatos de compilación y guarda **esa copia antigua** con `saveProject()`. Durante la espera el editor permite modificar el proyecto; el autosave y la salida guardan esas modificaciones. `m_compileBusy` bloquea otra compilación/preview, pero no las ediciones ni `onBack()`.

**Reproducción funcional:** iniciar Aplicar/Exportar, editar y guardar/salir antes de que llegue la respuesta. La finalización puede reemplazar el proyecto guardado por el snapshot inicial, con una fecha de modificación más reciente. El reemplazo atómico del archivo no evita esta pérdida lógica.

**Evidencia:** [icon_compile_snapshot.log](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/icon_compile_snapshot.log) ejecuta `runCompile()` extraído sin cambios, con colas deterministas de worker y main. Guarda una revisión nueva antes de entregar la finalización; el store termina con el contenido antiguo. El compilador y el store son dobles, no el motor ni archivos reales de proyectos.

**Corrección:** asociar la compilación a una revisión de contenido. Al completar, comprobar que el proyecto siga existiendo y actualizar solo los metadatos compatibles con la revisión compilada. Conservar las ediciones más nuevas y marcar el resultado como desactualizado cuando corresponda. Una cancelación/borrado no debe recrear el proyecto desde el snapshot.

### 4. P1 — Las regresiones no encuentran las dependencias que prepara la acción oficial de CI

Ubicaciones: [run_regressions.py:78](F:/code/Paimbnails-main/Paimbnails-main/tests/run_regressions.py:78), [run_regressions.py:87](F:/code/Paimbnails-main/Paimbnails-main/tests/run_regressions.py:87), [review_behavior_regression.py:76](F:/code/Paimbnails-main/Paimbnails-main/tests/review_behavior_regression.py:76) y [build.yml:67](F:/code/Paimbnails-main/Paimbnails-main/.github/workflows/build.yml:67).

La acción oficial de build exporta `CPM_SOURCE_CACHE` hacia `workspace/cpm-cache`. El workflow habilita esa caché. Los buscadores de fmt/RapidFuzz solo consultan `<build>/cpm-cache` y `ROOT/build/cpm-cache`; las fixtures buscan matjson/Result en esas mismas carpetas. No consultan la variable exportada ni la ubicación real preparada por la acción. Véase la [implementación oficial de build-geode-mod](https://raw.githubusercontent.com/geode-sdk/build-geode-mod/main/action.yml).

Mover las pruebas después del build resuelve el orden de descarga, pero no esta diferencia de rutas. En un runner limpio con el layout de la acción, los bancos de Animate/guía no encuentran headers y las fixtures pueden abortar por falta de fmt. En esta máquina la caché antigua bajo `build/cpm-cache` oculta el defecto.

**Evidencia:** [ci_source_cache.log](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/ci_source_cache.log) construye el layout de la acción en un directorio temporal, establece `CPM_SOURCE_CACHE` y coloca los headers requeridos en él. Las dos funciones actuales devuelven `None`. No se lanzó una ejecución remota de GitHub Actions.

**Corrección:** compartir una resolución de dependencias que respete `CPM_SOURCE_CACHE` y/o rutas explícitas obtenidas de CMake. Usarla también en las fixtures de matjson/Result. Verificar el workflow desde un checkout limpio sin la caché local heredada.

### 5. P2 — Los callbacks conservan ciclos de referencias y retienen texturas/pantallas

Principal: [RetainedLazyTextureLoad.hpp:43](F:/code/Paimbnails-main/Paimbnails-main/src/utils/RetainedLazyTextureLoad.hpp:43). Consumidores: [LevelCell.cpp:668](F:/code/Paimbnails-main/Paimbnails-main/src/hooks/LevelCell.cpp:668) y [MenuLayer.cpp:543](F:/code/Paimbnails-main/Paimbnails-main/src/hooks/MenuLayer.cpp:543).

El wrapper asigna a `LazySprite` un callback que captura un `Ref` al propio sprite. El ciclo es `sprite → callback → Ref<sprite>`. El callback también posee el estado y el callback del consumidor. En el SDK local **5.11.2**, `postInit()` y `onError()` llaman al callback almacenado sin vaciarlo, y `cancelLoad()` tampoco lo elimina. Poner `state->loader = nullptr` o destruir el wrapper no rompe el `loaderRef` que permanece en el callback.

Esto conserva la textura; en MenuLayer el callback del consumidor también captura referencias fuertes al menú y al contenedor. La memoria puede seguir creciendo al cambiar fondos o recorrer imágenes aunque se cierren sus pantallas.

El mismo patrón aparece en [ModlyUIHelpers.cpp:67](F:/code/Paimbnails-main/Paimbnails-main/src/features/compat-mods/ui/ModlyUIHelpers.cpp:67). Otro ciclo del mismo tipo está en [IconNamePopup.cpp:58](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/ui/IconNamePopup.cpp:58): el popup posee el botón y el callback del botón captura un `Ref` al popup. Es distinto de una retención temporal necesaria para una operación externa.

**Evidencia:** [lazy_reference_cycle.log](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/lazy_reference_cycle.log) incluye el header de producción y usa un doble del contador de referencias/callback persistente del SDK. Después de completar y destruir 25 wrappers siguen vivos 25 sprites y su contexto; cancelar antes de entregar también deja otro sprite vivo. El banco libera luego sus fixtures explícitamente. No mide la RAM del juego. Los contratos y el hash de `LazySprite.cpp` están en [sdk-contracts.json](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/sdk-contracts.json).

**Corrección:** mantener la propiedad de la operación desde su propietario y capturar referencias débiles cuando el callback pertenece al propio objeto o a un hijo suyo. Liberar el estado/callback al completar o cancelar con una secuencia que mantenga vivo el objeto durante la invocación. Revisar estos ciclos de propiedad sin sustituir indiscriminadamente las referencias de Cocos por `shared_ptr`.

### 6. P2 — Una miniatura antigua consume los callbacks de una solicitud nueva

Ubicaciones: [IconThumbs.cpp:39](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/services/IconThumbs.cpp:39), [IconThumbs.cpp:78](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/services/IconThumbs.cpp:78) y [IconThumbs.cpp:91](F:/code/Paimbnails-main/Paimbnails-main/src/features/icon-maker/services/IconThumbs.cpp:91).

La finalización solo comprueba si existe `m_pending[projectId]`. Si se invalida la petición A y se solicita B para el mismo proyecto, esa entrada vuelve a existir. La finalización de A toma los callbacks de B, publica su textura antigua y borra la entrada. Después B se descarta porque ya no quedan callbacks. Los guardados del editor llaman a `invalidate()`, así que la secuencia es alcanzable al editar y volver a la galería mientras se procesa una miniatura.

**Evidencia:** [icon_thumb_generation.log](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/icon_thumb_generation.log) ejecuta los métodos reales con colas deterministas. La solicitud de revisión 2 recibe la revisión 1; el resultado correcto se descarta y la caché conserva la versión antigua.

**Corrección:** identificar cada trabajo con un token/generación o con una entrada compartida cuya identidad se verifique al entregar. Invalidar debe retirar también la autoridad del worker anterior para publicar. La revisión debe acompañar a la clave del proyecto.

### 7. P2 — Cambiar de foto en Mod Images deja la imagen oculta

Ubicación: [ModPreviewGalleryPopup.cpp:68](F:/code/Paimbnails-main/Paimbnails-main/src/features/mod-previews/ui/ModPreviewGalleryPopup.cpp:68).

`openIndex()` reutiliza `m_sprite`, lo oculta y vuelve a llamar a `loadFromUrl()`. En Geode 5.11.2 esa función retorna si `m_hasLoaded` es verdadero. Una primera carga correcta establece ese estado; `cancelLoad()` no lo restablece y aquí solo se llama si hay una carga en curso. Al avanzar después de que se vea la primera foto, no se inicia otra solicitud ni se llama al nuevo callback: la imagen queda invisible aunque el contador avance.

**Evidencia:** [gallery_reuse.log](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/gallery_reuse.log) ejecuta `openIndex()` sin cambios frente al contrato del SDK: después de pedir la foto 2 solo existe una solicitud y el sprite está oculto. No se descargaron imágenes ni se abrió la galería real.

**Corrección:** crear un `LazySprite` nuevo por foto/solicitud o usar una ruta de carga con reemplazo explícito y token de generación. Retirar el anterior al cambiar de imagen. Cubrir el avance después de completar y los cambios rápidos durante una carga.

### 8. P2 — La auditoría del SDK aborta antes de revisar los headers por una contradicción con CI

Ubicaciones: [geode_sdk_compat_regression.py:58](F:/code/Paimbnails-main/Paimbnails-main/tests/geode_sdk_compat_regression.py:58) y [build.yml:59](F:/code/Paimbnails-main/Paimbnails-main/.github/workflows/build.yml:59).

El test exige literalmente `sdk: given`, pero el workflow usa `sdk: latest`. La suite real falla con `AssertionError` antes de comprobar includes, bases y prioridades. El workflow ejecuta la suite portable, que no contiene esta comprobación de bindings.

**Evidencia:** [geode_sdk_compat_regression.log](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/geode_sdk_compat_regression.log) y [bindings.json](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/bindings.json). La comprobación independiente de headers/hooks pasa: [sdk-independent-audit.json](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/sdk-independent-audit.json). Se conservó el resultado original fallido.

Esto no demuestra incompatibilidad actual del binario: hoy el SDK local y la release oficial son 5.11.2. Es una discrepancia verificable entre la política declarada por el test y la configuración del workflow. La acción oficial distingue `given` (versión del manifiesto) de `latest` (release estable disponible).

**Corrección:** acordar esa política y comprobar su significado, además del texto del YAML. `given` permite reproducir el ABI revisado del manifiesto. Si se mantiene `latest`, la auditoría debe validar la versión realmente utilizada y los pins de dependencias compatibles. Incorporar la suite de bindings al flujo de validación después de configurar el build.

## Optimizaciones con evidencia y mejoras de mantenimiento

1. **Evitar copias completas de frames GIF.** [GIFDecoder.cpp:183](F:/code/Paimbnails-main/Paimbnails-main/src/utils/GIFDecoder.cpp:183) copia `frame.pixels` al insertar el frame; ya se había copiado el canvas a ese vector. [GIFDecoder.cpp:187](F:/code/Paimbnails-main/Paimbnails-main/src/utils/GIFDecoder.cpp:187) también copia el frame crudo anterior. Una variante experimental generada únicamente bajo el build usa movimiento en esos dos puntos. En un banco CPU de 80 frames compuestos de 512×512, Clang `-O2`, cinco muestras tras calentamiento, la mediana pasó de **35,0 ms a 15,3 ms**. El hash de todos los píxeles coincide. Son aproximadamente 56 % menos tiempo en **ese caso**, no una mejora de FPS del juego ni una garantía para GIF arbitrarios. La primera copia evitable representa 80 MiB por decodificación en ese banco. Evidencia: [gif-copy-benchmark.json](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/gif-copy-benchmark.json); experimento: [GIFDecoder.move-experiment.cpp](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/GIFDecoder.move-experiment.cpp).
2. **Dar presupuesto a la caché de iconos.** `IconThumbs::m_cache` no tiene LRU ni límite de entradas/bytes. Cada textura RGBA de 128×128 ocupa aproximadamente 64 KiB antes de overhead: 1.000 proyectos visitados pueden conservar unos 62,5 MiB. Medir el conjunto real y fijar un presupuesto; no basta con corregir las generaciones.
3. **Abaratar y separar el acceso a frames cacheados.** En [FramePixelCache.cpp:69](F:/code/Paimbnails-main/Paimbnails-main/src/features/texture-studio/services/FramePixelCache.cpp:69), incluso un acierto vuelve a buscar linealmente los metadatos del frame. Guardarlos junto a los píxeles evita ese recorrido. El mutex también cubre `stat`, lectura, parseo y decodificación cuando cambia el atlas; separar el trabajo costoso de la publicación reduciría serialización entre workers. Falta medir la contención antes de cambiar el diseño.
4. **Probar las fronteras que faltan.** Las regresiones existentes cubren muchas correcciones anteriores, pero no estos contratos de propiedad, identidad del sistema de archivos y revisión de solicitudes. Convertir los bancos de evidencia en regresiones del comportamiento deseado al corregirlos. Los dobles de `LazySprite` deben conservar los estados reales que causan los hallazgos 5 y 7.
5. **Activar gradualmente las advertencias propias.** Ya existe `PAIMBNAILS_EXTRA_WARNINGS`; está desactivada en el build revisado. Activarla en mantenimiento/CI y filtrar dependencias permite revisar señales útiles sin tratar cada aviso del SDK como fallo del mod. Dividir las clases grandes cuando se toque una frontera concreta de persistencia, render o red; no reescribir todo el proyecto solo por su tamaño.

## Contraste con Geode y documentación oficial

El manifiesto y el SDK local apuntan a Geode 5.11.2 / Geometry Dash 2.2081. La [release oficial 5.11.2](https://github.com/geode-sdk/geode/releases/tag/v5.11.2) sigue orientada principalmente a 2.2081; que incluya arreglos de compatibilidad con 2.209 no valida por sí solo direcciones/layouts de un mod para otra versión. La separación local del proyecto 2.2091 se respetó.

La documentación oficial de [async](https://github.com/geode-sdk/docs/blob/main/tutorials/async.md) exige respetar el hilo principal para operar con Cocos/GD; la de [Ref/WeakRef](https://docs.geode-sdk.org/tutorials/utils/) explica su papel de propiedad. De ahí que las propuestas anteriores conserven los contratos de Cocos y distingan propiedad temporal de ciclos permanentes. Las [prioridades de hooks](https://docs.geode-sdk.org/tutorials/hookpriority/) deben expresar relaciones entre mods; sus nombres y objetivos locales se auditaron, pero la coexistencia efectiva con otros mods requiere pruebas en el juego.

Algunas páginas de la documentación y URLs raw devolvieron 403/cache miss durante la consulta. Para los detalles de `LazySprite` se contrastó directamente el código fuente instalado del SDK 5.11.2, además de los documentos oficiales accesibles. No se supone que el código de `main` o de una versión futura conserve exactamente esos contratos.

## Artefactos y orden de corrección

Datos resumidos: [summary.json](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/summary.json). Inventario: [inventory.json](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/inventory.json). Análisis estático: [tidy-results.json](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/tidy-results.json). Suite portable: [portable.json](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/portable.json). Fixtures existentes: [existing-fixtures-results.json](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/existing-fixtures-results.json). Reproducciones nuevas: [probes-run.log](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/probes-run.log), [extra-probes.log](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/extra-probes.log) y [gallery_reuse.log](F:/code/Paimbnails-main/Paimbnails-main/build-clang/review-2026-10-20/gallery_reuse.log).

Comandos usados para la validación principal, con el Python local del build porque el alias `python.exe` del PATH no era ejecutable en esta sesión:

```powershell
& './build-clang/tools/python/python.exe' tests/run_regressions.py --suite portable --build-dir build-clang --cxx 'C:/Program Files/LLVM/bin/clang++.exe' --sanitize
& './build-clang/tools/python/python.exe' tests/run_regressions.py --suite bindings --build-dir build-clang --sdk "$env:GEODE_SDK"
```

Orden recomendado: **confinamiento de rutas e identidad de proyectos → preservación de ediciones durante compilación → resolución de dependencias y auditoría de CI → ciclos de referencias → generaciones de miniaturas y galería → optimizaciones medidas**. Tras corregir, verificar importación/guardado/borrado con copias ficticias y recorrer las pantallas dentro de Geometry Dash; después validar Android y compatibilidad con otros mods.
