# Plan: Paimon Texture Studio

> Sistema in-game para crear y aplicar texture packs algorítmicamente, sin necesidad
> de overlays pre-pintados. El usuario carga un `.plist + .png` (suyo o detectado
> automáticamente desde la carpeta de GD), elige 3 colores (Color 1, Color 2, Glow)
> + brillo, y el sistema genera un texture pack compatible con Texture Loader.
>
> **Estrategia**: Híbrida (D). Auto-detección por clustering de colores + override
> manual por sprite cuando el algoritmo no convence.
>
> **Ubicación**: Botón nuevo en `MenuLayer` (pantalla principal de GD).
>
> **Convivencia**: Independiente de `colorful-icons`. Ese sigue manejando íconos
> del jugador en runtime. `texture-studio` maneja el menú/UI vía texture pack.

---

## 1. Arquitectura general

### Capas (de bajo a alto nivel)

```
┌──────────────────────────────────────────────────────────────┐
│  UI Layer                                                    │
│  TextureStudioLayer · SlotsGridView · SpriteEditorPopup      │
│  ColorPickerRow · LivePreviewNode                            │
└────────────────────────────┬─────────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────────┐
│  Persist Layer                                               │
│  TextureProject (struct) · SlotStore (matjson)               │
│  ManualOverrideStore                                         │
└────────────────────────────┬─────────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────────┐
│  Engine Layer                                                │
│  ColorClustering (k-means HSV)                               │
│  MaskBuilder (clusters → 3 máscaras virtuales)               │
│  LuminanceTinter (PackGen-style, R8 mask × tint color)       │
│  PackExporter (PNG repack + plist regen + zip)               │
│  TextureLoaderInstaller (copia a carpeta + apply)            │
└────────────────────────────┬─────────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────────┐
│  Data Layer                                                  │
│  PlistParser · SpritesheetReader · ImageBuffer (RGBA8)       │
│  RectPacker (shelf algorithm)                                │
│  GdResourcesLocator (autodetect plists vanilla)              │
└──────────────────────────────────────────────────────────────┘
```

### Estructura de archivos

```
src/features/texture-studio/
├── TextureStudio.hpp                    # API pública (entrypoint)
├── TextureStudio.cpp
│
├── data/
│   ├── ImageBuffer.hpp                  # RGBA8 con stride, pad, etc.
│   ├── ImageBuffer.cpp
│   ├── PlistParser.hpp                  # cocos2d-x plist → SpriteFrameInfo[]
│   ├── PlistParser.cpp
│   ├── PlistBuilder.hpp                 # SpriteFrameInfo[] → plist XML
│   ├── PlistBuilder.cpp
│   ├── SpriteFrameInfo.hpp              # rect, offset, rotated, size
│   ├── SpritesheetReader.hpp            # carga PNG + plist en RAM
│   ├── SpritesheetReader.cpp
│   ├── RectPacker.hpp                   # shelf packer (compatible PackGen)
│   ├── RectPacker.cpp
│   ├── GdResourcesLocator.hpp           # detecta carpeta Resources de GD
│   └── GdResourcesLocator.cpp
│
├── engine/
│   ├── ColorClustering.hpp              # k-means HSV con clasificador
│   ├── ColorClustering.cpp
│   ├── ClusterClassifier.hpp            # heurísticas: cuál es C1/C2/Glow/Outline
│   ├── ClusterClassifier.cpp
│   ├── MaskBuilder.hpp                  # clusters → máscaras (R8 alpha)
│   ├── MaskBuilder.cpp
│   ├── LuminanceTinter.hpp              # tinting estilo PackGen
│   ├── LuminanceTinter.cpp
│   ├── ManualMask.hpp                   # representación de override pintado
│   ├── ManualMask.cpp
│   ├── PackExporter.hpp                 # genera .zip Texture Loader
│   ├── PackExporter.cpp
│   ├── TextureLoaderInstaller.hpp       # copia a packs/ y aplica
│   └── TextureLoaderInstaller.cpp
│
├── persist/
│   ├── TextureProject.hpp               # un slot = un proyecto
│   ├── TextureProject.cpp
│   ├── SlotStore.hpp                    # lista de slots, save/load
│   └── SlotStore.cpp
│
├── ui/
│   ├── TextureStudioLayer.hpp           # layer principal
│   ├── TextureStudioLayer.cpp
│   ├── SlotsGridView.hpp                # grid de slots (cards)
│   ├── SlotsGridView.cpp
│   ├── NewProjectPopup.hpp              # dialog: nombre + selección plist
│   ├── NewProjectPopup.cpp
│   ├── ProjectEditorLayer.hpp           # editar un slot existente
│   ├── ProjectEditorLayer.cpp
│   ├── SpriteEditorPopup.hpp            # editor por sprite individual
│   ├── SpriteEditorPopup.cpp
│   ├── ColorPickerRow.hpp               # 3 color pickers + brightness
│   ├── ColorPickerRow.cpp
│   ├── LivePreviewNode.hpp              # preview en vivo de N sprites
│   ├── LivePreviewNode.cpp
│   ├── ManualPaintCanvas.hpp            # canvas para pintar máscaras a mano
│   └── ManualPaintCanvas.cpp
│
└── hooks/
    └── MenuLayerEntry.cpp               # botón nuevo en MenuLayer
```

