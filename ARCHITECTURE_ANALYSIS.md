# Analisis de Arquitectura — Paimbnails v1.0.5

> Documento de arquitectura basado en analisis profundo del codigo fuente.

---

## 1. Resumen Ejecutivo

Paimbnails es un mod de **Geode SDK v5** para Geometry Dash que transforma la experiencia visual del juego. Es uno de los mods mas grandes del ecosistema con **~500 archivos fuente** organizados en una arquitectura modular bien definida.

**Metricas clave:**
- **~3900 lineas** solo en `LevelCell.cpp` (el hook mas complejo)
- **~1066 lineas** en `MenuLayer.cpp`
- **324 archivos** en `src/features/`
- **38 hooks** de `$modify` en `src/hooks/`
- **Sistema de shaders GLSL** personalizado
- **Decodificacion de video** via pl_mpeg (MP1L/MP2L)

---

## 2. Estructura de Directorios

```
src/
├── main.cpp                    # Entry point: registra settings y arranca bootstrap
├── core/                       # Ciclo de vida y inicializacion
│   ├── Bootstrap.cpp           # Inicializa managers, carga assets, verifica dependencias
│   ├── RuntimeLifecycle.cpp    # Maneja onEnter/onExit de escenas para cleanup
│   └── Settings.hpp            # Definicion de settings del mod
├── framework/                  # Infraestructura compartida
│   ├── FrameworkInit.cpp       # Inicializacion de compatibilidad entre mods
│   ├── ModEvents.hpp           # Event bus para comunicacion entre componentes
│   └── compat/                 # Adaptadores para otros mods (node-ids, etc.)
├── hooks/                      # Hooks de Geode ($modify)
│   ├── MenuLayer.cpp           # Fondos dinamicos, profile pic, Paimon guide
│   ├── LevelCell.cpp           # Thumbnails, galeria, hover effects, compact mode
│   ├── LevelInfoLayer.cpp      # Leaderboard integration, blur
│   ├── PauseLayer.cpp          # Captura de pantalla, blur
│   ├── PlayLayer.cpp           # Discord presence, scene tracking
│   ├── LevelSearchLayer.cpp    # Busqueda en tiempo real
│   ├── DynamicPopupHook.cpp    # Blur dinamico para popups
│   └── ... (38 hooks total)
├── features/                   # Modulos de funcionalidad (324 archivos)
│   ├── thumbnails/             # Sistema de thumbnails
│   │   ├── services/
│   │   │   ├── ThumbnailLoader.hpp     # Orquesta descargas con prioridad
│   │   │   ├── ThumbnailCache.hpp      # Cache LRU RAM + disco
│   │   │   ├── LocalThumbs.hpp         # Thumbnails locales del usuario
│   │   │   └── LevelColors.hpp         # Extraccion de colores dominantes
│   │   └── ui/
│   ├── backgrounds/            # Fondos dinamicos por layer
│   │   └── services/
│   │       └── LayerBackgroundManager.hpp
│   ├── menu-music/             # Musica de menu via yt-dlp + ffmpeg
│   │   ├── services/
│   │   │   ├── MenuMusicPlayer.hpp
│   │   │   ├── YtDlpBootstrap.hpp
│   │   │   └── FfmpegBootstrap.hpp
│   │   └── ui/
│   ├── profiles/               # Customizacion de perfiles
│   ├── transitions/            # Transiciones animadas entre escenas
│   ├── emotes/                 # Sistema de emotes
│   ├── quick-hub/              # Hub central del mod
│   ├── guide/                  # Guia interactiva (Paimon)
│   ├── paidraw/                # Sistema de dibujo
│   ├── pet/                    # Mascota virtual
│   ├── cursor/                 # Cursor personalizado
│   ├── discord-presence/       # Rich presence para Discord
│   └── ... (mas features)
├── video/                      # Sistema de video
│   ├── VideoPlayer.hpp         # Player basado en pl_mpeg
│   ├── VideoThumbnailSprite.hpp# Sprite que renderiza video como thumbnail
│   └── platform/
│       └── DecoderPLM.hpp      # Decodificador MPEG1/2 via pl_mpeg
├── blur/                       # Sistema de blur
│   ├── BlurSystem.hpp          # Gaussian blur async
│   └── PopupBlurService.hpp    # Blur para popups
├── utils/                      # Utilidades compartidas
│   ├── HttpClient.hpp          # Cliente HTTP con cache y retry
│   ├── ThreadPool.hpp          # Pool de threads para trabajo async
│   ├── MainThreadDelay.hpp     # Scheduler para callbacks en main thread
│   ├── AnimatedGIFSprite.hpp   # Sprite con soporte GIF animado
│   ├── Shaders.hpp             # Sistema de shaders GLSL
│   └── ...
├── managers/                   # Managers globales (singletons)
│   └── ThumbnailAPI.hpp        # API REST para thumbnails
├── layers/                     # Custom layers
├── ui/                         # Componentes UI reutilizables
└── blur/                       # Sistema de blur (compartido)
```

