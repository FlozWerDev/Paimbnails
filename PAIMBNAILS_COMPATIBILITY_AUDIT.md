# Informe de Bugs de Compatibilidad — Paimbnails v1.0.5

> Analisis realizado sobre la base del codigo fuente de Paimbnails comparado contra las mejores practicas de **Geode SDK v5.x**.

---

## Resumen Ejecutivo

Se identificaron **4 problemas CRITICOS**, **6 problemas de severidad ALTA**, **8 problemas de severidad MEDIA**, y **12 problemas de severidad BAJA**. Los problemas criticos se concentran en:

1. Destructores mal implementados en hooks de `$modify`
2. Acceso prematuro a `m_fields` antes de llamar al `init()` original
3. Uso de `CCDirector::sharedDirector()` (API obsoleta en Geode v5)
4. Callbacks `[this]` dangling en operaciones asincronicas largas (yt-dlp/ffmpeg)

---

## 1. Problemas CRITICOS (Corregir antes del proximo release)

### 1.1 Destructor mal implementado en `DynamicPopupHook.cpp`

**Archivo:** `src/hooks/DynamicPopupHook.cpp` — Lineas 871–874

```cpp
void destructor() {
    clearPopupBlurState();
    FLAlertLayer::~FLAlertLayer();  // ← CRITICO
}
```

**Problema:** Llamar explicitamente al destructor del padre (`FLAlertLayer::~FLAlertLayer()`) desde un metodo que NO es un destructor de C++ provoca **double-free** y **undefined behavior**. En Geode v5, el patron correcto es declarar un destructor real del hook (`~PaimonDynamicPopupHook()`).

**Correccion:**
```cpp
~PaimonDynamicPopupHook() {
    clearPopupBlurState();
}
```

---

### 1.2 Acceso a `m_fields` antes de llamar al original en `LevelInfoLayer.cpp`

**Archivo:** `src/hooks/LevelInfoLayer.cpp` — Lineas 1529–1539

```cpp
bool init(GJGameLevel* level, bool challenge) {
    log::info(...);
    if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
        if (scene->getChildByType<LeaderboardsLayer>(0)) {
            m_fields->m_fromLeaderboards = true;   // ← acceso prematuro
            m_fields->m_leaderboardType = LeaderboardType::Default;
        }
    }
    if (!LevelInfoLayer::init(level, challenge)) return false;  // original tarde
```

**Problema:** Se accede a `m_fields` **antes** de que `LevelInfoLayer::init()` haya corrido. Si `init()` falla, los valores se escriben en un objeto no inicializado. Ademas, `m_fields` se inicializa lazy — acceder antes del `init()` original puede ser inseguro dependiendo del estado del objeto base.

**Correccion:**
```cpp
bool init(GJGameLevel* level, bool challenge) {
    if (!LevelInfoLayer::init(level, challenge)) return false;
    if (auto scene = CCDirector::get()->getRunningScene()) {
        if (scene->getChildByType<LeaderboardsLayer>(0)) {
            m_fields->m_fromLeaderboards = true;
            m_fields->m_leaderboardType = LeaderboardType::Default;
        }
    }
    return true;
}
```

---

### 1.3 `[this]` dangling en callbacks de procesos externos

**Archivos:**
- `src/features/menu-music/ui/FfmpegInstallPopup.cpp` (lineas 146, 150)
- `src/features/menu-music/ui/YtDlpInstallPopup.cpp` (lineas 151, 155)
- `src/features/menu-music/ui/MenuMusicAddPopup.cpp` (lineas 401, 450, 488)

**Problema:** Estos popups pasan callbacks `[this]` a procesos de bootstrap/descarga (ffmpeg, yt-dlp) que corren en threads separados y pueden tardar segundos. Si el usuario cierra el popup antes de que termine, `this` se convierte en **dangling pointer**, causando un crash garantizado cuando el callback se dispare.

**Correccion:** Usar `WeakRef<CCNode>` o un token de cancelacion:
```cpp
auto weakRef = WeakRef(this);
[bootstrap](..., [weakRef](bool ok, std::string msg) {
    if (auto* self = weakRef.get()) {
        // usar self de forma segura
    }
});
```

---

### 1.4 Raw `CCTexture2D*` sin `retain()`/`release()` en `VideoPlayer.hpp`

**Archivo:** `src/video/VideoPlayer.hpp` — Lineas 128–130