---

## 2. Algoritmo central — Detección automática de colores

### 2.1 Modelo de un sprite

Cada sprite del .plist se carga como `ImageBuffer` (RGBA8). Lo descomponemos en:

```
sprite_pixel = base_dark + α₁·tint(C1, lum) + α₂·tint(C2, lum) + α_glow·tint(Glow, lum)
```

Donde:
- `base_dark` = los píxeles que NO se tintan (outlines, sombras puras, fondo transparente).
- `α₁, α₂, α_glow ∈ [0, 1]` = pertenencia a cada cluster (las "máscaras virtuales").
- `tint(C, lum)` = `C · (lum / brightness_param)` (algoritmo de PackGen, Rec.601).

Nuestro objetivo es **inferir las 3 máscaras** sin tener overlays reales.

### 2.2 Pipeline de auto-detección

#### Paso 1 — Quantización
Convertir todos los píxeles con `α > 0` a HSV. Excluir píxeles con `α < 16` (anti-aliasing residual no nos interesa).

#### Paso 2 — k-means en espacio HSV
Cluster con `k = 4..6`. Distancia ponderada:
```
d(p, c) = w_h · circular_dist(h_p, h_c) + w_s · |s_p - s_c| + w_v · |v_p - v_c|
```
Pesos: `w_h = 0.5, w_s = 0.3, w_v = 0.2` (el hue manda).

Para el botón Play de tu screenshot (k=4 esperaríamos):
- Cluster 1: H≈0°, S≈0, V≈0.7 → grises claros (los cuadritos)
- Cluster 2: H≈0°, S≈0, V≈0.05 → negro (cuerpo)
- Cluster 3: H≈280°, S≈0.5, V≈0.6 → violeta (outline)
- Cluster 4: H≈0°, S≈0, V≈1 → blanco puro (highlights de la flecha)

#### Paso 3 — Clasificación heurística

Para cada cluster computamos features:
- `pixel_count` (qué tan común es)
- `avg_v`, `avg_s` (luminancia, saturación)
- `border_ratio` (qué fracción de píxeles del cluster está en el borde del sprite no-transparente)
- `connected_components` (si es contiguo o disperso)

Reglas (en orden de prioridad):

| Cluster characteristic | Rol asignado |
|---|---|
| `avg_v < 0.15` AND `avg_s < 0.2` | **Outline** (negro, no se tinta) |
| `avg_s > 0.3` AND `border_ratio > 0.6` AND `pixel_count` ≤ 30% del total | **Glow** |
| `avg_s < 0.25` AND `avg_v > 0.4` (más píxeles) | **Color 1** (zonas grises del Play) |
| `avg_v < 0.4` AND `avg_s < 0.3` (más píxeles, pero no outline) | **Color 2** (negro central del Play) |
| Cualquier otro | **Color 1 secundario** (se mergea con Color 1) |

