# Auditoría de bindings y hooks (2026-11-08)

## Alcance

- Geometry Dash 2.2091, Geode 6.0.0-alpha.1.
- Bindings fijados por `CMakeLists.txt` (`PAIMBNAILS_BINDINGS_REVISION`).
- Revisión estática: `.bro`, headers generados en
  `build-clang/bindings/bindings/Geode/modify` y los 236 bloques `$modify`
  de `src/`.

## Cómo funciona el enlace de Geode

La auditoría anterior (`audit_extract.md`) daba por muertos varios hooks porque
la función no tenía dirección `win`/`android` en el `.bro`. Eso es incorrecto:

- Una función **virtual** se engancha con
  `GEODE_APPLY_MODIFY_FOR_FUNCTION(addresser::getVirtual(...))`; Geode usa el
  índice de vtable y no necesita dirección.
- Una clase marcada `[[link(win, android)]]` expone sus símbolos enlazados; una
  función no virtual se engancha con `addresser::getNonVirtual(...)` aunque no
  tenga dirección en el `.bro`.

Evidencia en los headers generados:

| Método | Macro generada | Clase |
| --- | --- | --- |
| `CCEGLView::swapBuffers` | `getVirtual` | `CCEGLView` |
| `CCEGLViewProtocol::handleTouches*` | `getVirtual` | `CCEGLViewProtocol` |
| `CCMouseDispatcher::dispatchScrollMSG` | `getNonVirtual` | `[[link(win, android)]]` |
| `CCTextureCache::addImage` | `getNonVirtual` | `[[link(win, android)]]` |
| `CCKeyboardDispatcher::dispatchKeyboardMSG` | `getNonVirtual` | `[[link(win, android, ios)]]` |
| `CCKeypadDispatcher::dispatchKeypadMSG` | `getNonVirtual` | `[[link(win, android, ios)]]` |

Por eso no se tocaron los hallazgos C00–C02, C11, C12, C18–C21 de la auditoría
vieja: sus hooks están vivos en Windows/Android.

## Test nuevo

`tests/hook_bindings_regression.py` audita todos los `$modify` contra los
bindings reales del build:

- Cada método declarado en la clase base debe tener
  `GEODE_APPLY_MODIFY_FOR_FUNCTION` en el header `<Clase>.hpp`.
- Un `$override` debe existir en la jerarquía (nombre y aridad/tipos
  normalizados).
- Dos métodos del mismo bloque no pueden compartir la misma dirección Windows.
- Se detectan llamadas cualificadas a funciones no virtuales con dirección
  placeholder `0x9999999` (stubs que lanzan en runtime).

Resultado actual:

```
Bindings: 2.2091 | classes: 1109 | modify blocks: 236
Hooks checked: 442 | generated: 440 | known inherited: 7
known platform-only: 2 | warnings: 0
Platform limited (benign): 35
```

Se integra en la suite `bindings`:

```powershell
python tests/run_regressions.py --suite bindings --build-dir build-clang --sdk "$env:GEODE_SDK"
```

`--report` imprime el inventario sin fallar; `--write` regenera
`docs/HOOK_BINDINGS_AUDIT.md`.

## Deuda conocida: overrides heredados

Geode no genera un hook para un método que la clase base no declara. Por
ejemplo, `MenuLayer` no declara `onExit`; pertenece a `CCLayer`. Un
`$modify(PaimonMenuLayer, MenuLayer)` que define `onExit` compila, pero Geode
no lo engancha: el header `MenuLayer.hpp` no contiene esa macro.

El test mantiene la lista en `KNOWN_INHERITED` y **falla si aparece un caso
nuevo**. Quedan 7 casos, todos fuera de `NodeEvent`:

- Los `ccTouch*` de `LevelInfoLayer` (4), heredados de `CCLayer`.
- `SliderTouchLogic::ccTouchCancelled`, heredado de `CCMenu`.
- `enterPressed` de `LevelSearchLayer` y `ShareCommentLayer`, de
  `TextInputDelegate`.

Todos los `onEnter`, `onExit`, `onEnterTransitionDidFinish` y `cleanup`
heredados ya se migraron a `NodeEvent`:

- Las 9 capas de `BeatShaderHooks`.
- `MenuLayer`, `ProfilePage`, `LevelCell`, `ShareCommentLayer`,
  `CommentCell`, `CharacterColorPage`, `FLAlertLayer`, `GJBaseGameLayer`,
  `GJScoreCell`, `InfoLayer`, `LeaderboardsLayer`, `LevelBrowserLayer`,
  `LevelInfoLayer`, `LevelLeaderboard`, `LevelPage`, `LevelSearchLayer` y
  `Slider`.
- `CommentOptionsPopup::show` → `addOnEnterTransitionDidFinishCallback`.

Ya se migraron a selectores propios:

- `LevelCell::update` → `PaimonLevelCell::tickVideo` a 60 Hz (reproduce video).
- `ShareCommentLayer::update` → `PaimonShareComment::tickToolButtons` a 10 Hz
  (visibilidad de copiar/pegar y lectura de portapapeles cada 2 s).
- `MenuLayer::update` → `PaimonMenuLayer::tickMenu` a 60 Hz (colores
  adaptativos y badge de actualización).


Migración recomendada, siguiendo la documentación oficial:

