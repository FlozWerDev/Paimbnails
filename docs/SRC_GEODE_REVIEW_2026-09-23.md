# Revisión de `src/` — 23 de septiembre de 2026

Esta pasada continúa [la revisión anterior](SRC_GEODE_REVIEW_2026-09-22.md). Se conservaron los cambios que ya estaban en el árbol de trabajo.

## Alcance

- Inventario actual: 1.429 archivos C++/Objective-C++ y 338.849 líneas bajo `src/`.
- Se contrastaron las inclusiones y las bases de `$modify` con los headers del SDK local de Geode 5.10.1. Las 907 inclusiones `Geode/binding` o `Geode/modify` y las 195 declaraciones `$modify` (79 bases distintas) tienen un header correspondiente.
- Se revisaron patrones de IDs de nodos, prioridades de hooks, callbacks, lectura de archivos, límites de imágenes, comentarios y zonas de código denso. La presencia de un header no prueba que todas las firmas o rutas de ejecución sean correctas.

## Cambios aplicados

- Los IDs literales y varios IDs dinámicos de nodos propios usan `_spr`, junto con sus búsquedas internas. Se mantuvieron `left-menu`, `socials-menu` y `view-button` porque sustituyen IDs originales que otros componentes buscan. Se actualizaron también IDs pasados a helpers en iconos, modo dual, música y progresión.
- Los cuatro hooks del modo dual que usaban `Priority::Replace` usan `Priority::Last` con prioridad `Pre`, de acuerdo con la guía de Geode para rutas que sustituyen la implementación original.
- `ImageBuffer` limita los datos codificados a 128 MiB y la imagen decodificada a 16 Mi píxeles. Comprueba dimensiones antes de decodificar, evita desbordamientos en recortes, devuelve un búfer vacío si se excede el límite y copia píxeles rotados sin aliasing de tipos. Los consumidores que escriben directamente en el búfer comprueban el resultado vacío.
- `BlurDiskCache::lookupAsync` ya no invoca el callback mientras mantiene `m_mutex` bloqueado.
- Los crash reports quedan desactivados por defecto en `mod.json` y `ModuleCatalog`. `CrashReporter` lee directamente solo el tramo del archivo que enviará, en lugar de cargar el log completo para recortarlo después. Una elección ya guardada por el usuario sigue teniendo prioridad.
- `custom-hover` deja de usar macros para serializar una configuración pequeña; su runtime y popup tienen ramas y callbacks legibles. La vista previa evita escalas no positivas.
- `LocalThumbnailViewPopup` comparte el callback de las tres descargas de verificación, elimina mensajes de diagnóstico repetitivos y comprueba la textura antes de usarla.
- `ProfileThumbs` aplica el mismo límite LRU a imágenes, GIF y configuraciones; libera las referencias de GIF al sustituir, caducar, borrar o desalojar entradas. Los helpers que requieren el mutex de la caché ya no son públicos. Se retiró un pool de hilos sin usuarios y su paso de apagado vacío.
- El editor de texturas comprueba el tamaño de PNG, GIF, plist, máscaras y archivos de importación antes de cargarlos completos. `PlistParser` limita también las entradas ya presentes en memoria y elimina `sniffFormat`, que no tenía llamadas.
- `EmoteCache` acepta para la caché de disco solo un nombre de archivo sin directorios ni raíces, también al borrar entradas inválidas. Comprueba las dimensiones de imágenes estáticas antes de decodificarlas.
- Los callbacks diferidos de insignias, emotes, imágenes de perfil y tarjetas de clasificación retienen la textura hasta usarla. El grid de insignias conserva solo una referencia débil al botón mientras espera su emote. El selector de color comprueba que el picker y sus controles existan antes de acceder a ellos.
- Se quitaron comentarios de resumen en las zonas tocadas; permanecen las notas breves sobre contratos, límites y decisiones de compatibilidad.

## Verificación y límites

- `git diff --check` y la validación JSON de `mod.json` pasan.
- Un control léxico simple de los archivos C++ modificados no encontró delimitadores sin pareja. Se probaron 100.000 combinaciones aleatorias del cálculo de recorte usado por `ImageBuffer::blitOverwrite` frente a un modelo directo.
- No se ejecutaron compilaciones ni comandos de build por `AGENTS.md`. Tampoco se ejecutó el mod, de modo que faltan pruebas de hooks e interacción con otros mods en Windows, Android, macOS e iOS.
- La corrección de la caché de perfiles y los límites de importación se revisaron estáticamente; siguen pendientes de prueba funcional en el juego por la restricción de no compilar.
- La vida útil de las texturas en callbacks y el rechazo de nombres de caché con rutas se comprobaron por inspección del flujo de llamadas; también requieren prueba funcional cuando se permita compilar.
- Los archivos más extensos siguen mereciendo una revisión funcional por subsistema: `GifPaintVectorizer.cpp`, `LevelCell.cpp`, `HttpClient.cpp`, `LocalThumbnailViewPopup.cpp` y `CollabManager.cpp` superan las 2.800 líneas. La longitud por sí sola no justifica dividirlos; conviene separar responsabilidades al modificar cada flujo y probarlo en el juego.

## Referencias

- [Guía de publicación y compatibilidad de Geode](https://docs.geode-sdk.org/mods/guidelines/)
- [Modificación de capas y hooks](https://docs.geode-sdk.org/handbook/vol1/chap1_6/)
- [Prioridad de hooks](https://docs.geode-sdk.org/tutorials/hookpriority/)
- [Campos de instancias modificadas](https://docs.geode-sdk.org/tutorials/fields/)
