# Arranque, miniaturas y compatibilidad OpenGL — 25/10/2026

Se revisaron `Bootstrap`, los hooks de carga y menú, la precarga de niveles
oficiales, las colas de miniaturas, las configuraciones de cursor/mascota y el
inicio de las subidas de vídeo a la GPU.

No se recibió un registro del crash ni el modelo de CPU/GPU afectado. Los
defectos siguientes se reprodujeron con pruebas del código de producción; no
demuestran todavía que sean la causa del crash reportado por esos usuarios.

## 1. Configuración compartida durante el arranque

`deferredMenuServicesInit` iniciaba los tickers y después ejecutaba
`PetManager::loadConfig()` y `CursorManager::loadConfig()` en un worker.
Esas funciones modificaban los mismos booleanos, cadenas y conjuntos que los
tickers y los eventos de entrada leían en el hilo principal, sin sincronización.
El orden depende del tiempo de lectura del disco y del planificador de hilos;
no es un problema exclusivo de Intel.

Ahora el worker lee y parsea dos snapshots JSON. Los gestores reciben esos
snapshots y modifican su configuración únicamente en el callback del hilo
principal. Las operaciones de disco y el parseo siguen fuera del renderizado.
Las cargas síncronas existentes reutilizan la misma aplicación del JSON.

## 2. Funciones de OpenGL opcionales en Windows

El camino PBO comprobaba disponibilidad en Android y macOS, pero Windows
podía llegar a `glMapBufferRange` sin verificar su disponibilidad. Además,
las funciones de sincronización aceptaban direcciones inválidas distintas
de cero, y se reutilizaban después de cambiar de contexto.

Ahora se comprueban las capacidades de pixel buffers y mapeo por rangos, las
funciones requeridas y el contexto actual. Se recargan los entry points por
inicialización y se rechazan `nullptr`, `1`, `2`, `3` y `-1`. Si falta alguna
función requerida, `init` devuelve `false` y `VideoPlayer` usa su camino de
subida directa ya existente. La ausencia de fences mantiene el comportamiento
opcional anterior. La limpieza de errores GL se limita a 16 intentos para que
un error persistente no bloquee el arranque indefinidamente.

La documentación de [Khronos sobre carga de funciones](https://wikis.khronos.org/opengl/Load_OpenGL_Functions)
describe esos valores de error de WGL. La disponibilidad del mapeo se
comprueba mediante OpenGL 3.0 o `ARB_map_buffer_range`.

No se encontraron opciones globales que obliguen a usar AVX/AVX2 en
`CMakeLists.txt`. Una CPU Intel no identifica por sí sola la GPU ni el driver
OpenGL utilizado; falta el registro para distinguir esas causas.

## 3. Presupuesto de subida de miniaturas

Las miniaturas visibles tenían prioridad en la cola y además una excepción
que omitía el límite de tiempo. Por tanto, varias subidas lentas podían
acumularse en el mismo fotograma. Cuando el presupuesto ya estaba agotado,
otra invocación todavía podía iniciar una subida adicional.

Se conserva el orden de prioridad y se aplica el presupuesto a todas las
miniaturas. Si ya se agotó, el trabajo espera al siguiente drenaje de la cola.
Después de una primera subida lenta se aplazan las restantes, incluso si
esa primera subida falló. El cierre del runtime también impide nuevas subidas.

Esto limita el trabajo acumulado; una llamada individual al driver todavía
puede tardar más que el presupuesto. No se midió una mejora de FPS ni del
tiempo de arranque en el hardware de los usuarios afectados.

## Validación

Las pruebas usan las funciones actuales extraídas del código de producción
y dobles de los servicios del motor, compilados con ASan/UBSan:

- `menu_services_startup`: lectura/parseo en worker, mutación/aplicación en main,
  inicialización única y cancelación del callback al cerrar.
- `windows_gl_entrypoints`: valores de error WGL y reemplazo de contexto.
- `windows_pbo_startup`: inicialización RGBA/YUV, funciones o extensiones
  ausentes, contexto inválido, errores persistentes y recuperación.
- `thumbnail_upload_budget`: GPU lenta, presupuesto agotado, subidas fallidas,
  conservación de la cola y cierre.

Las pruebas iniciales fallaron al detectar la mutación de configuración fuera
del hilo principal, los punteros WGL inválidos y las subidas visibles sin límite.
Las cuatro pruebas pasan con las correcciones. También pasan las regresiones
de precarga de niveles oficiales y las guardas de compatibilidad de crashes.
La batería completa de callbacks terminó con **70 pruebas aprobadas y ningún
fallo**, incluyendo las cuatro pruebas nuevas, todas con ASan/UBSan.

Los cinco archivos `.cpp` modificados compilan con los argumentos Release del
proyecto y Geode 5.11.2. Hay advertencias previas por variables/parámetros sin
uso fuera de los bloques corregidos. También se enlazó correctamente el
conjunto de objetos del proyecto en `build-clang/startup-verified.dll`, sin
ejecutar los comandos de instalación posteriores al enlace.
La configuración completa de CMake se
detuvo porque `geode project check` intentó guardar su configuración global
en una ubicación sin permiso de escritura. No se instaló el mod en el juego.

Para cerrar la investigación de Intel se necesita un registro de Geode del
crash, el modelo de CPU, la GPU utilizada y la versión del driver. Conviene
comparar un arranque en frío con otro con caché y comprobar tanto el fondo
normal como un fondo de vídeo personalizado.