---

## 3. Patrones de Arquitectura

### 3.1 Patron Singleton

Casi todos los managers usan el patron singleton con instancia estatica:

```cpp
class ThumbnailLoader {
    static ThumbnailLoader& get() {
        static ThumbnailLoader instance;
        return instance;
    }
};
```

**Managers principales:**
- `ThumbnailLoader` — Orquesta descargas con prioridad
- `ThumbnailCache` — Cache LRU en RAM
- `LocalThumbs` — Thumbnails locales del usuario
- `LayerBackgroundManager` — Fondos dinamicos por layer
- `BlurSystem` — Gaussian blur async
- `MenuMusicPlayer` — Reproductor de musica
- `ProfileThumbs` — Thumbnails de perfiles
- `LevelColors` — Colores dominantes por nivel
- `PaiDrawManager` — Sistema de dibujo
- `PetManager` — Mascota virtual
- `CursorManager` — Cursor personalizado
- `QuickHubManager` — Hub central
- `TransitionManager` — Transiciones entre escenas

### 3.2 Patron Hook ($modify)

Geode v5 usa el macro `$modify(Nombre, ClaseBase)` para interceptar metodos:

```cpp
class $modify(PaimonLevelCell, LevelCell) {
    struct Fields {
        Ref<CCClippingNode> m_clippingNode = nullptr;
        Ref<CCSprite> m_thumbSprite = nullptr;
        // ... 50+ campos
    };
    
    $override void loadFromLevel(GJGameLevel* level) {
        LevelCell::loadFromLevel(level);  // Llamar al original PRIMERO
        // Logica del mod...
    }
};
```

**Reglas criticas seguidas:**
1. Llamar al original primero en `init()`
2. Usar `m_fields` para estado propio del hook
3. Usar `Ref<>` para objetos de Cocos2d
4. Usar `WeakRef` para callbacks async

### 3.3 Patron Observer (Event Bus)

```cpp
// Emision
ModEvents::get().emitThumbnailInvalidated(levelID);

// Suscripcion (en hook)
fields->m_invalidationListenerId = ThumbnailLoader::get().addInvalidationListener(
    [safeRef](int invalidLevelID) {
        auto selfRef = safeRef.lock();
        // ...
    });
```

### 3.4 Patron Async con Callbacks

Todo el trabajo pesado (descargas, decodificacion, blur) se hace async:

```cpp
WeakRef<PaimonLevelCell> safeRef = this;
ThumbnailLoader::get().requestLoad(levelID, fileName, 
    [safeRef, levelID](CCTexture2D* tex, bool success) {
        auto cellRef = safeRef.lock();
        auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
        if (!cell || !cell->getParent()) return;
        // Procesar textura...
    });
```

### 3.5 Patron Staged Loading (360fps optimization)

El `LevelCell` distribuye el trabajo en 3 etapas para no bloquear el frame:

```
Frame 0 (inmediato): Sprite + clipping node
Frame 1 (deferred): Gradient + blur async
Frame 2 (deferred): Particulas + view button
```

```cpp
// Etapa 0
setupClippingAndSeparator(bg, sprite);

// Etapa 1 — proximo frame
this->scheduleOnce(schedule_selector(PaimonLevelCell::deferredSetupGradient), 0.0f);

// Etapa 2 — otro frame
this->scheduleOnce(schedule_selector(PaimonLevelCell::deferredSetupExtras), 0.0f);
```

---