Si el clasificador no logra asignar los 3 roles claramente, el sprite se marca como `needs_manual_review` y aparece con un icono de advertencia en el editor.

#### Paso 4 — Construcción de máscaras

Para cada píxel `p` del sprite:
- Calcula `softmax_distance` a cada cluster (más cerca = más pertenencia)
- `α₁(p)` = pertenencia a cluster_C1 × `α(p) / 255`
- `α₂(p)` = pertenencia a cluster_C2 × `α(p) / 255`
- `α_glow(p)` = pertenencia a cluster_glow × `α(p) / 255`
- `α_outline(p)` = pertenencia a cluster_outline × `α(p) / 255`

Las máscaras se guardan como buffers R8 (1 byte por píxel × W × H).

#### Paso 5 — Tinting + composición

Para cada máscara `(α_i, color_i)`:
```
tinted_i(p) = color_i · (luminance_original(p) / brightness_param)
output(p) += α_i(p) · tinted_i(p)
```

Outline se preserva tal cual: `output(p) += α_outline(p) · original_pixel(p)`.

Píxeles donde la suma de todas las máscaras es < 1 se completan con el original (preservando detalles que el clasificador no entendió).

### 2.3 Override manual

Cuando el usuario abre `SpriteEditorPopup` para un sprite, ve:
- El sprite original a la izquierda
- 3 máscaras coloreables (C1 verde, C2 azul, Glow amarillo) superpuestas con opacidad 50%
- Sliders para reasignar clusters: "el cluster que el algoritmo dijo que es C2, en realidad es Glow"
- **Pincel manual** que pinta directamente sobre cualquiera de las 3 máscaras (modos: pintar+, borrar−, intercambiar)
- Botón "Reset to auto" que descarta el override
- El override se guarda en el slot:

```json
"manual_overrides": {
  "GJ_playBtn_001.png": {
    "version": 1,
    "mask_c1": "<base64-encoded R8>",
    "mask_c2": "<base64-encoded R8>",
    "mask_glow": "<base64-encoded R8>",
    "uses_auto_for_outline": true
  }
}
```

Si un sprite no tiene override, se usa el resultado del algoritmo automático.

---

## 3. Esquema de datos

### 3.1 TextureProject (un slot)

```cpp
struct TextureProject {
    std::string name;                        // "Mi Pack Rosa"
    std::string id;                          // slug autogenerado: "mi_pack_rosa_a1b2"
    std::string sourcePlistPath;             // de dónde se cargó el .plist
    std::string sourcePngPath;               // de dónde se cargó el .png
    std::int64_t createdAt;                  // unix ms
    std::int64_t modifiedAt;                 // unix ms
    
    // Colores principales del pack
    cocos2d::ccColor3B color1{149, 226, 3};     // verde por defecto (PackGen default)
    cocos2d::ccColor3B color2{28, 233, 255};    // cyan por defecto
    cocos2d::ccColor3B colorGlow{255, 255, 255};
    int brightness = 160;                       // 100..300
    
    // Opciones globales
    bool includeMediumPort = false;
    bool useAlternativeGlowOverlay = false;
    
    // Estado del último build
    bool hasBuiltOnce = false;
    std::int64_t lastBuiltAt = 0;
    std::string lastExportedZipPath;
    
    // Overrides manuales por sprite
    std::map<std::string, ManualOverride> manualOverrides;
    
    // Cache de detección automática (para no rehacer el k-means al reabrir)
    std::map<std::string, AutoDetectionCache> autoCache;
};
```

### 3.2 ManualOverride

```cpp
struct ManualOverride {
    int version = 1;
    std::vector<std::uint8_t> maskC1;        // R8, W*H bytes
    std::vector<std::uint8_t> maskC2;
    std::vector<std::uint8_t> maskGlow;
    std::vector<std::uint8_t> maskOutline;
    int width;
    int height;
    bool exists() const { return !maskC1.empty(); }
};
```

