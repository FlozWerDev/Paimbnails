# BetterEdit — referencia de ideas, funciones y cómo funcionan

Documento de análisis del mod **[HJfod/BetterEdit](https://github.com/HJfod/BetterEdit)**
(rama `v6`, versión 6.10.0), *"Makes the Geometry Dash Editor Better"*. El objetivo
es catalogar todas las ideas/features del mod y explicar el mecanismo técnico
(qué clase de GD se hookea y cómo) para poder reutilizarlas como referencia.

> BetterEdit está archivado y descontinuado (el autor se retiró del modding de GD).
> Es LGPLv3, así que el código es una fuente de referencia legítima, pero cualquier
> reuso de código debe mantener la licencia. Este documento describe *ideas* y
> *mecanismos*, no copia el código.

Fuentes:
- Repo: https://github.com/HJfod/BetterEdit (rama `v6`)
- `about.md`, `changelog.md`, `mod.json` del repo
- Código en `src/features/`, `src/utils/`, `src/features/backups/`, `src/features/ViewTab/`

---

## 1. Contexto técnico

- **Framework**: Geode SDK `5.6.1`, GD `2.2081` (win/mac/android/ios).
- **Patrón base**: casi todo se implementa con `$modify` (hooks de Geode) sobre las
  clases del editor de GD:
  - `EditorUI` — la UI del editor (botones, controles, input, scroll, touch).
  - `LevelEditorLayer` (`m_editorLayer`) — la capa del nivel; contiene `m_objectLayer`
    (el nodo que se escala/mueve para hacer zoom y pan) y `m_objects`.
  - `EditorPauseLayer` — menú de pausa del editor (guardado, salir, opciones).
  - `DrawGridLayer` — capa de dibujo de la rejilla (se aprovecha para dibujar líneas
    de indicadores de trigger y líneas de orbes).
  - `SetGroupIDLayer` — popup de edición de Group IDs.
  - `ObjectToolbox` — proporciona tamaños de rejilla (`gridNodeSizeForKey`).
  - `GameObject` — objetos del nivel (visibilidad LDM).
  - `GManager` / `LocalLevelManager` — sistema de guardado en disco.
- **Dependencias** (`mod.json`):
  - `geode.node-ids` — IDs de nodos para localizar elementos de UI por nombre.
  - `hjfod.gmd-api` — importar/exportar niveles como `.gmd` (backups y quick-save).
  - `cvolton.level-id-api` — IDs numéricos estables de niveles (`EditorIDs`).
  - `alphalaneous.editortab_api` — API para añadir pestañas al editor (View Tab).
- **Recursos**: dos spritesheets (`UISheet`, `ViewTabSheet`) y sprites sueltos.

### Utilidades internas reutilizables (`src/utils/`)

- **`Editor.hpp`** — helpers de editor: `getSelectedObjects`, `tintObject`
  (colorear un objeto como "seleccionado" sin cambiar su color real),
  `createViewOnlyEditor`/`isViewOnlyEditor` (editor de solo lectura),
  `focusEditorOnObjects` (centrar cámara sobre objetos), cálculo de posiciones de
  slots de trigger (`getTriggerSlots`, `getTriggerTargetedGroups`) para dibujar
  indicadores, y **eventos propios**:
  - `EditorExitEvent` — estandariza detectar el cierre del editor.
  - `UIShowEvent(EditorUI*)` — evento para saber cuándo se muestra/oculta la UI, de
    modo que cada feature esconda/enseñe sus propios controles. Muchas features
    escuchan esto con `.listen(...)`.
- **`EditCommandExt`** — extiende el enum `EditCommand` de GD con comandos propios
  para mover en fracciones (`QuarterLeft/Right/Up/Down`, `EighthLeft/...`,
  `UnitLeft/...`) usando valores enum libres (`0x400+`).
- **`BEMenuItemToggler.hpp`** — toggler de menú basado en callbacks `get/set` y un
  predicado `shouldEnable`. Base de todos los toggles de la View Tab.
- **`EditableBMLabelProxy`** — convierte un `CCLabelBMFont` existente en un campo de
  texto editable (usado para escribir directamente en la etiqueta de capa/Z).
- **`NextFreeOffsetInput.hpp`** — input genérico parametrizado por una "fuente" de
  IDs (`GroupIDSource`, etc.) que recolecta IDs usados y calcula el siguiente libre.
- **`HolyUB.hpp`** — `fakeEditorPauseLayer(lel)`: crea un `EditorPauseLayer` "falso"
  para poder llamar a sus métodos (`saveLevel`, `onBuildHelper`, `onAlignX`, etc.)
  desde `EditorUI` sin abrir realmente el menú de pausa. Truco clave para exponer
  acciones del menú de pausa como keybinds.
- **`Warn.hpp`**, **`PopupWithCorners.hpp`**, **`ObjectIDs.hpp`** (constantes de IDs
  de trigger), **`Server.*`/`OpenSSL.*`** (cliente de servidor para supporters).

---

## 2. Controles de ratón y zoom

### Improved Mouse Controls (`FixMouseControls.cpp`, desktop)
Hookea `EditorUI::scrollWheel`. Idea: dar control tipo software de dibujo al scroll.
- **Ctrl + rueda** = zoom. La escala se calcula exponencialmente
  `zoom = e^(ln(scale) - y*0.01)` (heredado del BE viejo) y se limita con `clamp`.
- **Zoom hacia el cursor** (`mouse-move-on-zoom`): convierte la posición del ratón a
  espacio del `m_objectLayer` antes y después de aplicar el zoom, y reposiciona la
  capa para que el punto bajo el cursor no se mueva.
- **Shift + rueda** = scroll horizontal.
- Rueda normal = scroll vertical (en Mac se usa horizontal por el trackpad).
- Se deshabilita durante playtest y ajusta `m_swipeStart` para que el rectángulo de
  selección siga al mover la vista.

### Pinch to Zoom (`PinchToZoom.cpp`, móvil)
Hookea `ccTouchBegan/Moved/Ended/Cancelled` de `EditorUI` (crédito a matcool).
- Mantiene un `set<Ref<CCTouch>>` de toques activos.
- Con 2 dedos: guarda distancia inicial y escala inicial; al mover calcula
  `zoom = initialScale / (initialDistance / distanciaActual)`, clamp y protección
  contra `nan/inf` (que rompen GD).
- Panea el nivel según cómo se mueve el punto medio entre los dos dedos.

### Zoom Level Text (`ZoomLevelText.cpp`)
Muestra el nivel de zoom actual como texto que "parpadea" al hacer zoom
(controlado por la opción `show-zoom-text`).

---

## 3. Rejilla (grid)

### Custom Grid Size (`GridScaling.cpp`, no Android-32)
Dos hooks:
- `$modify(ObjectToolbox)::gridNodeSizeForKey` — si hay un tamaño de rejilla
  personalizado guardado (`grid-size`) distinto de 30, lo devuelve para todas las
  claves; si no, delega al original.
- `$modify(GridUI, EditorUI)` — añade controles a la UI: dos botones (+/-) y un
  `TextInput` con filtro `Float`. Guarda el valor con `setSavedValue("grid-size")`.
  - `updateGridConstSize(value)` valida el rango (0.9–120), guarda, refresca la
    rejilla y opcionalmente la enseña si el tamaño no es el por defecto
    (`show-grid-on-size-change`, game variable `"0038"`).
  - `updateGridNodeSize` override: para engañar a GD y que use el tamaño en modo
    Create, cambia temporalmente `m_selectedMode = 2`, llama al original y lo
    restaura.
  - Los botones **snapean** entre tamaños predefinidos `{3.75, 7.5, 15, 30, 60, 90,
    120}` con `lower_bound`/`upper_bound` (`incrementGridSize`/`decrementGridSize`).
  - Se oculta con el evento `UIShowEvent`.
- Keybinds `Increase/Decrease Grid Size` llaman a las mismas funciones.

---

## 4. Capas y Z Order

### Type-in Z Layer / Editor Layer (`TypeInZLayer.cpp`)
Hookea `EditorUI::init`. Convierte la etiqueta de capa actual en un campo editable
(`EditableBMLabelProxy`) para escribir el número de capa directamente. Además:
- Añade botón de **bloqueo de capa** (`onLockLayer`) con sprites que reflejan el
  estado (abierto/cerrado/gris para "All") y colorea la etiqueta en naranja si está
  bloqueada.
- Añade botón **Next Free Layer** (`onNextFreeLayer`): recorre todos los objetos,
  junta `m_editorLayer`/`m_editorLayer2` usados y busca el primer entero libre.
- Fuerza prioridad de toque del input (`evilForceTouchPrio`) y esconde el lock
  vanilla; escucha `UIShowEvent` para mostrar/ocultar sus controles.

### Type in Z Order (`TypeInZLayer.cpp` relacionado / `TypeInZLayer`)
Permite escribir el Z Order; el changelog mantiene el detalle de que el Z Order está
limitado por diseño y que se pueden editar valores mixtos.

### Edit Mixed Values (`EditMixedValues.cpp`)
Hookea el popup de Group ID (`SetGroupIDLayer`) para poder **modificar valores
mixtos** (Editor Layer, Editor Layer 2, Z Order, Channel) de varios objetos a la vez
sin resetearlos. Los valores mixtos muestran el **rango** (min–max) en vez de solo
"Mixed", y permiten modificación relativa. También quita límites artificiales del
Z Order.

---

## 5. Group IDs y helpers de construcción

### Next Free Offset (`NextFreeOffset.cpp` + `NextFreeOffsetInput`)
Hookea `SetGroupIDLayer`:
- Inyecta un `NextFreeOffsetInput<GroupIDSource>` en el menú "next-free".
- `GroupIDSource` recolecta todos los IDs usados de cada objeto: grupos
  (`m_groups`), color groups, opacity groups, y en `EffectGameObject` el
  `m_centerGroupID`/`m_targetGroupID`. Rango 1–9999.
- Al pulsar Next Free (`onNextGroupID1`) asigna el siguiente ID libre **a partir de
  un offset** configurable, en lugar de siempre desde 1. Igual para color channels.

### Build Helper / Create Loop / Align X / Align Y
No se reimplementan: se disparan llamando a los métodos reales del
`EditorPauseLayer` vía `fakeEditorPauseLayer(...)` (`onBuildHelper`, `onCreateLoop`,
`onAlignX`, `onAlignY`). Expuestos como keybinds.

### Group Summary (`GroupSummaryPopup.cpp/.hpp`)
Popup que lista qué **grupos están en uso** en el nivel y cuáles libres, con info de
triggers. Se abre con un botón en la toolbar o el keybind `Open Group Summary`.

---

## 6. Escala y rotación

### Improved Scale & Rotate (`ImprovedScaleAndRotate.cpp`)
Rediseño de los controles de escala/rotación:
- Inputs numéricos para **Scale**, **Scale X**, **Scale Y**.
- **Scale Snapping** y **Rotation Snapping** con tamaño de snap configurable.
- **Input de grados de rotación** directo.
- **Lock Object Position During Rotation**.
- Teclas modificadoras: **Shift** activa snapping, **Control** activa posición
  absoluta durante la manipulación (`scale-rotate-input-modifier-keys`).
- Keybinds: Toggle Scale Control, Toggle Scale X/Y Control, Toggle Warp Control,
  Rotate 45 CW/CCW, Rotate Snap (rota para alinear con slopes adyacentes).
- Corrige el bug vanilla de escalar objetos de distintas escalas a la vez.

### Scaling helpers (`src/features/scaling/`)
Utilidades adicionales de escalado; `UIScaling` (ver §9).

---

## 7. Color y HSV

### New Color Menu (`BetterColorSelect.cpp`, `ColorSelect.cpp`)
Rediseño del menú de selección de color de objeto:
- Muestra **más canales** a la vez, con **previews** de los colores y de los colores
  especiales.
- Recuerda la última página en la que estabas.
- **Next Free Offset** para canales de color.
- Opción **Larger Color Menu** (botones más grandes, por defecto en Android).
- Copiar canales de Player Color en Copy Color.

### HSV Preview (`HSVPreview.cpp`)
Muestra una **preview del resultado HSV** al ajustar los sliders HSV, para ver el
color final sin aplicar. Se hizo más responsivo con el tiempo.

### Better Font Select (`BetterFontSelect.cpp`)
Reemplaza la pantalla de selección de fuente por una **con scroll** en vez de la
lista vanilla limitada.

### Force Hide Trigger UI (`ForceHideTriggerUI.cpp`)
Opción `hide-trigger-ui`: **oculta el fondo** de todos los triggers al usar sliders
(como en los triggers de shader), para ver el nivel detrás mientras ajustas.

---

## 8. Menú de movimiento (Move Menu)

### New Edit Menu (`MoveMenu.cpp`)
Rediseña el menú de mover objetos: en vez de una lista de botones, los agrupa
**direccionalmente** (arriba/abajo/izquierda/derecha). Requiere reentrar al editor.
- Movimientos en fracciones relativas al tamaño de rejilla actual: medio bloque (15u),
  cuarto (7.5u), octavo (3.75u), y "big" (5 bloques, 150u), usando los comandos
  `EditCommandExt`.
- Botones para filas/columnas configurables (Button Rows / Buttons Per Row) con
  límites ampliados (12 filas, 48 por fila — `ButtonRowsBypass.cpp`).

### Button Rows Bypass (`ButtonRowsBypass.cpp`)
Sube los límites hardcodeados de filas de botones y botones por fila del build bar.

---

## 9. Escalado de UI

### UI Scaling (`scaling/UIScaling.cpp`, `EditorUIScaling.cpp`, `EditorPauseUIScaling.cpp`)
- **Scale Factor** (0.6–1.0): escala global de la UI del editor. Requiere reentrar.
- **Scale Pause Menu**: escala también el menú de pausa.
- **Scale Build Tabs**: escala las pestañas sobre el área de selección de objetos.
- Las build tabs además son **responsive** al tamaño de pantalla (aportación de
  Alphalaneous). Advierte de incompatibilidad con QOLMod en el escalado.

---

## 10. View Tab (pestaña de visibilidad)

### View Tab (`ViewTab/ViewTab.cpp`)
Añade una **cuarta pestaña** al editor (vía `alphalaneous.editortab_api`) con toggles
rápidos de visibilidad. Detalles:
- `$modify(GameObjectExtra, GameObject)` override `updateVisibility`/`setVisible`:
  un objeto se oculta si es LDM (`m_isHighDetail`), estás en el editor, y la opción
  `hide-ldm` está activa. Así el toggle **Hide LDM** funciona sin tocar el nivel.
- Toggles construidos con `BEMenuItemToggler`; muchos mapean a **game variables**
  (`createViewToggleGV`) y llaman `m_editorLayer->updateOptions()` al cambiar.
- Incluye toggles como Preview Mode, LDM, **Show Hitboxes**, etc.
- Usa prioridades de hook (`onModify` con `setHookPriority`) para `selectObject`,
  `selectObjects` (VeryLate) y `toggleMode` (Early), coordinándose con el Editor
  Tab API y cambiando los sprites de las pestañas (create/edit/delete/view).
- Se integra con el keybind **View Mode** (tecla `4` por defecto).

### Dash Orb Line (`ViewTab/DashOrbLine.cpp`)
Dibuja las **líneas de los orbes de dash** en el editor (hook sobre `DrawGridLayer`),
para visualizar la trayectoria del dash orb.

### Portal Line Colors (`ViewTab/PortalLineColors.cpp`)
Colorea las **líneas de los portales activados** con el color del propio portal, para
distinguirlos de un vistazo.

---

## 11. Trigger Indicators

### Trigger Indicators (`TriggerIndicators.cpp`)
Dibuja **líneas entre triggers y sus objetos objetivo** (alternativa a "Trace In/Out"
vanilla). Hookea `DrawGridLayer` para pintar.
- `getTriggerColor(trigger)` mapea cada `m_objectID` de trigger a un color propio
  (usando las constantes de `ObjectIDs.hpp`): Move = magenta, Rotate = azul, Scale =
  cian, Pulse = amarillo, Alpha = cian, Spawn = verde, Stop varía según
  Stop/Pause/Resume, etc.
- Calcula posiciones de slots de entrada/salida del trigger (`getTriggerSlots`) y los
  grupos objetivo (`getTriggerTargetedGroups`). Las líneas a "Center Groups" se
  dibujan **punteadas** (`usesDashedLine`).
- **Clustering**: si hay varios objetos objetivo cercanos, dibuja una sola línea al
  grupo en vez de muchas.
- Opciones: colores dinámicos (`None`/`Selected Only`/`All`), grosor de línea,
  opacidad, y **puntas de flecha** (arrowheads) al final. Desactivado por defecto.

---

## 12. Guardado, autosave y backups (`src/features/backups/`)

### Quick Save (`QuickSave.cpp`)
Idea: guardar el nivel **rápido** escribiendo a un archivo `.gmd` de respaldo en vez
de reescribir todo el `CCLocalLevels` en disco (que es lento en muchos niveles).
- `$modify(EditorPauseLayer)::saveLevel`: pone `SKIP_NEXT_LLM_SAVE = true` cuando
  quick-save está activo, ejecuta el `saveLevel` original (para que GD actualice el
  estado del nivel) pero **salta** la escritura de `LocalLevelManager`. Luego exporta
  el nivel a `saveDir/quicksave/<id>.gmd` con `gmd-api`.
- `$modify(GManager)::save`: si es el `LocalLevelManager` y toca saltar, no guarda.
  Cuando sí guarda correctamente los niveles locales, **borra** los directorios
  temporales `quicksave/` y `autosave/` (ya no hacen falta).
- **Recuperación de crash** (`$modify(MenuLayer)::init`, una sola vez al arrancar):
  `restoreCrashData` lee los `.gmd` de `autosave/` y luego de `quicksave/` (los de
  quicksave son más recientes) y **restaura** el `m_levelString` en el nivel
  existente por ID, o inserta el nivel si ya no existe.

### Auto-Save (`AutoSave.cpp`)
`$modify(AutoSaveUI, EditorUI)` programa `onAutoSaveTick` cada segundo.
- Intervalo configurable (`auto-save-rate`, 1–60 min); nunca guarda durante playtest.
- Cuenta atrás de 5 s con una `Notification` ("Saving in N seconds").
- Al llegar al intervalo: para el playtest si hace falta, llama `createAutoSave`
  (que hace un `saveLevel` marcado como autosave hacia `autosave/`), crea un backup
  (`Backup::create(level, true)`) y limpia backups automáticos viejos
  (`Backup::cleanAutomated`). Muestra éxito/error en la notificación.
- Los cambios auto-guardados se **descartan si sales sin guardar**: el override de
  `onExitNoSave`/`FLAlert_Clicked` borra el `.gmd` de `autosave/` al confirmar salir.

### Sistema de Backups (`Backup.cpp/.hpp`, `BackupItem`, `BackupListPopup`, `BackupsUI`)
- `Backup::create` exporta el nivel como `.gmd` con timestamp a
  `betteredit-level-backups/<id-numérico>/backups`.
- Los backups automáticos se **auto-borran** tras crear 3 nuevos, para no llenar el
  disco.
- `BackupListPopup` lista los backups de un nivel; `BackupItem` es cada entrada
  (restaurar/borrar). `BackupsUI` añade el botón para abrir la lista.
- Restauración de backups antiguos (pre-2.206): mover de la ruta vieja de BetterSave
  a `betteredit-level-backups/<id>/backups` (ver `about.md`).

---

## 13. Copiar / pegar

### Copy & Paste to System Clipboard (`CopyToClipboard.cpp`)
Opción `copy-paste-from-clipboard`: al copiar objetos, se copian también al
**portapapeles del sistema** en su representación de texto, de modo que puedes
pegarlos en un editor de texto y luego volver a pegarlos en el editor.

### Copy Particle String (`CopyParticleString.cpp`)
Permite **copiar el string de un particle** desde el editor de partículas.

### Copy Values / Paste State / Paste Color (keybinds)
Se disparan llamando a los métodos reales de `EditorUI` (`onCopyState`,
`onPasteState`, `onPasteColor`). Paste State tiene popup para elegir qué propiedades
pegar (histórico).

---

## 14. Keybinds (`Keybinds.cpp`, desktop)

Sistema de keybinds totalmente personalizable (categoría `editor`), definido en
`mod.json` con `type: keybind` y migración desde IDs antiguos (`migrate-from`).
- `defineKeybind(key, callback)` registra un listener
  `KeybindSettingPressedEventV3` que ejecuta el callback en `down`.
- La mayoría de acciones **no se reimplementan**: llaman a métodos reales de
  `EditorUI` o, vía `fakeEditorPauseLayer`, a métodos del `EditorPauseLayer`.
- Keybinds destacados:
  - Rotación: Rotate 45 CW/CCW (`transformObjectCall(RotateCW45/CCW45)`), Rotate Snap.
  - Escala: Toggle Scale / Scale X-Y / Warp (activan el control correspondiente
    buscándolo con `querySelector`).
  - Movimiento: Half/Quarter/Eighth/Big en 4 direcciones (usan `moveObjectCall` con
    `EditCommand`/`EditCommandExt`). Defaults: `Ctrl+Alt+WASD` para medio bloque.
  - Edición: Edit Object/Group/Special, Copy Values, Paste State, Paste Color.
  - Selección: Select All / Left / Right.
  - Nivel: **Save Level** (con buffer anti-spam de 2 s, para el playtest antes de
    guardar y muestra notificación), Build Helper, Create Loop, Align X/Y,
    Toggle Link Controls (game variable `"0097"`).
  - Playtest: **Pause/Resume Playtest** (llama `onPlaytest` si ya está en playback).
  - UI: Show UI / Hide UI, View Mode, Group Summary, Grid Size +/-.
- Soporte para asignar **botones laterales del ratón** a keybinds (vía dependencia
  Custom Keybinds).

---

## 15. Otras utilidades de UI

- **Hide UI Button** (`HideUIButton.cpp`): botón para ocultar/mostrar toda la UI del
  editor; emite `UIShowEvent` para que el resto de features escondan sus controles.
- **Back To Content Button** (`BackToContentButton.cpp`): botón para volver a
  centrar la vista en el contenido del nivel.
- **LDM Object Count** (`LDMObjectCount.cpp`): en el menú de pausa, hookea
  `EditorPauseLayer::init` y añade al label de conteo de objetos el número y
  porcentaje de objetos LDM (`m_isHighDetail`).
- **Changelog Popup**: muestra el changelog al arrancar tras una actualización
  (opción `enable-changelog-popup`); accesible desde el popup de BetterEdit en el
  menú de pausa.
- **Auto update checking**: comprobación de actualizaciones al iniciar (histórico).

---

## 16. Servidor y supporters (`src/features/supporters/`, `src/server/`)

- `Server.cpp`/`Server.hpp` + `OpenSSL.*`: cliente HTTP hacia el backend de BetterEdit
  para la funcionalidad de **supporters** (enlazar cuenta, aparecer listado
  in-game). En v6.10 todas las features de supporter se hicieron **gratis para
  todos**, así que ya no hay features exclusivas.
- `supporters/Pro.hpp`: gating histórico de features "Pro" (ya libre).

---

## 17. Features scrappeadas / históricas (`src/scrapped-features/` y changelog)

Ideas que existieron en versiones previas (BE v5 sobre Geode 2.1, y el BetterEdit
Legacy DLL de 2.1) y que pueden servir de inspiración aunque no estén en v6:
- **Scripting** con el lenguaje propio **"Swipe"** (WIP, scrappeado).
- **Full Undo History** (deshacer cualquier cosa), Playtest Replay.
- **Global Clipboard**, Clear Clipboard.
- **Advanced Filter**, Favorite objects, Level presets, Remap IDs, Paste Objects
  From String, Relative Swipe.
- **Create Guidelines from BPM**, Pulse Objects & Rotate Saws en el editor.
- **Editor percentage / posición / start pos switcher**, editor volume controls.
- **Layer Tools** (nombrar/opacidad/bloqueo de capas), color picker (pick de
  cualquier color en pantalla), RGB color input.
- **Take screenshots** dentro del editor, right-click context menu (barebones).

> Varias se quitaron por requerir midhooks o hooks de `CCScheduler::update`
> (fuente potencial de lag/inestabilidad), lección útil de rendimiento.

---

## 18. Resumen de mecanismos reutilizables (chuleta)

| Idea | Clase hookeada | Truco clave |
|------|----------------|-------------|
| Zoom/scroll custom | `EditorUI::scrollWheel` | escalar/mover `m_objectLayer`, zoom exp + reposición al cursor |
| Pinch zoom móvil | `EditorUI::ccTouch*` | set de touches, distancia entre dedos, clamp + anti-nan |
| Grid size custom | `ObjectToolbox::gridNodeSizeForKey` + `EditorUI` | fingir modo Create al actualizar; snap entre tamaños |
| Editar label como input | `EditableBMLabelProxy` | reemplaza `CCLabelBMFont` por input |
| Next free ID | `SetGroupIDLayer` | recolectar IDs usados de todos los objetos + offset |
| Acciones de pausa como keybind | `HolyUB::fakeEditorPauseLayer` | crear pause layer falso y llamar sus métodos |
| Ocultar objetos LDM | `GameObject::updateVisibility/setVisible` | esconder si `m_isHighDetail` y opción activa |
| Dibujar overlays (líneas) | `DrawGridLayer` | pintar indicadores de trigger / orbes / portales |
| Quick save | `EditorPauseLayer::saveLevel` + `GManager::save` | saltar escritura de LocalLevelManager, `.gmd` de respaldo |
| Recuperación de crash | `MenuLayer::init` | leer `.gmd` de autosave/quicksave y restaurar level string |
| Mostrar/ocultar controles propios | evento `UIShowEvent` | cada feature escucha y togglea sus nodos |
| Coordinar hooks con otros mods | `onModify` + `setHookPriority` | Early/VeryLate para selectObject/toggleMode |

---

### Notas de licencia
BetterEdit es **LGPLv3**. Este documento resume ideas y mecanismos para referencia;
si se reutiliza código real, debe respetarse la licencia (no cerrar el código
derivado). Puedes crear mods de código cerrado que *dependan* de BetterEdit, pero no
versiones cerradas de BetterEdit en sí. Contenido reformulado para cumplir con las
restricciones de licencia.