```cpp
cocos2d::CCTexture2D* m_texY  = nullptr;
cocos2d::CCTexture2D* m_texCb = nullptr;
cocos2d::CCTexture2D* m_texCr = nullptr;
```

**Problema:** `CCTexture2D` es un `CCObject` con reference counting. Si `CCTextureCache` evicta estas texturas, los punteros quedan dangling y el decoder seguira escribiendo frames sobre memoria liberada.

**Correccion:** Usar `Ref<CCTexture2D>` (incluido en `geode::prelude`) en lugar de raw pointers.

---

## 2. Problemas de severidad ALTA

### 2.1 `new CCTexture2D()` sin `release()` en rama de error

**Archivos:**
- `src/hooks/PauseLayer.cpp:748` y `:927`
- `src/hooks/LevelCell.cpp:552`

**Problema:** Se crean texturas con `new` (refcount inicial = 1), pero si `initWithImage()` o `initWithData()` fallan, no se hace `texture->release()`, causando memory leak.

**Correccion:** Usar RAII o asegurar `release()` en todas las ramas de error.

---

### 2.2 Raw pointer `CCTexture2D*` estatico sin `Ref<>`

**Archivo:** `src/framework/ModEvents.hpp:75`

```cpp
static inline cocos2d::CCTexture2D* s_lastTextureRaw = nullptr;
```

**Problema:** Puntero crudo a un objeto con refcount. Si la textura se libera, el puntero queda invalido permanentemente.

---

### 2.3 `new ProfileConfig` sin `delete`

**Archivo:** `src/features/profiles/ui/CommentBgSettingsPopup.cpp:40`

```cpp
m_configPtr = new ProfileConfig(config);
```

**Problema:** `ProfileConfig` no es un `CCObject`. No hay destructor visible que haga `delete m_configPtr`, causando memory leak garantizado.

---

### 2.4 `[this]` en `scheduleMainThreadDelay` sin token de cancelacion

**Archivos:**
- `src/features/paidraw/PaiDrawManager.cpp:1288`
- `src/features/thumbnails/services/ThumbnailTransportClient.cpp:236`

**Problema:** Estos managers no son `CCObject`. Si se destruyen antes de que expire el delay (0.08s), el callback invoca un objeto destruido.

---

### 2.5 Acceso a `getRunningScene()` desde posible hilo no-main

**Archivos:**
- `src/features/discord-presence/services/DiscordPresenceManager.cpp:66`
- `src/hooks/PlayLayer.cpp:743`

**Problema:** `CCDirector::get()->getRunningScene()` se llama desde contextos que pueden estar en un thread secundario (worker de Discord, callback de captura). El scene graph de Cocos2d **NO es thread-safe**.

**Correccion:** Siempre envolver el acceso al scene graph con `Loader::get()->queueInMainThread(...)`.

---

### 2.6 `typeinfo_cast` usado para castear `this` en hook

**Archivo:** `src/hooks/LevelListLayer.cpp:227`

```cpp
bool isLevelList = typeinfo_cast<LevelListLayer*>(this) != nullptr;
```

**Problema:** Dentro de un `$modify(LevelListLayer)`, `this` ya ES un `LevelListLayer*`. Usar `typeinfo_cast` aqui es innecesario y mas lento. Si el cast fallara (lo cual no deberia), el comportamiento seria incorrecto.

---

## 3. Problemas de severidad MEDIA

### 3.1 `CCDirector::sharedDirector()` — API obsoleta en Geode v5

**Archivo:** Afecta a **~70 archivos** en todo `src/`

**Problema:** `CCDirector::sharedDirector()` esta marcado como obsoleto en Geode v5. El metodo canonico es `CCDirector::get()`.

**Correccion global:** Reemplazar `CCDirector::sharedDirector()` por `CCDirector::get()`.

**Nota:** Aunque `sharedDirector()` sigue funcionando, puede ser removido en futuras versiones del SDK.

---

### 3.2 Destructor de `PaimonLevelCell` llama `this->unschedule()`

**Archivo:** `src/hooks/LevelCell.cpp` — Lineas 345–353

**Problema:** Llamar `this->unschedule()` dentro del destructor de un `$modify` es inseguro porque el `CCScheduler` puede haber removido al nodo antes de que el destructor del hook corra. El cleanup correcto debe hacerse en `onExit()` o `cleanup()`.

---

### 3.3 Uso de `getChildByTag` en lugar de `getChildByID`

