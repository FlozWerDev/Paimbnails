# Geometry Dash Level Renderer & Interactive Editor Viewer

Renderizador preciso e interactivo para niveles del editor de **Geometry Dash** desarrollado en **Python**.

Renderiza la geometría estática del editor con las texturas instaladas del juego.
El visor y la exportación PNG comparten el mismo motor de dibujo.

---

## Características

- **Gráficos 100% Originales de RobTop**: Extrae y decodifica directamente las hojas de sprites originales (`GJ_GameSheet-uhd.plist`, `GJ_GameSheet02-uhd.plist`, `GJ_GameSheetGlow-uhd.plist`, etc.) desde la carpeta de recursos de Geometry Dash.
- **Geometría y composición**:
  - Corrección precisa de sprites rotados en atlas (`textureRotated == True` corregido a 90° anti-horario).
  - Reconstrucción de dimensiones sin recortar y aplicación exacta de `spriteOffset`.
  - Descomposición multicapa: Base, Detalle (Color 2) y Glow (brillo aditivo `GL_SRC_ALPHA, GL_ONE`).
  - Orden estricto de capas Z (`B5`, `B4`, `B3`, `B2`, `B1`, Suelo, `T1`, `T2`, `T3`, `T4`) y orden Z dentro de cada capa (`Key 25`).
  - Transformaciones completas de GD 2.2: escala fraccional (`scaleX`, `scaleY`), rotación (`rotation` y ejes independientes 131/132), inversión horizontal/vertical (`flipX`, `flipY`), y desplazamientos HSV (`baseHSV`, `detailHSV`).
- **Formatos de nivel**:
  - Carga directa desde tus niveles del editor local en `CCLocalLevels.dat` (por nombre o número).
  - Carga archivos exportados `.gmd` en XML o gzip.
  - Carga `.dat` cifrados: abre el primer nivel no vacío del archivo.
  - Carga archivos `.txt` o `.level` (cadenas de nivel descomprimidas o codificadas en Base64 + Gzip).
- **Visor interactivo Pygame**:
  - Desplazamiento (Pan) con clic derecho o rueda central del ratón, o flechas / WASD.
  - Zoom centrado en el cursor (desde vista general del nivel hasta 800% de detalle).
  - Rejilla del editor estilo GD (30x30 unidades) conmutables (`G`).
  - Fondo (`B`), suelo (`F`) y resplandores (`L`) conmutables.
  - Inspección de objetos: haz clic en cualquier bloque/trigger para ver su ID, coordenadas, rotación, escala y canales de color.
  - Exportación de capturas PNG (`S` o `F12`), conservando el ajuste de Glow.
  - Reutiliza el fotograma cuando la cámara no cambia. El coste de dibujar al moverla depende del nivel, la resolución y el antialiasing.
- **Modo Render Headless (CLI)**:
  - Genera capturas de alta definición o miniaturas directamente desde la consola o scripts de Python.

---

## Requisitos

- Python 3.10+
- `pillow`
- `numpy`
- `pygame` y `tkinter`, solo para el visor interactivo

Para exportar imágenes solo se necesitan Pillow y NumPy; no se importa la interfaz gráfica.

```powershell
python -m pip install pillow numpy
# Opcional, para el visor (tkinter viene con la instalación habitual de Python):
python -m pip install pygame
```

---

## Uso Rápido

### 1. Iniciar el Visor Interactivo

Abrir el visor interactivo con el nivel editado más reciente:
```bash
python tools/gd_renderer/main.py
```

Abrir un nivel específico de tus niveles guardados en GD:
```bash
python tools/gd_renderer/main.py --level "gemini here"
python tools/gd_renderer/main.py --level "ZZZ layout"
```

Abrir un archivo de nivel (.gmd, .txt, .level):
```bash
python tools/gd_renderer/main.py --file "mi_nivel.gmd"
```

Listar todos los niveles locales guardados en tu juego:
```bash
python tools/gd_renderer/main.py --list
```

---

### 2. Controles en el Visor Interactivo

| Acción | Control |
| :--- | :--- |
| **Moverse por el nivel (Pan)** | Arrastrar con **Clic Derecho** o **Clic Central**, o teclas **WASD / Flechas** |
| **Zoom** | **Rueda del ratón** (centrado en cursor) o teclas **`+` / `-`** |
| **Resetear Cámara** | Tecla **`R`** |
| **Inspeccionar Objeto** | **Clic Izquierdo** sobre cualquier objeto del nivel |
| **Abrir Archivo (.gmd/.txt)** | Tecla **`O`** |
| **Menú de Niveles Locales** | Tecla **`M`** |
| **Exportar Captura PNG** | Tecla **`S`** o **`F12`** (captura vista actual) |
| **Exportar Nivel Completo** | Tecla **`Shift + S`** (renderiza los límites completos) |
| **Alternar Rejilla 30x30** | Tecla **`G`** |
| **Alternar Fondo (BG)** | Tecla **`B`** |
| **Alternar Suelo** | Tecla **`F`** |
| **Alternar Brillo (Glow)** | Tecla **`L`** |
| **Mostrar/Ocultar HUD** | Tecla **`H`** |