### 3.3 AutoDetectionCache

```cpp
struct AutoDetectionCache {
    int version = 1;
    std::uint64_t spriteHash;                // hash del PNG original, invalida si cambia
    std::array<ClusterInfo, 6> clusters;
    int clusterCount;
};

struct ClusterInfo {
    cocos2d::ccColor3B avgColor;
    float avgSaturation;
    float avgValue;
    float borderRatio;
    int pixelCount;
    enum class Role { Outline, Color1, Color2, Glow, Unassigned } role;
};
```

### 3.4 Persistencia

Cada slot vive en disco como:
```
<save-dir>/texture-studio/slots/<slot_id>/
├── project.json          # TextureProject serializado (sin máscaras grandes)
├── overrides/
│   └── <sprite_name>.bin # ManualOverride binario (más eficiente que base64)
├── cache/
│   └── auto.bin          # AutoDetectionCache de todos los sprites
└── output/
    └── pack.zip          # último .zip exportado (para reaplicar sin regenerar)
```

La lista de slots se mantiene en `<save-dir>/texture-studio/slots.json`:
```json
{
  "version": 1,
  "active_slot_id": "mi_pack_rosa_a1b2",
  "slots": [
    {"id": "mi_pack_rosa_a1b2", "name": "Mi Pack Rosa", "modified": 1731600000},
    {"id": "neon_pack_c3d4", "name": "Neon Pack", "modified": 1731500000}
  ]
}
```

---

## 4. UI Mockup (textual)

### 4.1 Botón en MenuLayer
```
┌──────────────────────────────────────────────┐
│       ████  PLAY  ████                       │
│                                              │
│                                              │
│   [creator] [garage] [Texture Studio▶]       │  ← botón nuevo aquí
└──────────────────────────────────────────────┘
```

### 4.2 TextureStudioLayer
```
┌──────────────────────────────────────────────────────────┐
│ ◀  Texture Studio                          [+ New Pack]  │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  ┌─────────────────────┐  ┌─────────────────────┐        │
│  │   ◾◾◾ Preview       │  │   ◾◾◾ Preview       │        │
│  │                     │  │                     │        │
│  │ "Mi Pack Rosa"      │  │ "Neon Pack"         │        │
│  │ 482 sprites          │  │ 250 sprites         │        │
│  │ Last edit: 2 min ago│  │ Last edit: 1 day ago│        │
│  │ [Apply] [Edit] [✕]  │  │ [Apply] [Edit] [✕]  │        │
│  └─────────────────────┘  └─────────────────────┘        │
│                                                          │
│  ┌─────────────────────┐                                 │
│  │   + Empty slot      │                                 │
│  │   Click to create   │                                 │
│  └─────────────────────┘                                 │
│                                                          │
├──────────────────────────────────────────────────────────┤
│  Active: "Mi Pack Rosa"     [Reload Game] [Folder] [?]   │
└──────────────────────────────────────────────────────────┘
```

### 4.3 NewProjectPopup
```
┌──────────────────────────────────────┐
│        Create New Texture Pack       │
├──────────────────────────────────────┤
│ Name:     [My Pack___________]       │
│                                      │
│ Source:                              │
│ ◉ Auto-detect from GD Resources      │
│ ○ Choose .plist + .png manually      │
│                                      │
│ Detected sheets:                     │
│ ☑ GJ_GameSheet01    (482 sprites)    │
│ ☑ GJ_GameSheet02    (320 sprites)    │
│ ☐ GauntletSheet     ( 12 sprites)    │
│ ☐ FireSheet         (  8 sprites)    │
│ ☐ GJ_LaunchSheet    ( 50 sprites)    │
│                                      │
│ Quality: ◉ UHD  ○ HD  ○ Low          │
│                                      │
│         [Cancel]    [Create]         │
└──────────────────────────────────────┘
```