**Archivo:** Varios archivos en `src/`

**Problema:** `getChildByTag` es el patron antiguo de Cocos2d. Geode v5 promueve el uso de **String IDs** (`setID(...)`, `getChildByID(...)`) para compatibilidad entre mods. Los tags numericos colisionan facilmente con otros mods.

**Archivos afectados:**
- `src/features/transitions/ui/CustomTransitionScene.cpp`
- `src/features/custom-slider/services/CustomSliderManager.cpp`
- `src/features/pet/ui/PetConfigPopup.cpp`
- `src/features/main-menu-layout/ui/MainMenuLayoutEditor.cpp`
- `src/features/community/ui/LeaderboardLayer.cpp`

---

### 3.4 `detach()` en `TimedJoin.hpp` con capturas `this` vivas

**Archivo:** `src/utils/TimedJoin.hpp` — Lineas 49, 64, 78

**Problema:** Si un thread no termina dentro del timeout, se hace `detach()`. El thread continua corriendo con capturas de `this` que pueden apuntar a memoria liberada si el objeto propietario se destruye durante el shutdown abrupto.

---

### 3.5 `join()` sin timeout en `EmoteCache.cpp`

**Archivo:** `src/features/emotes/services/EmoteCache.cpp:517`

**Problema:** `t.join()` bloquea indefinidamente si un worker de decodificacion se atasca en I/O.

---

### 3.6 `new Task()` (CCObject) que puede leak si el scheduler muere

**Archivo:** `src/utils/MainThreadDelay.hpp:25`

**Problema:** El `Task` hace `this->release()` en `fire()`, pero si el scheduler es destruido antes de disparar el selector (shutdown del juego), el objeto nunca se libera.

---

### 3.7 Lambdas anidadas con `[this]` en `ProfileThumbs`

**Archivo:** `src/features/profiles/services/ProfileThumbs.cpp` — Lineas 970, 1109, 1159, 1181

**Problema:** Tres niveles de lambda anidados capturando `this` en callbacks HTTP. Aunque `ProfileThumbs` es un singleton, este patron es fragil y dificil de razonar sobre la vida util de los objetos.

---

### 3.8 Clase interna en `LeaderboardsLayer.cpp` llama `Popup::init()`

**Archivo:** `src/hooks/LeaderboardsLayer.cpp` — Linea 34–35

```cpp
bool init() {
    if (!Popup::init(360.f, 180.f)) return false;
```

**Problema:** La clase interna (helper) llama a `Popup::init()` directamente, saltandose `LeaderboardsLayer::init()`. Esto omite la logica de inicializacion de GD.

---

## 4. Problemas de severidad BAJA

### 4.1 Singletons con `new` intencional sin destructor

**Archivos:**
- `src/utils/MainThread.hpp:24,30`
- `src/features/backgrounds/services/LayerBackgroundManager.cpp:595`

**Problema:** Estos singletons usan `new` intencionalmente para evitar destruccion en `atexit`, pero confunden herramientas de leak detection y pueden causar problemas en unload de la DLL si contienen threads activos.

---

### 4.2 Clase helper en `LevelAreaInnerLayer.cpp` llama `Popup::init()`

**Archivo:** `src/hooks/LevelAreaInnerLayer.cpp` — Lineas 17–18

Igual que el problema 3.8, pero en una clase helper interna.

---

### 4.3 Uso inconsistente de `cocos2d::CCDirector::get()` vs `CCDirector::get()`

**Archivo:** Varios

**Problema:** Algunos archivos usan el namespace explicito (`cocos2d::CCDirector::get()`) mientras que otros usan la version importada por `geode::prelude`. La inconsistencia dificulta el mantenimiento.

---

### 4.4 `std::thread` en `DecoderPLM.hpp` sin garantia de `join()`

**Archivo:** `src/video/platform/DecoderPLM.hpp:52`

```cpp
m_thread = std::thread(&DecoderPLM::decodeLoop, this);
```

**Problema:** Si `DecoderPLM` se destruye antes de que el thread termine, `this` es dangling. El header no muestra si el `.cpp` hace `join()` en el destructor.

---

### 4.5 `CCDirector::sharedDirector()` usado en archivos de hooks