1. Usar `CCNode::addEventListener` / `NodeEvent`
   (`Geode/ui/NodeEvent.hpp`) en un método que sí esté hookeado, normalmente
   `init`:
   - `addOnEnterCallback`
   - `addOnEnterTransitionDidFinishCallback`
   - `addOnExitCallback`
   - `addOnExitTransitionDidStartCallback`
   - `addCleanupCallback`
   El listener pertenece al nodo y se desregistra solo al destruirse. No hace
   falta un `$modify(CCLayer)` propio.
2. Para limpieza, preferir `Fields::~Fields` antes que un `onExit` heredado;
   la documentación de Geode lo marca como el método fiable.
3. Para `CommentOptionsPopup::show`, registrar
   `addOnEnterTransitionDidFinishCallback` en `init` (o enganchar
   `SetupTriggerPopup::show` y comprobar el tipo).
4. Los `ccTouch*` heredados de `LevelInfoLayer` no tienen `NodeEvent`; hay que
   enganchar `CCLayer::ccTouch*` en la clase base y filtrar con
   `typeinfo_cast<LevelInfoLayer*>(this)`, o usar un listener de toques.
5. `enterPressed` es de `TextInputDelegate`, no ciclo de vida de nodo:
   registrarlo como delegate o enganchar la interfaz corresponde a otra
   pasada.

Documentación consultada:

- [Hooking / Modifying classes](https://docs.geode-sdk.org/tutorials/modify/):
  `$modify` solo engancha funciones disponibles en la clase modificada; la
  firma debe coincidir exactamente o el hook no se crea.
- [Chapter 1.6: Modifying Layers](https://docs.geode-sdk.org/handbook/vol1/chap1_6/):
  para modificar una capa, hookear `init` (una vez por instancia).
- [Events](https://docs.geode-sdk.org/tutorials/events/): para nodos
  existentes, usar `CCNode::addEventListener`; el listener se destruye con el
  nodo.
- [NodeEvent](https://docs.geode-sdk.org/classes/geode/NodeEvent/):
  `OnEnter`, `OnExit`, `OnEnterTransitionDidFinish`,
  `OnExitTransitionDidStart`, `OnCleanup`.

`SlideInLayer::showLayer/hideLayer` es el otro caso especial: solo existe en
macOS en 2.2091. Está documentado en `KNOWN_MISSING` y en
`docs/DYNAMIC_TRANSITION_BINDINGS.md`.

## Correcciones aplicadas en esta pasada

### Ciclo de retención del popup

`src/hooks/DynamicPopupHook.cpp` usaba `Ref<FLAlertLayer> m_exitGuard = this`
para mantenerse vivo durante la animación de salida. Si la escena retiraba el
popup antes de que terminara la acción, el `Ref` nunca se liberaba. Ahora la
espera es `paimon::scheduleMainThreadDelay(...)` con un `Ref` capturado en el
lambda; el popup se libera al terminar el retardo aunque la acción se haya
cancelado.

### Beat Shaders: 9 capas

`BeatShaderManager::findLayerByKey` solo resolvía `menu`, `creator`,
`levelinfo` y `garage`. `rebuildBackgrounds` no refrescaba Level Select,
Browser, Search, Leaderboards ni Profile. Se añadieron las cinco capas y un
guard estático en `tests/review_guards_regression.py` que exige que
`availableLayers()` y `findLayerByKey()` tengan el mismo conjunto de claves.

### Menu Loop: allocs por frame

`MenuLoopFMODHook::update` llamaba `getActiveMusic(0)` y
`getCurrentSong()` en cada frame; ambos devuelven `std::string` por valor. Se
cachean y se refrescan cada 0.25 s. La posición y el shuffle siguen por frame.

### Level Entry Effects: O(n²)

`animateObjects` hacía `m_objects->containsObject(...)` por cada objeto activo.
Se construye un `unordered_set` una vez cuando hay más de 256 activos y se usa
`m_activeObjects` directamente (`std::vector<GameObject*>`), sin
`reinterpret_cast`/`typeinfo_cast`.

## Rendimiento e IO

- `ProfilePage`: la lectura de la caché de imagen de perfil mayor de 8 MB ya no
  se hace en main. Se prefetchea en `ThreadTracker`, se guarda en una caché
  temporal por cuenta y se muestra al volver a main. La decodificación de
  textura sigue en main.
- `PauseLayer`: la imagen estática de fondo se lee en worker y se decodifica en
  main con `loadWithSTBFromMemory`.
- `HoverRuntime`: el DFS de escena baja a 5 Hz cuando el ratón y la escena no
  cambian; vuelve a 30 Hz al moverse o cambiar de escena.
- `QuickHubButtonCapture`: cachea el `CCMenuItem` resuelto por `def.id` con
  `WeakRef`, valida interactuabilidad y owner, y limpia al cambiar de escena.
- `ShareCommentLayer`: la visibilidad de copiar/pegar y la lectura del
  portapapeles pasan a un ticker propio de 0.1 s (lectura cada 2 s) en vez de
  depender de `update` heredado.
- `DeathEffectManager`: el escaneo recursivo y las copias de `importFolder`
  corren en worker; el registro FMOD se hace en main en lotes de 8.

## Verificación

```powershell
python tests/hook_bindings_regression.py --build-dir build-clang
python tests/review_guards_regression.py
python tests/run_regressions.py --suite bindings --build-dir build-clang --sdk "$env:GEODE_SDK"
python tests/run_regressions.py --suite portable --build-dir build-clang --cxx <clang++>
```

La compilación del `.geode` solo se ejecuta cuando se solicita explícitamente;
el flujo local empaqueta e instala y hay que comparar el SHA-256 del paquete
instalado con el generado.
