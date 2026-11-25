# Novedades del SDK y adopción en Paimbnails

Fecha: 31 de octubre de 2026. Revisión del SDK público desde `v5.11.2`
(`3909856`) hasta la nightly `57415d94ed47f6f439e2440ffde3fe20b063a063`.
El historial incluye 181 commits, incluidas fusiones. Se revisaron los cambios
de las interfaces públicas y sus implementaciones relevantes, y los usos del
mod. No es una auditoría exhaustiva de todas las funciones internas del juego.

La nightly incorpora la [fusión de la rama v6](https://github.com/geode-sdk/geode/pull/2314)
del 30 de octubre. Su archivo `VERSION` todavía declara `5.11.2`: esa cadena
por sí sola no identifica el ABI. El objetivo efectivo del SDK y del mod es
GD **2.2091**. Esto no equivale a una publicación estable de Geode 6.
La [release nightly](https://github.com/geode-sdk/geode/releases/tag/nightly)
es una versión de desarrollo.

## Adoptado y compilado

| Área | Estado en el mod |
| --- | --- |
| ABI de GD 2.2091 | Manifiesto, bindings `6f295839044600ceb772407caaa90ad4aeb02a45`, firmas de hooks y librería nightly alineados. Se regeneraron bindings y generador. |
| Contenedores del juego | Migrados los usos de `gd::string`, `gd::vector` y otros alias retirados a `std::*`. |
| Rutas de archivos | [FilesystemPath.hpp](../src/utils/FilesystemPath.hpp) adapta `std::filesystem` y el nuevo `asp::fs::Path`, con conversión UTF-8 explícita. Adaptados selectores, guardados, imágenes, ZIP y metadatos. |
| Dependencias del SDK | arc 1.6.4 y asp 1.5.3 alineados con esta revisión; tests adaptados a matjson 4.0.1 y sus dependencias. |
| APIs opcionales | Adaptadores aislados para los headers anteriores de More Icons y Globed. Compilar estos headers no demuestra compatibilidad binaria con una instalación vieja de esos mods. |
| Texto de ventanas | Los títulos heredados de `Popup` ya usan el nuevo tipo `geode::Label` y su API de límites. |
| Transiciones nativas | Añadidas ocho coberturas de ventanas nuevas. La auditoría enumera 1109 clases, 471 candidatas y 35 hooks. |
| Liquid Glass: recarga gráfica | Se liberan el fondo compartido, la textura blanca y los sprites del efecto antes de recrear el contexto. Los nodos conservados reconstruyen su sprite y shader al volver a dibujarse. |
| Liquid Glass: eventos | Se conserva el listener de `WindowResizeEvent` para pedir otra captura y permitir reintentar una asignación fallida. `TexturesUnloaded` también invalida los recursos. El hook previo a `reloadAll` permanece porque libera recursos antes de cambiar de contexto. |
| Sliders: ABI de creación | Se retiró el hook sobre el binding erróneo `Slider::init` y se usa la fábrica nativa `Slider::create`. Véase la [investigación del crash](CRASH_SLIDER_2_2091_2026-10-31.md). |

La última corrección está en [GlassNode.cpp](../src/features/liquid-glass/services/GlassNode.cpp),
[GlassBackdrop.cpp](../src/features/liquid-glass/services/GlassBackdrop.cpp) y
[GLContextReload.cpp](../src/core/GLContextReload.cpp). El reinicio es idempotente:
el hook previo y el evento pueden anunciar la misma recarga. La captura conserva
la comprobación de dimensiones por fotograma; no depende exclusivamente de que
una plataforma emita el evento de ventana.

## Qué conviene implementar después

Estas recomendaciones describen trabajo pendiente; no se sustituyeron en bloque
las interfaces ni los flujos existentes.

| Prioridad | Novedad | Aplicación concreta y condición |
| --- | --- | --- |
| Alta | Atajos integrados con los de GD | Geode registra los ajustes `keybind` de mods en la interfaz nativa mediante su `GeodeKeybindSource` interno. Los ajustes actuales ya pueden aprovechar esa integración. Comprobar captura, zoom, búsqueda y volumen desde ese menú; sincronizar las extensiones de ratón/rueda que conserva [ExtendedKeybind.cpp](../src/utils/ExtendedKeybind.cpp). No registrar una segunda acción para un mismo ajuste. |
| Alta | Touch por jerarquía visual y `CCTouchClipper` | Revisar Quick Hub, sliders y listas con ventanas superpuestas: el SDK ordena touch por raíces, jerarquía y Z, y amplía el tratamiento del clic derecho. Las prioridades numéricas antiguas ya no bastan para describir el orden. Verificar arrastre, clipping y cancelación en [PaiConfigKit.cpp](../src/ui/PaiConfigKit.cpp) y [QuickHubRadial.cpp](../src/features/quick-hub/ui/QuickHubRadial.cpp). |
| Media | `Label` ampliado | Migrar primero texto de usuarios, canciones y comentarios que necesite Unicode, fuentes de respaldo o emojis. Probar ajuste de líneas y tamaños. Las fábricas de [PaimonUI.cpp](../src/ui/PaimonUI.cpp) todavía devuelven `CCLabelBMFont`; cambiar su tipo afecta a muchos consumidores. |
| Media | Información de glifos de `Label` | `getGlyphInfos()` expone rectángulos e índices de texto. Puede sustituir la inspección de sprites por carácter en [CommentTextLayout.hpp](../src/utils/CommentTextLayout.hpp). Hay que convertir índices UTF-32 a los rangos UTF-8 del selector y tratar emojis/nodos como unidades. La selección de texto debe migrarse junto al renderizador. |
| Media | `Button` y scroll | `setSwallowTouches(false)` y `setDragCancelDistance()` ayudan a evitar activar una tarjeta cuando el usuario arrastra una lista. Aplicarlos en controles nuevos que usen `geode::Button`; los actuales `CCMenuItemSpriteExtra` no reciben estos métodos automáticamente. |
| Media | `getScrollFromTop()` / `scrollToFromTop()` | Centralizar el restablecimiento de posición al filtrar y reconstruir listas. [MenuMusicLibraryPopup.cpp](../src/features/menu-music/ui/MenuMusicLibraryPopup.cpp) calcula posiciones manualmente; migrar con pruebas de lista vacía, compacta, filtrada y virtualizada. |
| Media | `ProgressBar` renovado | Tiene estilos Level/Slider/Solid, animación y cancelación de animación. Puede reemplazar el dibujo manual de barras de tareas. Sus valores están entre **0 y 100**; conservar la escala y el formato de porcentaje actuales. |
| Media | `ProcessPopup` | Útil para exportaciones o tareas con mensaje y resultado. No proporciona por sí mismo descarga, cancelación ni rollback. [UpdateProgressPopup.cpp](../src/features/updates/ui/UpdateProgressPopup.cpp) conserva controles de cancelar/reiniciar y bytes recibidos que una sustitución directa perdería. |
| Baja | `BoxedLabel` | Reutilizable en identificadores, estados y estadísticas: fondo NineSlice, padding, ancho máximo y opacidad. Elegir primero un uso pequeño donde encaje con el estilo de Paimon. |
| Baja | Hashes incrementales | `Sha256Hasher`, `sha256()` y `sha256File()` permiten unificar hashes y comprobar paquetes grandes por bloques. No sustituyen directamente el HMAC de [HmacSha256.cpp](../src/utils/HmacSha256.cpp): deben conservarse los vectores y el protocolo existente. |
| Baja | Helpers Cocos y compilación | `CCArrayExt` es más compatible con STL; existe `addChildren`. Los includes públicos se hicieron más específicos. Aprovecharlos al tocar código; un reemplazo global no demuestra mejoras de rendimiento. |

Fuentes primarias de las APIs: [teclado nativo](https://github.com/geode-sdk/geode/blob/57415d94ed47f6f439e2440ffde3fe20b063a063/loader/src/hooks/KeybindHooks.cpp),
[touch y Z](https://github.com/geode-sdk/geode/blob/57415d94ed47f6f439e2440ffde3fe20b063a063/loader/src/hooks/ZOrderTouchPrio.cpp),
[Label](https://github.com/geode-sdk/geode/blob/57415d94ed47f6f439e2440ffde3fe20b063a063/loader/include/Geode/ui/Label.hpp),
[Button](https://github.com/geode-sdk/geode/blob/57415d94ed47f6f439e2440ffde3fe20b063a063/loader/include/Geode/ui/Button.hpp),
[ScrollLayer](https://github.com/geode-sdk/geode/blob/57415d94ed47f6f439e2440ffde3fe20b063a063/loader/include/Geode/ui/ScrollLayer.hpp),
[ProgressBar](https://github.com/geode-sdk/geode/pull/2271),
[ProcessPopup](https://github.com/geode-sdk/geode/pull/2283),
[BoxedLabel](https://github.com/geode-sdk/geode/blob/57415d94ed47f6f439e2440ffde3fe20b063a063/loader/include/Geode/ui/BoxedLabel.hpp),
[crypto](https://github.com/geode-sdk/geode/blob/57415d94ed47f6f439e2440ffde3fe20b063a063/loader/include/Geode/utils/crypto.hpp),
[redimensionado](https://github.com/geode-sdk/geode/pull/2272).

## Cambios de infraestructura que requieren atención

- Se retiró `geode::Task`; las tareas usan `arc::Future` y los propietarios
  `async::TaskHolder`. El mod ya usa TaskHolder en sus peticiones. Una migración
  adicional debe preservar cancelación, referencias débiles y entrega al hilo
  principal; una coroutine no vuelve seguro el acceso a Cocos desde un worker.
- Se retiró la API antigua de eventos. Los handles de listeners deben tener
  dueño o usar `.leak()` únicamente cuando el listener dura toda la sesión.
  `GEODE_EVENT_EXPORT` cambió de firma y admite nuevas formas de declarar
  sobrecargas. Los adaptadores de terceros permanecen en
  [framework/compat](../src/framework/compat).
- El CLI mínimo pasa a 3.9.0; el instalado es 3.9.0. Las dependencias pueden
  aportar `DependencyMain.cmake`; el SDK admite `GEODE_NO_ENTRY` para casos de
  integración especiales. Este mod necesita la entrada habitual.
- Se retiraron utilidades antiguas como `timer.hpp`; los temporizadores del mod
  no deben volver a depender de ese header. Conservar `steady_clock` para medir
  duraciones.
- La nightly mejora recursos base, reinicio y entrada en varias plataformas.
  Son cambios del loader: no requieren duplicar sus hooks en Paimbnails.
- El workflow usa `sdk: nightly`, por lo que puede descargar una revisión
  posterior a la evaluada aquí. Para una publicación reproducible hay que
  conservar el SDK y los binarios de la misma revisión y volver a validar si
  cambia la nightly. Los bindings del proyecto sí están fijados.

No se encontró un componente público de Liquid Glass en esta revisión. El
efecto del mod sigue usando sus shaders, su captura y su política de calidad;
el SDK aporta eventos y controles para mejorar su integración.

## Dependencia Node IDs actualizada

La revisión inicial comprobó las releases publicadas y el paquete instalado,
que seguía apuntando a GD 2.2081. La rama principal de Node IDs ya contiene la
[adaptación a 2.2091](https://github.com/geode-sdk/NodeIDs/commit/19bbd4680cbbe7f385a56a6dc6e22b2a3d1420e9)
del 25 de octubre. El nuevo commit del 31 de octubre,
[`4a61afc`](https://github.com/geode-sdk/NodeIDs/commit/4a61afcc91c8383b084f37d906f9e7214520ff25),
corrige la firma de inicialización de `SetupObjectOptionsPopup`: dos argumentos.
El número de versión del proyecto permanece en **1.23.3**.

Se compiló esa revisión oficial, sin modificaciones, con el mismo SDK nightly
y los mismos bindings que Paimbnails. Se empaquetó e instaló su variante Windows;
el manifiesto del paquete instalado declara GD **2.2091**. La copia anterior
se conservó fuera de la carpeta de mods. Los SHA-256 generado e instalado
coinciden:
`0d53b9849d7060ae0bd833da3770193542966b240fca3cc1c15eba8f9aeed8f7`.

Se adaptó [BadgeHooks.cpp](../src/features/badges/hooks/BadgeHooks.cpp) al nuevo
ID `comment-text-area-label`, con respaldo para `comment-text-area`. Esto
conserva la búsqueda del texto para emotes, fuentes y selección de comentarios.
Una regresión reproduce el fallo con el ID nuevo y pasa con ASan/UBSan tras
la corrección; también comprueba comentarios compactos y prioridad del overlay.
Paimbnails se recompiló, empaquetó e instaló después de este cambio.

Queda resuelto el desfase del paquete obligatorio de Node IDs. Se validaron
el arranque y la apertura de la lista de mods dentro del juego; el recorrido
completo de funciones y las integraciones opcionales siguen pendientes.

## Validación local

- Compilación Windows, empaquetado e instalación con
  `PAIMBNAILS_DEPLOY_LOCAL_MOD=ON`: correctos.
- SHA-256 del paquete generado e instalado:
  `2f955878d5dd4115ebd743660346299d96a1478430d9a983c9c32797d9f06f1a`.
- Tres auditorías de bindings: correctas. La del SDK comprueba 275 headers,
  106 bases de modificación y 144 objetivos de prioridad.
- Shader Liquid Glass en un contexto OpenGL oculto: compila y enlaza; pasan
  orientación, esquinas, transformaciones, clipping y opacidad.
- El primer pase portátil produjo 99 comprobaciones correctas y dos grupos
  fallidos por fixtures desactualizados. Se adaptaron las dependencias de JSON,
  las firmas y los stubs; el grupo Shop pasó con ASan/UBSan. La nueva ejecución
  completa del grupo de callbacks pasó sus **92 casos con ASan/UBSan**, incluida
  la nueva regresión del ciclo de vida gráfico de Liquid Glass.
- Tras actualizar Node IDs, pasó la nueva regresión de comentarios y se
  repitieron correctamente las tres auditorías de bindings.
- La regresión de la fábrica de sliders pasó con ASan/UBSan y se repitieron
  las tres auditorías. También pasó la prueba del slider y apertura de la lista
  de mods dentro de GD. ImagePlus se apartó durante esa ejecución y se restauró
  con su SHA-256 verificado; el componente temporal de prueba se retiró.
- Android y el recorrido visual completo de las funciones no se ejecutaron.

Comandos usados:

```powershell
cmake --build build-clang --target Paimbnails_PACKAGE --parallel 8
python tests/run_regressions.py --suite bindings --build-dir build-clang --sdk C:/Users/paimon/Documents/Geode
python tests/review_behavior_regression.py --build-dir build-clang
python tests/glass_shader_regression.py
```