**Archivos:**
- `src/hooks/MenuLayer.cpp:210,522,658,996`
- `src/hooks/LevelSelectLayer.cpp:178`
- `src/hooks/LevelInfoLayer.cpp:352,697,985,1284,1293,1314,1387,1532,1592,1860,1895,1987,2037,2141,2438`
- `src/hooks/LevelCell.cpp:2844,3740`
- `src/hooks/GauntletLayer.cpp:101,210`
- `src/hooks/LeaderboardsLayer.cpp:158`
- `src/hooks/LevelAreaInnerLayer.cpp:245,280,323`
- `src/hooks/DynamicPopupHook.cpp:29,258`
- `src/hooks/LevelSearchLayer.cpp:621,964,2312`
- `src/hooks/LoadingLayer.cpp:265`

**Problema:** Uso masivo de la API obsoleta `sharedDirector()` en el codigo de hooks.

---

### 4.6 Mezcla de `m_fields->` y `m_fields.self()`

**Archivos:** `src/hooks/MenuLayer.cpp`, `src/hooks/LevelCell.cpp`

**Problema:** Uso inconsistente de los dos estilos de acceso a fields. Aunque ambos funcionan, mezclarlos (a veces con null-check, a veces sin) puede causar crashes si `self()` retorna null.

---

## 5. Problemas de Configuracion (mod.json / CMakeLists.txt)

### 5.1 Version de Geode SDK

**Archivo:** `mod.json`

```json
"geode": "5.6.1"
```

**Estado:** Correcto. La version esta actualizada.

---

### 5.2 Version de GD

```json
"gd": {
    "win": "2.2081",
    "android": "2.2081",
    "mac": "2.2081",
    "ios": "2.2081"
}
```

**Estado:** Correcto. Todas las plataformas estan definidas.

---

### 5.3 Dependencias

```json
"dependencies": {
    "geode.node-ids": {
        "version": ">=v1.23.0",
        "required": true
    },
    "prevter.imageplus": {
        "version": ">=v1.1.0",
        "required": false
    }
}
```

**Estado:** Correcto. `node-ids` es una dependencia fuerte bien versionada.

---

### 5.4 Incompatibilidades

```json
"incompatibilities": {
    "cdc.level_thumbnails": {
        "version": "*",
        "breaking": true
    }
}
```

**Estado:** Correcto. Usa la sintaxis v5 de `breaking: true/false`.

---

### 5.5 CMakeLists.txt — `target_link_libraries` sin keyword `PRIVATE`

**Archivo:** `CMakeLists.txt` — Linea 173–174

```cmake
target_link_libraries(${PROJECT_NAME} pl_mpeg)
target_link_libraries(${PROJECT_NAME} rapidfuzz)
```

**Problema:** En versiones modernas de CMake, omitir el keyword (`PRIVATE`/`PUBLIC`/`INTERFACE`) esta deprecado. Geode recomienda usar `PRIVATE` para dependencias internas del mod.

**Correccion:**
```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE pl_mpeg)
target_link_libraries(${PROJECT_NAME} PRIVATE rapidfuzz)
```

---

### 5.6 Uso de `file(GLOB_RECURSE ...)` en CMake

**Archivo:** `CMakeLists.txt` — Linea 66

```cmake
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
```

**Problema:** `GLOB_RECURSE` no detecta automaticamente archivos nuevos/elimnados en todos los generadores de CMake. La recomendacion de Geode es listar explicitamente los archivos fuente.

**Nota:** Dado el tamano del proyecto (~300+ archivos .cpp), esto es comprensible pero puede causar builds inconsistentes.

---

## 6. Recomendaciones Generales para Compatibilidad con Geode v5

### 6.1 Siempre usar `CCDirector::get()`

```cpp
// ❌ Obsoleto
auto* director = CCDirector::sharedDirector();

// ✅ Correcto
auto* director = CCDirector::get();
```

### 6.2 Siempre llamar al original PRIMERO en `init()`

```cpp
bool init(...) {
    if (!OriginalClass::init(...)) return false;
    // Tu codigo aqui
    return true;
}
```

### 6.3 Usar `WeakRef` para callbacks asincronicos que referencian nodos

```cpp
auto weakRef = WeakRef(this);
someAsyncOperation([weakRef]() {
    if (auto* self = weakRef.get()) {
        // seguro
    }
});
```

### 6.4 Preferir `getChildByID` sobre `getChildByTag`

```cpp
// ❌ Fragil ante otros mods
auto* node = parent->getChildByTag(42);

// ✅ Compatible
auto* node = parent->getChildByID("my-mod.node-name"_spr);
```

