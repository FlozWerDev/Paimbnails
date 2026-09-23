# Revisión de `src/` frente a Geode 5.10.1

## Alcance y comprobaciones

- Inventario inicial: 1.430 archivos C++/Objective-C++ y 338.490 líneas bajo `src/`.
- `mod.json`, `CMakeCache` y el SDK instalado apuntan a Geode 5.10.1.
- Inicialmente se encontraron 194 declaraciones `$modify` sobre 79 clases base. Todas las bases se localizaron en los bindings generados o en los headers de Cocos de Geode.
- Los 197 `#include <Geode/modify/...>` iniciales apuntan a headers generados presentes.
- Se cotejaron 140 referencias de prioridad (78 métodos distintos) con las declaraciones locales; la referencia a `FLAlertLayer::removeFromParentAndCleanup` corresponde a un método heredado de `CCNode`.
- Se revisaron patrones de ownership, hilos, prioridades, conversiones y comentarios en todo el árbol. El cotejo de nombres no demuestra que cada firma de hook coincida ni que cada ruta funcione en el juego.
- La pasada final comprobó 907 inclusiones `Geode/binding` y `Geode/modify` contra el SDK y los bindings generados: ninguna faltante. `git diff --check` no reportó errores.

## Cambios de esta pasada

- `GifToSheetPopup`: la decodificación y exportación usan `ThreadTracker`; los hilos tienen nombre y la capa oculta el indicador si el tracker rechaza el trabajo durante el cierre.
- `OfficialSlotStore`: el ID oficial se analiza con `numFromString<int>`, sin `stoi` ni `catch`; mantiene la comprobación de clave canónica para evitar duplicados como `o:01`.
- Se eliminó `FeatureRegistry`, que solo registraba 18 entradas que nadie leía. `PermissionTier` quedó junto a `PermissionPolicy`; el registro operativo sigue en `core/modules/ModuleRegistry`.
- El batch de robot/spider ahora usa hooks Geode para `CCSpriteBatchNode::visit` y `draw`, activados por una marca en el nodo. Se eliminó la escritura directa de su puntero de vtable y la subclase ficticia.
- El repintado diferido del robot/spider retiene el jugador hasta que corre el callback y comprueba la capa de juego actual antes de tocar sus sprites.
- Se quitaron comentarios de resumen y un bloque de comentario obsoleto en los archivos tocados. Se conservó la atribución de Icon Gradients que requiere `THIRD-PARTY-NOTICES.md`.
- `joinWithWarning` reemplaza el antiguo `timedJoin`: avisa si el cierre tarda más del plazo y siempre espera a que termine el hilo. Los decodificadores ya no dejan workers separados ni conservan estados terminales y fugas de codecs por ese motivo.
- `VideoPlayer`, thumbnails y el importador de GIF ya no tienen ramas de recuperación para decodificadores separados. `DecoderMF` intenta despertar `ReadSample` con `Flush` antes de esperar incluso si la bandera de decodificación ya estaba apagada.
- Las prioridades de los 23 hooks que pedían ejecutarse después del propio mod usan `VeryLate` directamente. La llamada anterior a `setHookPriorityAfterPost` con `flozwer.paimbnails2` se retiró: la API ordena hooks respecto de otros mods, no garantiza un orden entre hooks del mismo mod.
- Se eliminaron cabeceras que solo resumían clases en 23 archivos y se acortaron comentarios extensos de los archivos modificados. Se conservaron las notas que explican invariantes, compatibilidad, contratos de callbacks y atribuciones.

## Revisión adicional

- El callback diferido de `GradientProfilePage::toggleShip` retiene la página con `geode::Ref`, como los otros callbacks de esa clase.
- `AnimatedGIFSprite` sincroniza la lectura del tamaño de caché, une los workers aunque hayan parado por el cierre del runtime y evita encolar después de detenerlos. Las tareas se mueven fuera de la cola para no copiar los bytes del GIF.
- La caché GIF en disco guarda los píxeles antes de moverlos al sprite. El lector comprueba cada campo antes de usarlo, limita la memoria total y rechaza archivos truncados o con duraciones inválidas.
- La caché GIF mantiene fuera de la lista de expulsión las entradas fijadas y deja que un sprite retenga sus texturas antes de expulsar una entrada grande.
- Las dos rutas que leen GIF desde archivo comparten una lectura limitada a 64 MiB. La ruta directa no deja una entrada vacía en caché si fallan todas las cargas de texturas.
- Los lectores de miniaturas `.rgb` y vistas previas verifican dimensiones, longitud del archivo y un máximo de 16 Mi píxeles antes de reservar memoria. Sus escritores usan el mismo límite. La lectura de imágenes locales comunes se limita a 64 MiB.
- `PaimonFormat` limita la escritura y la lectura a 10 MiB de datos, acepta archivos vacíos válidos y mantiene sus detalles XOR y hash dentro del `.cpp`. `LevelColors` conserva el estado pendiente cuando falla el guardado. Se retiraron comentarios que describían el código. Los workers de carga local y de importación de transiciones tienen nombre.

## Pasada posterior