### 4.4 ProjectEditorLayer
```
┌──────────────────────────────────────────────────────────┐
│ ◀  Editing: "Mi Pack Rosa"           [Generate Pack ▶]   │
├──────────────────────────────────────────────────────────┤
│ Color 1:  [● #95E203]    Color 2:  [● #1CE9FF]           │
│ Glow:     [● #FFFFFF]    Brightness: [────●───] 160      │
│ ☐ Include Medium port  ☐ Use alternative glow overlay    │
├──────────────────────────────────────────────────────────┤
│ Sprites (482):                          [Search______]   │
│                                                          │
│ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐         │
│ │auto│ │auto│ │auto│ │ MAN│ │auto│ │auto│ │ ⚠  │         │
│ │play│ │menu│ │stop│ │play│ │... │ │... │ │... │         │
│ └────┘ └────┘ └────┘ └────┘ └────┘ └────┘ └────┘         │
│ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐         │
│ │auto│ │auto│ │auto│ │auto│ │auto│ │auto│ │auto│         │
│                                                          │
│  Click any sprite to manually edit · ⚠ = needs review    │
└──────────────────────────────────────────────────────────┘
```

### 4.5 SpriteEditorPopup
```
┌──────────────────────────────────────────────────────┐
│              Edit: GJ_playBtn_001.png            [✕] │
├──────────────────────────────────────────────────────┤
│  ┌──────────┐  Detected clusters (4):                │
│  │          │   ● #BFBFBF · Color 1   [▼]  47% px    │
│  │   ◼◼◼   │   ● #1A1A1A · Color 2   [▼]  31% px    │
│  │  PREVIEW │   ● #9B59E0 · Glow      [▼]  18% px    │
│  │          │   ● #000000 · Outline   [▼]   4% px    │
│  └──────────┘                                        │
│                                                      │
│  Mask layers:                                        │
│  ☑ Show C1 mask (green)                              │
│  ☑ Show C2 mask (blue)                               │
│  ☑ Show Glow mask (yellow)                           │
│                                                      │
│  Manual paint:                                       │
│  ◉ Brush     ○ Eraser                                │
│  Size: [──●────] 8 px                                │
│  Target: ◉ C1  ○ C2  ○ Glow  ○ Outline               │
│                                                      │
│  [Reset to auto]  [Cancel]  [Apply]                  │
└──────────────────────────────────────────────────────┘
```

---

## 5. Decisiones técnicas justificadas

### 5.1 Por qué clustering HSV en lugar de RGB
HSV separa hue (qué color) de saturation y value (cómo de oscuro/saturado). Para detectar
"el verde del menú" vs "el cyan del menú", la distancia angular en hue es mucho más
robusta que distancia euclídea en RGB.

### 5.2 Por qué Rec.601 en lugar de Rec.709
PackGen usa `0.3·R + 0.59·G + 0.11·B`. Lo replicamos para que packs generados con
nuestro mod tengan el mismo "look" que los de PackGen.

### 5.3 Por qué shelf packer en lugar de maxrects
PackGen usa shelf simple. Es ~30% peor en aprovechamiento de espacio que maxrects,
pero es 10× más simple de implementar y probar. En fase 9 podemos cambiar a maxrects
si la diferencia se nota.

### 5.4 Por qué CCImage + addImage en lugar de cargar texturas a mano
GD ya tiene infraestructura para `CCTextureCache::addImage(path)`. Si exportamos
nuestros .png a disco y le decimos "recarga este path", funciona out-of-the-box.
Para hot-swap (fase 9) usaremos `CCTextureCache::removeTexture` + `addImage`.

### 5.5 Por qué overrides binarios en lugar de base64
Una máscara R8 de un sprite 256×256 = 64KB. En base64 son 86KB. Para 100 sprites con
override son 6.4MB vs 8.6MB. El binario también es 3-5× más rápido de leer/escribir.

