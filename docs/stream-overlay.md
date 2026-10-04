# Overlay de requests para OBS

Activa **Overlay para OBS** en el mod y copia su URL en una **Fuente de navegador** de OBS de 1920×1080. El servidor escucha en `localhost:21680` mientras el juego está abierto; está disponible en Windows y macOS.

- `/overlay`: fuente transparente con los datos del directo.
- `/preview`: vista previa; muestra una demo cuando no hay partida ni requests.
- `/gallery`: galería de los 19 estilos con enlaces individuales para copiar.

Cada fuente puede elegir su estilo, diseño, animación y escala sin cambiar los ajustes generales:

```text
http://localhost:21680/overlay?style=gd&layout=ticker&anim=bounce&scale=0.8
```

| Opción | Valores |
| --- | --- |
| `style` | `glass`, `gd`, `neon`, `synthwave`, `arcade`, `terminal`, `minimal`, `comic`, `glitch`, `holo`, `aurora`, `inferno`, `frost`, `galaxy`, `royal`, `pastel`, `brutal`, `cozy`, `esports` |
| `layout` | `cards`, `compact`, `ticker`, `sidebar`, `spotlight`, `corner`, `banner` |
| `anim` | `flow`, `slide`, `pulse`, `none`, `bounce`, `flip`, `zoom`, `glitch`, `drop`, `blur`, `typewriter` |
| `scale` | De `0.3` a `2` en la URL |

Los controles del mod permiten elegir de 1 a 8 próximos niveles, personalizar colores, opacidad y bordes, y mostrar u ocultar autor, ID, solicitante, plataforma, dificultad, progreso, intentos, estadísticas, alertas y partículas. Los diseños de esquina y foco muestran solo el nivel actual; el banner muestra hasta tres próximos niveles.

El estilo GD utiliza fuentes y sprites de la instalación del juego. Si no están disponibles, conserva texto e indicadores de dificultad alternativos. Las fuentes bitmap y los marcos originales de GD conservan sus colores propios. Las celebraciones se omiten en práctica y en niveles de plataformas. El sonido es opcional; para gestionarlo en el mezclador, activa **Controlar audio vía OBS** en la fuente.

## Prueba del navegador sin compilar el mod

`tests/stream_overlay_regression.mjs` sirve los archivos locales con datos simulados y comprueba las 133 combinaciones de estilo/diseño, las 11 animaciones, controles, alertas y enlaces de la galería. Requiere Playwright o Playwright Core y Chromium ya instalados:

```sh
PLAYWRIGHT_MODULE=/ruta/a/node_modules/playwright-core \
CHROMIUM_PATH=/ruta/a/chrome \
GD_RESOURCES='/ruta/a/Geometry Dash/Resources' \
node tests/stream_overlay_regression.mjs
```

`GD_RESOURCES` es opcional y habilita la comprobación de fuentes y sprites reales. Esta prueba no inicia el juego ni verifica el servidor C++ dentro de Geode.
