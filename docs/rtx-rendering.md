# Paimon RTX: estabilidad del renderizado

RTX conserva una captura de la imagen a resolucion completa y calcula la iluminacion
en buffers reducidos. La imagen original mantiene su resolucion durante la composicion.

## Problemas corregidos

- La creacion de targets dejaba conectado un framebuffer interno antes de capturar
  la escena. La captura vuelve a conectar expresamente el framebuffer del juego y
  selecciona la unidad de textura adecuada, incluso despues de redimensionar.
- El primer fotograma podia mezclar color de un historial invalido. La validez se
  aplica tanto al color como a la varianza; tambien funciona en menus sin camara.
- Saltar fotogramas reutilizaba luz en coordenadas viejas. Tanto la composicion como
  el bloom reproyectan la luz con la transformacion actual de la camara.
- La reproyeccion suponia escala uniforme y ausencia de rotacion. Usa la matriz
  afin completa y rechaza transformaciones singulares, cambios de escena, viewport,
  configuracion del trazado, reactivaciones y pausas largas entre fotogramas.
- Una guia de color anterior permite rechazar historial cuando cambian objetos,
  fondos o interfaces. El reescalado compara esa guia con la escena actual y usa
  iluminacion neutra cuando no encuentra una coincidencia suficiente.
- Normalizar las direcciones en UV deformaba el alcance horizontal y vertical.
  Los rayos avanzan en proporciones de pixel y la distancia se expresa como
  fraccion de la altura. La resolucion reducida conserva la proporcion de pantalla.
- Los pasos geometricos tienen una alternativa lineal para crecimiento igual a
  uno. La densidad GGX se cancela antes de evaluar los reflejos para evitar picos
  numericos; los reflejos funcionan aunque la intensidad de luz rebotada sea cero.
- Los buffers de trazado e historial preservan valores HDR cuando la GPU los
  soporta. En GLES2 se prueban texturas half-float con filtrado lineal; la captura
  RGB admite framebuffers opacos. Sin soporte HDR se mantienen buffers de 8 bits.
- El filtrado mantiene un minimo de proteccion de bordes. La aberracion cromatica
  desplaza hasta cuatro pixeles por canal con el control al maximo, y los rayos
  volumetricos usan ruido espacial estable sin discontinuidades angulares.
- Con efectos desactivados y controles de color neutros, las curvas conservan el
  color de origen, incluidos los blancos. Intensidad cero evita tambien el dither.
- El estado OpenGL se restaura al salir, incluidos framebuffer de lectura/escritura,
  viewport, programa, texturas, atributos, buffers, VAO, mascara de color y pruebas
  de mezcla, recorte, profundidad, stencil y caras. Las llamadas internas usan GL
  directamente para conservar la coherencia de las caches de Cocos.

## Verificacion sin compilacion

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/rtx_render_regression.py
```

El script comprueba conexiones entre shaders y renderer, 500 transformaciones de
camara, curvas numericas para los 256 niveles de cada canal, proporciones de pantalla
y limites de los rayos. Tambien usa EGL/GLES para verificar una copia RGB con origen
de viewport distinto de cero y valores superiores a uno en targets HDR. No compila
C++ ni shaders. Con `--static-only` omite las comprobaciones EGL.

Estas pruebas no ejecutan el renderer C++ dentro de Geometry Dash ni validan el aspecto
visual de los shaders en las GPU de los usuarios. Queda pendiente la comprobacion
en juego con cambios de ventana/pantalla completa, presets, fotogramas saltados,
rotacion y zoom, reinicio del nivel, pausa, menus y recarga del contexto GL.

La reproyeccion se estima a partir de la camara y del color de la imagen: RTX no
dispone de profundidad real ni vectores de movimiento por objeto. Los objetos que
se mueven independientemente pueden requerir iluminacion nueva en el siguiente
fotograma de trazado.