- `GlobalIconService` comprueba el límite de 4 MiB de cada archivo antes de cargarlo, usando el límite que ya imponía a los datos enviados.
- Las dos rutas de lectura de la caché de imagen de perfil rechazan archivos mayores de 64 MiB antes de reservar memoria; el escritor aplica el mismo límite. La extracción de colores limita los archivos locales a 64 MiB.
- `BlurDiskCache` limita las imágenes a 16 Mi píxeles al leer y capturar, verifica la longitud del archivo antes de reservar el búfer y solo descuenta una entrada del índice si pudo eliminarla o ya no existe.
- `EmoteCache` aplica un límite de 32 MiB a disco y descargas, rechaza dimensiones estáticas mayores de 4096 y crea `CCImage` en el hilo principal cuando stb no puede decodificar la imagen. El worker de decodificación tiene nombre.
- `InfoStore` carga como máximo 8 MiB y respeta al leer los límites de entradas que aplica al escribir. `ProgressTracker` limita su archivo a 32 MiB, ajusta los contadores de muertes a `uint32_t` y aplica el límite configurado al cargar. Ambos conservan los cambios pendientes si falla la escritura.
- `GDRobTopCache` limita las respuestas que conserva a 16 MiB y los JSON de disco a 32 MiB antes de leerlos o escribirlos.

## Verificación que requiere ejecutar el mod

- Comprobar robot y spider con gradientes activos e inactivos y junto a otros mods que alteren `CCSpriteBatchNode`.
- Comprobar cierre, pausa y búsqueda de video en Windows, Android y Apple. Esperar al worker evita el uso de memoria liberada, pero un codec o una operación de I/O que no responda puede retrasar el cierre.
- Comprobar los hooks de UI con varios mods activos; `VeryLate` no establece por sí solo un orden entre hooks propios con la misma prioridad.
- Comprobar GIF grandes, fijados y cargados dos veces, incluida la reutilización de su caché en disco.
- Comprobar miniaturas `.rgb` y vistas previas existentes en Windows, Android y Apple, además del rechazo de archivos truncados.
- Comprobar el respaldo de emotes estáticos que no decodifica stb, la caché de desenfoque y los datos de Info Suite después de reiniciar.

El resto de bloques de comentario largos incluye atribuciones y explicaciones de formatos o algoritmos en subsistemas no alterados. No se hizo una reescritura masiva de esos archivos porque cambiar contratos o borrar razones técnicas sin probarlos ampliaría el riesgo. Los archivos modificados quedaron sin bloques `//` de más de dos líneas.

## Seguimiento del 23 de septiembre

- Se sustituyeron las conversiones `atoi` de versiones, IDs de audio e iconos y capas Z por `geode::utils::numFromString`, de acuerdo con las recomendaciones de Geode. El parser de objetos de Autobuild usa la misma utilidad para enteros y flotantes.
- `SaveString` conserva su independencia de Geode para las pruebas del proyecto: analiza enteros con `std::from_chars`, exige flotantes completos y finitos, y calcula desplazamientos de IDs en `long long` antes de validar el rango.
- Info Suite valida los contadores y porcentajes cargados de JSON antes de reducirlos a `int`, evita desbordamientos al acumular estadísticas y mantiene el límite de niveles también en las rutas de actualización que antes lo omitían. La UI muestra totales y tiempos con suficiente rango.
- Los presets de búsqueda limitan la lectura a 2 MiB y 40 entradas, filtran dificultades y longitudes fuera de rango y no convierten números de JSON a `int` antes de validarlos.
- Se sustituyeron conversiones `path.string()` por `geode::utils::string::pathToString` en nombres de archivo y extensiones usados por Autobuild, Discord, Crash Reports, perfiles, Icon Maker, Texture Studio y Menu Music. Los nombres UTF-8 que vuelven a convertirse en rutas usan `assets::pathFromUtf8`.
- `TemplateStore` ya no lee una plantilla si falla la consulta de tamaño. `DeathEffectManager::uniqueDestination` usa la variante de `filesystem::exists` con `error_code`.
- `MenuMusicLibrary` verifica el tamaño de `library.json` antes de cargarlo, ajusta el contador y la duración al rango de sus tipos y registra los fallos de escritura.
- Las lecturas de perfil de gustos, caché de etiquetas y catálogo de emotes quedan limitadas a 16 MiB. La subida de GIF de mascotas usa el mismo máximo que la carga de imágenes estáticas.
- La caché de etiquetas conserva su estado pendiente si falla la escritura o el reemplazo del archivo. El catálogo de emotes comprueba el cierre del archivo y descarta marcas de tiempo inválidas sin desbordar la resta.
- El slider personalizado lee imágenes con el lector limitado existente, comprueba dimensiones antes de decodificar y valida los enums de su configuración. También limita la lectura del JSON y registra los fallos de guardado.
- `IconCompiler` analiza el índice devuelto por el empaquetador sin `std::stoul` y comprueba que esté dentro de `frames` antes de acceder a él.
- La revisión de esta tanda fue estática: `git diff --check` y búsquedas de conversiones restantes. No se ejecutaron compilaciones por `AGENTS.md`.

## Referencias de Geode

- [Hooking y firmas exactas](https://docs.geode-sdk.org/handbook/vol1/chap1_6/)
- [Prioridades y compatibilidad](https://docs.geode-sdk.org/tutorials/hookpriority/)
- [Fields en clases modificadas](https://docs.geode-sdk.org/tutorials/fields/)
- [Ref y ownership](https://docs.geode-sdk.org/classes/geode/Ref/)
- [Async y TaskHolder](https://docs.geode-sdk.org/tutorials/async/)
- [Directrices para mods](https://docs.geode-sdk.org/mods/guidelines/)

No se ejecutaron compilaciones ni comandos de build por `AGENTS.md`.