---

### 3. Modo Renderizado por Consola (CLI)

Exportar una imagen PNG en alta definición sin abrir la ventana:
```bash
python tools/gd_renderer/main.py --level "gemini here" --render --out render.png --scale 1.5 --grid
```

Parámetros opcionales:
- `--out`, `-o`: Ruta del archivo PNG de salida (por defecto: `level_render.png`).
- `--scale`, `-s`: Factor de escala de píxeles por unidad GD (1.0 = 30px por bloque, 2.0 = 60px por bloque).
- `--grid`: Incluir la rejilla 30x30 del editor.
- `--no-bg`: No renderizar el fondo (fondo transparente).
- `--no-ground`: No renderizar el suelo.
- `--no-glow`: Ocultar sprites de Glow.
- `--antialias 1|2|4`: Muestreo interno para contornos finos; por defecto `2`. `1` reduce el coste y `4` mejora el suavizado.
- `--resources RUTA`: Carpeta de recursos del juego, tanto para PNG como para el visor.
- `--preview-x X`: Vista aproximada de los triggers de color hasta X, aplicados de forma instantánea. Por defecto se conservan los colores iniciales del editor.
- `--quality`: Calidad de texturas: `uhd` (4K UHD original) o `hd`.
- `--x-start`, `--x-end`, `--y-bottom`, `--y-top`: Coordenadas de recorte personalizadas. Los límites omitidos se calculan con los sprites transformados y un margen de 60 unidades; la imagen completa ya no se corta a Y=600.

---

### 4. Uso como Módulo de Python

Puedes importar el renderizador en cualquier script:

```python
from tools.gd_renderer import LevelParser, GDRenderer

# 1. Cargar nivel (desde CCLocalLevels.dat o archivo)
level = LevelParser.load_local_level("gemini here")
# o: level = LevelParser.parse_file("path/to/level.gmd")

# 2. Inicializar renderizador
renderer = GDRenderer(quality="uhd")

# 3. Renderizar rango a una imagen PIL
bounds = level.get_bounds()
img = renderer.render_range(
    level=level,
    x_start=bounds[0] - 60,
    x_end=bounds[2] + 60,
    y_bottom=min(0.0, bounds[1] - 60),
    y_top=bounds[3] + 60,
    scale=1.0,
    draw_bg=True,
    draw_ground=True,
    draw_grid=True
)

# 4. Guardar
img.save("mi_render.png")
```


## Validación y alcance

```powershell
python tests/gd_renderer_regression.py
python tests/gd_renderer_regression.py --level "gemini here" --out build-regressions/gd-renderer
```

La segunda orden lee el guardado local sin modificarlo, comprueba que existen las
texturas y definiciones utilizadas, genera un PNG con fondo y otro transparente,
y verifica que renderizar un recorte coincide con esa región del render completo.
Las pruebas unitarias cubren transformaciones, capas, atlas rotados y offsets de
medio píxel, alfa, mezcla aditiva, copias HSV, recursos alternativos, CLI y la
coincidencia entre visor y exportación. La prueba del visor usa el controlador SDL
virtual; se omite si faltan pygame o tkinter.
El lector acepta también el terminador NUL de la cadena codificada que Cocos
puede incluir en los guardados XOR, manteniendo la validación del contenido Base64.

La revisión utiliza los bindings de GD **2.2081** de
`build-geode-5.11.2-probe/_deps/bindings-src/bindings/2.2081/GeometryDash.bro`
(`GameObject`, `ColorAction`, `GJBaseGameLayer`) y `Cocos2d.bro` (`CCNode`).
`bindings/include/Geode/Enums.hpp` confirma B5=-5 y las demás capas.
La composición de los ejes sigue la matriz de
[CCNode::nodeToParentTransform de Cocos2d-x 2.2](https://github.com/cocos2d/cocos2d-x/blob/cocos2d-x-2.2/cocos2dx/base_nodes/CCNode.cpp#L1072).

Este renderizador no ejecuta el juego: no simula animaciones, partículas emitidas,
shaders ni triggers de movimiento, spawn, pulse o transiciones temporales de color.
Un PNG transparente conserva el brillo con cobertura alfa; su composición sobre
otro fondo no puede reproducir todas las mezclas aditivas de OpenGL. Para conservar
la mezcla sobre el fondo del nivel, exporta con fondo.