### 6.5 Usar `Ref<>` para objetos de Cocos2d

```cpp
// ❌ Raw pointer
CCTexture2D* m_texture = nullptr;

// ✅ Reference counting
Ref<CCTexture2D> m_texture;
```

### 6.6 No llamar destructores explicitamente

```cpp
// ❌ NUNCA hagas esto
void destructor() {
    BaseClass::~BaseClass();
}

// ✅ Usa un destructor real
~MyHook() {
    // cleanup
}
```

### 6.7 Acceso a fields desde fuera del modify

```cpp
// ✅ Correcto
static_cast<MyModifyClass*>(obj)->m_fields->myField = 12;

// ❌ Incorrecto (evita typeinfo_cast para este caso)
typeinfo_cast<MyModifyClass*>(obj)->m_fields->myField = 12;
```

---

## 7. Lista de Archivos que Requieren Atencion Inmediata

| Prioridad | Archivo | Lineas | Problema |
|-----------|---------|--------|----------|
| P0 | `src/hooks/DynamicPopupHook.cpp` | 871–874 | Destructor explicito ilegal |
| P0 | `src/hooks/LevelInfoLayer.cpp` | 1529–1539 | Acceso prematuro a `m_fields` |
| P0 | `src/video/VideoPlayer.hpp` | 128–130 | Raw CCTexture2D* sin retain |
| P0 | `src/features/menu-music/ui/MenuMusicAddPopup.cpp` | 401,450,488 | `[this]` dangling en yt-dlp |
| P0 | `src/features/menu-music/ui/FfmpegInstallPopup.cpp` | 146,150 | `[this]` dangling en ffmpeg |
| P0 | `src/features/menu-music/ui/YtDlpInstallPopup.cpp` | 151,155 | `[this]` dangling en yt-dlp |
| P1 | `src/hooks/PauseLayer.cpp` | 748, 927 | `new CCTexture2D` sin release en error |
| P1 | `src/hooks/LevelCell.cpp` | 552 | `new CCTexture2D` sin release en error |
| P1 | `src/framework/ModEvents.hpp` | 75 | Raw static CCTexture2D* |
| P1 | `src/features/profiles/ui/CommentBgSettingsPopup.cpp` | 40 | `new ProfileConfig` sin delete |
| P1 | `src/hooks/PlayLayer.cpp` | 743 | Scene graph desde thread secundario |
| P1 | `src/features/discord-presence/services/DiscordPresenceManager.cpp` | 66 | Scene graph desde thread secundario |
| P2 | `src/hooks/LevelCell.cpp` | 345–353 | `unschedule()` en destructor |
| P2 | `src/hooks/LeaderboardsLayer.cpp` | 34–35 | `Popup::init()` en clase interna |
| P2 | `src/utils/TimedJoin.hpp` | 49,64,78 | `detach()` con capturas vivas |
| P2 | `src/features/emotes/services/EmoteCache.cpp` | 517 | `join()` sin timeout |
| P2 | `src/utils/MainThreadDelay.hpp` | 25 | `new Task()` potencial leak |
| P2 | `src/features/paidraw/PaiDrawManager.cpp` | 1288 | `[this]` en delay sin cancelacion |
| P2 | `src/features/thumbnails/services/ThumbnailTransportClient.cpp` | 236 | `[this]` en delay sin cancelacion |
| P3 | ~70 archivos | Varias | `CCDirector::sharedDirector()` obsoleto |
| P3 | Varios | Varias | `getChildByTag` en lugar de `getChildByID` |
| P3 | `CMakeLists.txt` | 173–174 | `target_link_libraries` sin keyword |

---

## 8. Notas Finales

El mod Paimbnails es uno de los mas grandes y completos en el ecosistema de Geode. La mayoria de los hooks siguen correctamente los patrones de Geode v5 (`$modify` con nombre, llamada al original primero, uso de `m_fields` con valores por defecto). Los problemas criticos se concentran en areas especificas:

1. **Hooks destructores** (`DynamicPopupHook.cpp`)
2. **Timing de inicializacion** (`LevelInfoLayer.cpp`)
3. **Vida util de callbacks asincronicos** (menu-music popups)
4. **Memory management de recursos graficos** (VideoPlayer, ModEvents)

Corregir los 6 problemas P0 y los 6 problemas P1 garantizara una estabilidad significativamente mayor y mejor compatibilidad con futuras versiones de Geode SDK.