### 5.6 Por qué k-means y no HDBSCAN o GMM
k-means con k fijo es trivial de implementar (~30 líneas), determinista (con seed
fija), rápido (<5ms por sprite 256×256). Métodos más sofisticados darían mejor
calidad pero quintuplicarían la complejidad. Si fase 9 muestra que la calidad no
basta, migramos a GMM o k-means++.

### 5.7 Por qué stb_image y no cocos2d para load PNG
Ya tienes `stb_image.h` en `src/utils/`. Cargar a `ImageBuffer` (RGBA8 plano) nos
da control total para clustering. cocos2d expone `CCImage` pero su API es awkward
para procesamiento (no expone `getData()` de manera clean en todas las versiones).

### 5.8 Por qué `geode.texture-loader` como dependencia obligatoria (no opcional)
La alternativa es implementar nuestro propio sistema de texture override en runtime
(hot-swap del CCTextureCache). Esto es factible pero:
- Requiere hooks en cada layer que use sprites
- Conflicto seguro con `geode.texture-loader` si el usuario lo tiene activo
- Recargar texturas durante gameplay es peligroso

Texture Loader tiene 4.5M descargas y es estable. Aprovechémoslo.

---

## 6. Tasks list completo (fases)

### Fase 1 — Núcleo de datos y parser (offline-testable)
- [ ] `data/ImageBuffer.hpp/cpp` — RGBA8 con stride, init/copy/save/load
- [ ] `data/SpriteFrameInfo.hpp` — struct con rect, offset, rotated, sourceSize
- [ ] `data/PlistParser.hpp/cpp` — parser cocos2d-x v2/v3 (formats 0/1/2/3)
- [ ] `data/PlistBuilder.hpp/cpp` — generador inverso, compatible PackGen
- [ ] `data/SpritesheetReader.hpp/cpp` — carga .png con stb + frames del .plist
- [ ] `data/RectPacker.hpp/cpp` — shelf packer con limit 4096
- [ ] `data/GdResourcesLocator.hpp/cpp` — detecta carpeta GD por SO
- [ ] **Test offline**: cargar `GJ_GameSheet01-uhd.plist`, listar 482 sprites, extraer 5 al azar como PNGs separados