## 4. Flujo de Datos Principal

### 4.1 Thumbnail Loading Flow

```
LevelCell::tryLoadThumbnail()
    |
    +-- ThumbnailAPI::getThumbnails(levelID)  [async]
    |       |
    |       +-- HTTP GET /api/thumbnails/{levelID}
    |       +-- Callback: galleryThumbnails[]
    |
    +-- ThumbnailLoader::requestLoad(levelID, filename)
            |
            +-- Cache check (RAM)
            +-- Cache check (Disk)
            +-- HTTP GET /t/{levelId}  [async]
                    |
                    +-- Decode (PNG/WebP/GIF)
                    +-- CCTexture2D::initWithImage()
                    +-- Cache RAM + Disk
                    +-- Callback: texture
```

### 4.2 Background System Flow

```
MenuLayer::updateBackground()
    |
    +-- LayerBackgroundManager::getConfig("menu")
    |       +-- Type: default | custom | id | video | shader | random
    |
    +-- Segun tipo:
        |-- "custom": CCTextureCache::addImage(path)
        |-- "id": LocalThumbs::loadTexture(id)
        |-- "video": VideoPlayer + shaders
        |-- "shader": ShaderBgSprite + GLSL
        |-- "random": LocalThumbs::getAllLevelIDs() + RNG
```

### 4.3 Video Thumbnail Flow

```
LevelCell::tryLoadThumbnail()
    |
    +-- LocalThumbs::findAnyThumbnail(levelID)
    |       +-- Ends with .mp4?
    |           +-- VideoPlayer::create(path)
    |               +-- plm_create_from_filename()
    |               +-- Thread: decodeLoop()
    |               +-- Callback: frame texture
    |
    +-- O: VideoThumbnailSprite::createAsync(url)
            +-- HttpClient::download()
            +-- VideoPlayer + cache
```

### 4.4 Gallery Cycle Flow

```
LevelCell::updateGalleryCycle(dt)
    |
    +-- Check: transition guard timeout?
    +-- Scan window: buscar siguiente thumbnail listo
    |       +-- Video? -> VideoThumbnailSprite::isCached()
    |       +-- Image? -> ThumbnailLoader::isUrlLoaded()
    |
    +-- Found? -> crossfadeToThumb(texture)
    |       +-- applyGalleryTransition()
    |       +-- 20 tipos de transicion (crossfade, slide, zoom, etc.)
    |
    +-- Not found? -> requestGalleryThumbnail() + backoff
```

---

## 5. Sistema de Shaders

### 5.1 Arquitectura de Shaders

```
Shaders::
├── loadShader()              # Carga y cachea programas GLSL
├── getBgShaderProgram()      # Shaders para fondos
├── getBlurCellShader()       # Blur para celdas
├── ShaderBgSprite            # Sprite con shader de fondo
├── PaimonShaderSprite        # Sprite base con uniforms
├── PaimonShaderGradient      # Gradient animado
└── GLSLLoader                # Carga archivos .glsl
```

### 5.2 Shaders Disponibles

**Para celdas (cell_*.glsl):**
- sepia, sharpen, edge-detection, vignette
- pixelate, posterize, chromatic, scanlines
- solarize, rainbow, grayscale, invert, blur, glitch

**Para fondos (bg_*.glsl):**
- Variantes de los anteriores optimizados para fullscreen

### 5.3 Uniforms Comunes

```glsl
uniform float u_time;        // Tiempo para animaciones
uniform float u_intensity;   // Intensidad del efecto (0-1)
uniform vec2  u_texSize;     // Tamaño de la textura
uniform vec2  u_screenSize;  // Tamaño de pantalla
```

---

## 6. Sistema de Video

### 6.1 Componentes

```
VideoPlayer (C++ class)
├── plm_t* decoder            # Decodificador pl_mpeg
├── std::thread decodeThread  # Hilo de decodificacion
├── CCTexture2D* textures[3]  # Planos Y, Cb, Cr
├── CircularBuffer frames     # Buffer de frames
└── State: Stopped | Playing | Paused

VideoThumbnailSprite (CCSprite subclass)
├── VideoPlayer* player
├── bool m_hasVisibleFrame
└── Callback: onFirstVisibleFrame
```

### 6.2 Pipeline de Decodificacion