### Fase 2 — Engine de color clustering + tinting
- [ ] `engine/ColorClustering.hpp/cpp` — k-means HSV con k=4..6
- [ ] `engine/ClusterClassifier.hpp/cpp` — heurísticas → asignación de roles
- [ ] `engine/MaskBuilder.hpp/cpp` — clusters → máscaras R8 con softmax
- [ ] `engine/LuminanceTinter.hpp/cpp` — algoritmo PackGen con SIMD opcional
- [ ] **Test offline**: tomar el botón Play, generar máscaras, tintar con (#FF00FF, #00FFFF, #FFFFFF), comparar con output de PackGen

### Fase 3 — Pack exporter (Texture Loader compatible)
- [ ] `engine/PackExporter.hpp/cpp` — orquesta clustering+tint+repack para todos los sprites
- [ ] Generación `pack.json`, `ui/colors.json`, `ui/ModsLayer.json`, `ui/LoadingLayer.json` (similar a PackGen)
- [ ] Empacado en `.zip` con miniz (header-only) o JSZip-like
- [ ] **Test offline**: exportar pack completo, abrir el .zip, verificar estructura

### Fase 4 — Slot system de persistencia
- [ ] `persist/TextureProject.hpp/cpp` — struct + matjson serialize/deserialize
- [ ] `persist/SlotStore.hpp/cpp` — list, create, delete, get, update
- [ ] `persist/AutoDetectionCache` con hash de invalidación
- [ ] **Test offline**: crear 3 proyectos, guardar, reiniciar mod, verificar que cargan

### Fase 5 — UI Layer principal (TextureStudioLayer)
- [ ] `ui/TextureStudioLayer.hpp/cpp` — layer con grid + footer
- [ ] `ui/SlotsGridView.hpp/cpp` — cards scrollables
- [ ] `ui/NewProjectPopup.hpp/cpp` — auto-detect + selección manual
- [ ] `ui/ColorPickerRow.hpp/cpp` — 3 pickers + slider brightness
- [ ] **Test in-game**: abrir layer, crear pack, ver en grid

### Fase 6 — Editor por sprite (override manual)
- [ ] `ui/ProjectEditorLayer.hpp/cpp` — grid de sprites del pack
- [ ] `ui/SpriteEditorPopup.hpp/cpp` — preview + clusters + reasignación
- [ ] `ui/ManualPaintCanvas.hpp/cpp` — pintar máscaras a mano
- [ ] `engine/ManualMask.hpp/cpp` — IO binario de overrides
- [ ] **Test in-game**: editar un sprite, reasignar cluster, pintar, regenerar pack

### Fase 7 — Hook en MenuLayer (botón principal)
- [ ] `hooks/MenuLayerEntry.cpp` — botón en barra inferior con sprite "TS"
- [ ] Setting toggle "Enable Texture Studio button" en mod.json
- [ ] **Test in-game**: ver botón, abrirlo

### Fase 8 — Integración con Texture Loader (apply)
- [ ] `engine/TextureLoaderInstaller.hpp/cpp` — copia zip a `<config>/geode.texture-loader/packs/`
- [ ] Detectar si está instalado, mostrar warning si no
- [ ] Sugerir reload (no podemos forzarlo de manera segura)
- [ ] Setting "Auto-apply on generate" (default true)
- [ ] **Test in-game**: generar pack, ver que aparece en lista de Texture Loader

### Fase 9 — Pulido (preview en vivo, hot-swap opcional, presets)
- [ ] Live preview de N sprites mientras se mueven los color pickers
- [ ] Presets de paleta (Neon, Pastel, Vaporwave, etc.)
- [ ] Export/import de proyectos como `.paimon-pack` (zip con project.json + overrides)
- [ ] Soporte de batch: aplicar override a varios sprites a la vez
- [ ] Hot-swap experimental: `CCTextureCache::removeTexture` + `addImage` sin reload del juego
- [ ] Localization completa (todos los strings en `Localization.hpp`)

---

## 7. Riesgos identificados y mitigaciones

| Riesgo | Probabilidad | Impacto | Mitigación |
|---|---|---|---|
| Detección automática da resultados raros en sprites no-vanilla | Alta | Medio | Override manual desde la fase 1 + flag "needs_review" |
| .plist con formatos exóticos (cocos2d 2.0 vs 3.0) | Media | Alto | Soportar formats 0/1/2/3 desde el principio, copiar tests de PackGen |
| Memoria pico al exportar pack completo | Media | Alto | Procesar sheet por sheet, liberar tras cada uno; budget máximo 256MB |
| Texture Loader no instalado | Alta | Bajo | Mostrar instrucciones de instalación, link al mod |
| Usuario edita .plist mientras se procesa | Baja | Medio | Hash de invalidación de cache, reset si no coincide |
| Cocos2d sprites rotados al packear | Alta | Alto | PackGen ya nos enseña el manejo (rotación 90° + reflect); copiamos ese código |
| Crash al volver de la layer si hay export en background | Media | Alto | Cancellation token; el export se aborta limpiamente al salir |
| GD update cambia el formato de los sheets | Baja | Alto | Versionar el cache, regenerar si la versión del .plist cambia |
| Plataforma móvil sin file picker | Alta | Medio | En móvil solo permitimos auto-detect, no manual upload |

---

## 8. Métricas de éxito

- [ ] Detección automática produce un resultado **aceptable** (sin override manual) en
      ≥80% de sprites del menú vanilla.
- [ ] Tiempo de generación de pack completo (5 sheets, ~1500 sprites): <30s en desktop,
      <90s en móvil.
- [ ] Memoria pico durante generación: <256MB en desktop, <96MB en móvil.
- [ ] Tamaño del .zip exportado: comparable o menor a PackGen para los mismos colores.
- [ ] Slot save/load: <100ms para slot con 100 overrides.
- [ ] UI responde a 60fps mientras se mueven color pickers (preview throttled).

---

## 9. Convivencia con `colorful-icons`

Son dos features completamente independientes. No comparten estado.

| Feature | Qué recolorea | Cuándo | Cómo |
|---|---|---|---|
| `colorful-icons` | Íconos del jugador (cube/ship/ball/etc.) | Runtime, en cada layer que muestre íconos | `SimplePlayer::setColors()` |
| `texture-studio` | Sprites del menú/UI (botones, fondos, faces) | Una vez, generando un texture pack | Vía `geode.texture-loader` |

Ambos pueden estar activos al mismo tiempo. El texture pack generado por `texture-studio`
NO afecta a los íconos del jugador (esos los maneja `SimplePlayer` en runtime).

---

## 10. Localization keys nuevas

```cpp
// utils/Localization.hpp añadirá:
namespace texture_studio {
    LANG(title, "Texture Studio");
    LANG(button_open, "Texture Studio");
    LANG(new_pack, "New Pack");
    LANG(slots_empty, "No packs yet. Click \"+ New Pack\" to create one.");
    LANG(slot_apply, "Apply");
    LANG(slot_edit, "Edit");
    LANG(slot_delete, "Delete");
    LANG(name_placeholder, "Enter pack name");
    LANG(source_auto, "Auto-detect from GD Resources");
    LANG(source_manual, "Choose .plist + .png manually");
    LANG(quality_uhd, "UHD");
    LANG(quality_hd, "HD");
    LANG(quality_low, "Low");
    LANG(color1, "Color 1");
    LANG(color2, "Color 2");
    LANG(glow, "Glow");
    LANG(brightness, "Brightness");
    LANG(generate_pack, "Generate Pack");
    LANG(generating, "Generating pack...");
    LANG(generated_ok, "Pack generated successfully!");
    LANG(needs_review, "Needs manual review");
    LANG(cluster_role_outline, "Outline");
    LANG(cluster_role_c1, "Color 1");
    LANG(cluster_role_c2, "Color 2");
    LANG(cluster_role_glow, "Glow");
    LANG(reset_to_auto, "Reset to auto");
    LANG(brush, "Brush");
    LANG(eraser, "Eraser");
    LANG(brush_size, "Size");
    LANG(brush_target, "Target");
    LANG(no_textureldr, "Texture Loader is not installed. Install it from the Geode index.");
    LANG(reload_to_apply, "Reload the game to see the changes.");
}
```

---

## 11. Settings nuevos en `mod.json`

```json
"texture-studio-title": {
    "name": "Texture Studio",
    "description": "Algorithmic texture pack generator",
    "type": "title"
},
"texture-studio-enabled": {
    "type": "bool",
    "name": "Enable Texture Studio",
    "description": "Show the Texture Studio button in the main menu.",
    "default": true
},
"texture-studio-auto-apply": {
    "type": "bool",
    "name": "Auto-apply on Generate",
    "description": "Automatically copy generated packs to Texture Loader.",
    "default": true
},
"texture-studio-medium-port": {
    "type": "bool",
    "name": "Always include Medium quality port",
    "description": "Generate -hd files alongside -uhd by default.",
    "default": false
}
```

---

## 12. Dependencias nuevas en `mod.json`

```json
"geode.texture-loader": {
    "version": ">=v1.10.0",
    "required": false
}
```

`required: false` permite que el mod siga funcionando si Texture Loader no está,
pero la feature de Apply muestra un warning. La generación del .zip funciona
siempre.

---

## 13. Cierre y próximos pasos

Este documento es la fuente de verdad. Cada PR/commit del feature debe referenciar
una task de la sección 6. Las decisiones de arquitectura no se cambian sin actualizar
este plan.

**Orden de ejecución**: Fase 1 → 2 → 3 (núcleo offline) → 4 → 5 → 7 (UI mínima) →
6 (override) → 8 (apply) → 9 (pulido).

**MVP utilizable**: al final de la Fase 8.
**Producto completo**: al final de la Fase 9.

---

*Plan creado: 2026-05-28 · Versión 1.0*