```
Archivo MP4 -> pl_mpeg -> Frames YUV -> Upload GL -> CCTexture2D
                |
                +-- Thread: decodeLoop()
                +-- Frame drop si buffer lleno
                +-- Loop infinito (para thumbnails)
```

---

## 7. Sistema de Menu Music

### 7.1 Componentes

```
MenuMusicPlayer (singleton)
├── FMOD::Channel* m_channel
├── std::string m_currentTrack
├── std::deque<string> m_history
└── TrackChangedListener[] listeners

YtDlpBootstrap
├── downloadYtDlp()           # Descarga binario yt-dlp
├── extractAudio()            # Extrae audio de YouTube
└── Callback: onComplete

FfmpegBootstrap
├── downloadFfmpeg()          # Descarga ffmpeg
├── convertToOgg()            # Convierte a formato GD
└── Callback: onComplete
```

### 7.2 Flujo de Reproduccion

```
Usuario ingresa URL de YouTube
    |
    +-- yt-dlp descarga audio
    +-- ffmpeg convierte a .ogg
    +-- MenuMusicPlayer::playTrack(path)
            +-- FMOD::System::createSound()
            +-- FMOD::System::playSound()
            +-- Notifica listeners
```

---

## 8. Threading Model

### 8.1 ThreadPool

```cpp
class ThreadPool {
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop = false;
};
```

Usado para:
- Decodificacion de imagenes
- Blur async
- Descargas HTTP
- Procesamiento de video

### 8.2 MainThreadDelay

```cpp
// Ejecutar callback en el hilo principal despues de un delay
MainThreadDelay::schedule(0.08f, [this]() {
    // Safe: estamos en el hilo de GL
});
```

### 8.3 Thread Safety Rules

1. **Nunca acceder al scene graph desde threads secundarios**
2. **Usar `WeakRef` para callbacks que referencian nodos**
3. **Usar `queueInMainThread` para actualizar UI**
4. **Los `Ref<>` manejan refcounting thread-safe**

---

## 9. Cache Hierarchy

### 9.1 Niveles de Cache

```
L1: RAM (CCTextureCache + ThumbnailCache LRU)
     +-- Capacidad: configurable (default 256MB)
     +-- TTL: 5 minutos
     +-- Eviccion: LRU

L2: Disk (geode/cache/paimbnails/)
     +-- Formatos: PNG, WebP, GIF, MP4
     +-- TTL: 30 dias
     +-- Limpieza: automatica al inicio

L3: Network (ThumbnailAPI)
     +-- CDN: Cloudflare
     +-- Retry: 3 intentos con backoff
     +-- Failed cache: 1 hora
```

### 9.2 Invalidation Strategy

```cpp
// Versionado por nivel
int getInvalidationVersion(int levelID);

// Cuando un usuario sube nueva thumbnail:
ThumbnailLoader::get().invalidateLevel(levelID);
// -> Incrementa version
// -> Notifica a todas las celdas via listener
// -> Celdas recargan automaticamente
```

---

## 10. Settings System

### 10.1 Versionado Atomico

```cpp
namespace paimon::settings::internal {
    inline std::atomic<uint64_t> g_settingsVersion{0};
}

// Al cambiar cualquier setting:
g_settingsVersion.fetch_add(1, std::memory_order_relaxed);

// En hooks:
if (fields->m_loadedSettingsVersion < g_settingsVersion.load()) {
    cacheSettings(); // Reload
}
```

### 10.2 Settings Clave

| Setting | Tipo | Default | Descripcion |
|---------|------|---------|-------------|
| `level-thumb-width` | double | 0.5 | Ancho de thumbnail en celda |
| `levelcell-hover-effects` | bool | true | Animaciones hover |
| `levelcell-gallery-autocycle` | bool | true | Rotacion automatica de galeria |
| `levelcell-background-blur` | double | 3.0 | Intensidad de blur |
| `bg-type` | string | "default" | Tipo de fondo de menu |
| `menu-music-enabled` | bool | false | Musica en menu |
| `compact-list-mode` | bool | false | Modo compacto |
| `transparent-list-mode` | bool | false | Listas transparentes |

---

## 11. Interaccion entre Hooks

### 11.1 MenuLayer <-> LevelCell

```
MenuLayer::updateBackground()
    +-- LayerBackgroundManager::applyVideoBg()
        +-- VideoPlayer::create()
        +-- Shared video state

LevelCell::tryLoadThumbnail()
    +-- ThumbnailLoader::requestLoad()
        +-- Mismo VideoPlayer (reutilizado)
```

### 11.2 LevelInfoLayer <-> LeaderboardsLayer

```
LevelInfoLayer::init()
    +-- Detecta si vino de LeaderboardsLayer
    +-- m_fields->m_fromLeaderboards = true
    +-- Afecta comportamiento de navegacion
```

### 11.3 DynamicPopupHook <-> BlurSystem

```
FLAlertLayer::init()
    +-- PaimonDynamicPopupHook captura
    +-- PopupBlurService::captureSceneTexture()
    +-- BlurSystem::buildPaimonBlurAsync()
    +-- Aplica blur al fondo
```

---

## 12. Performance Optimizations

### 12.1 LevelCell (3900 lineas de optimizaciones)

| Tecnica | Beneficio |
|---------|-----------|
| Viewport culling | No renderiza celdas fuera de pantalla |
| Staged loading (3 etapas) | Distribuye trabajo en frames |
| Lazy static thumbnail | Carga PNG/JPEG directo sin decode |
| Typeinfo cache | Evita 3x RTTI por tick |
| Fast-swap texture | Reusa nodos si mismo nivel |
| Gallery scan window | Solo prefetch N siguientes |
| Maintenance tick (200ms) | Health check async |
| Request ID versioning | Invalida callbacks viejos |
| WeakRef en callbacks | Evita dangling pointers |

### 12.2 MenuLayer

| Tecnica | Beneficio |
|---------|-----------|
| Ref<> en callbacks async | Evita leaks |
| WeakRef en bubble tick | Safe si nodo destruido |
| GIF pin/unpin | Cache de GIFs animados |
| Video shared cache | Reusa decoders |
| Force evict stale videos | Limpia RAM al cambiar bg |

---

## 13. Potential Issues & Mitigations

### 13.1 Memory Pressure

**Problema:** Muchos `Ref<>` + texturas grandes + videos

**Mitigaciones:**
- `LayerBackgroundManager::forceEvictAllStaleVideos()`
- `ThumbnailCache` con LRU y TTL
- `CCTextureCache::removeTextureForKey()` al cambiar fondo

### 13.2 Callback Hell

**Problema:** 3+ niveles de lambdas anidadas en async

**Mitigacion:**
- `WeakRef` en cada nivel
- `requestId` / `galleryToken` para invalidar
- `m_isBeingDestroyed` flag

### 13.3 Scene Graph Thread Safety

**Problema:** Callbacks async tocan nodos

**Mitigacion:**
- `getParent()` check antes de modificar
- `queueInMainThread` para ops de UI
- `Ref<>` retiene objetos vivos

---

## 14. Extension Points

Para agregar nuevos features:

1. **Nuevo hook:** Crear `src/hooks/NuevaClase.cpp`
2. **Nuevo manager:** Crear `src/features/nuevo/services/Manager.hpp`
3. **Nuevo shader:** Agregar `.glsl` a `resources/shaders/`
4. **Nuevo setting:** Registrar en `src/core/Settings.hpp`
5. **Nuevo evento:** Agregar a `src/framework/ModEvents.hpp`

---

## 15. Conclusion

Paimbnails es un mod arquitectonicamente sofisticado que demuestra:

- **Dominio de Geode SDK v5:** Hooks, fields, Ref<>, WeakRef
- **Manejo de async:** Callbacks seguros con invalidacion
- **Performance:** Staged loading, viewport culling, caching multi-nivel
- **Extensibilidad:** Sistema modular con event bus
- **Robustez:** Manejo de errores, retry logic, graceful degradation

La complejidad del `LevelCell` (3900 lineas) refleja la cantidad de edge cases manejados: reciclaje de celdas, cambios de nivel, galeria async, video, hover effects, compact mode, transparent mode, y 20 tipos de transiciones.

El mod funciona como un **sistema operativo visual** sobre Geometry Dash, interceptando y enriqueciendo casi todos los elementos de UI del juego.
