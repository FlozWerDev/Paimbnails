# Correcciones de la revisión del 16 de octubre de 2026

Se corrigieron los ocho hallazgos de [la revisión](REVISION_CODIGO_2026-10-16.md).

| Hallazgo | Cambio |
| --- | --- |
| Vida útil del manifiesto | `getEntry()` devuelve una copia opcional bajo mutex. Las operaciones del índice se separaron de la serialización en `DiskManifestIndex.cpp` y se prueban directamente con concurrencia. |
| Audio pendiente | Cada solicitud conserva su generación, cuenta y sesión original. Configuración, descarga, extracción y reproducción comprueban su validez. Cerrar el perfil cancela incluso si todavía no comenzó a reproducirse. Cada página limpia solamente su propia sesión. |
| Guardado de proyectos | Iconos, estilos, slots, índices, manifiesto y descargas de miniaturas usan el escritor atómico existente. Se propagan errores de índice y se recuperan proyectos válidos que hayan quedado sin registrar tras un fallo parcial. |
| GIF entrelazados | El decodificador salta las pasadas que no tienen filas. También inicializa la salida inválida y acepta de forma segura un puntero nulo. |
| Presupuesto de memoria | El límite conjunto se calcula antes de decidir la expulsión. Se coordinan las cachés de niveles, URLs y GIF con un orden fijo de locks y resta saturada. Los recursos fijados conservan su excepción; no se promete un límite duro sobre memoria en uso por escenas. |
| Pruebas/CI | Nuevo ejecutor portable, objetivo `check`, CTest y paso de CI previo al empaquetado. Se repararon rutas de bindings, inventarios, ejecutables Windows y fixtures de emotes. Las dependencias opcionales se informan por separado. |
| PaiDraw | Los snapshots actualizan el estado completo; los eventos de trazos se reservan para paquetes incrementales. Se eliminó la lectura del vector movido y se agrupan las actualizaciones de sala/ronda para reconstruir el canvas una vez por frame. |
| Caducidad del disco | La purga evalúa la edad aunque el tamaño total esté bajo la cuota. Los niveles principales fijados siguen protegidos. |

También se exportan los comandos de compilación y se ofrece `PAIMBNAILS_EXTRA_WARNINGS` para activar advertencias adicionales durante mantenimiento sin cambiar por defecto los diagnósticos de dependencias externas.

## Verificación

- Suite portable: **26 comprobaciones aprobadas**, incluidas compilación y ejecución de nueve regresiones C++ con ASan/UBSan.
- Suite de bindings: **3 auditorías aprobadas**, incluida la auditoría general del SDK y el inventario de 380 candidatos de interfaz.
- CTest: integración de la suite portable aprobada.
- Compilación Windows con Clang y Geode 5.11.1: genera el `.dll` y el `.geode`, y lo instala automáticamente en el perfil configurado de Geometry Dash.
- Los logs se conservan en `build-clang/audit-fixes-validation/` y `build-clang/regression-results/`.

Las pruebas nuevas llaman al índice y al decodificador de producción: conservan una entrada del manifiesto mientras otro hilo la elimina, ejercitan lecturas/escrituras concurrentes, verifican caducidad y cuotas, comparan GIF entrelazados y normales en 34 tamaños, rechazan respuestas de sesiones antiguas y fuerzan fallos de reemplazo de archivos sin perder su contenido anterior.

## Pruebas manuales pendientes

1. Abrir un perfil sin música cacheada y cerrarlo antes de que termine la carga. Repetir abriendo otro perfil y reabriendo la misma cuenta. La respuesta anterior no debe iniciar ni detener el audio de la sesión nueva.
2. Crear, editar y recargar un icono y un slot de Texture Studio. Comprobar que un error de escritura se muestra y conserva el proyecto anterior. La recuperación de índices solo añade proyectos válidos ausentes; no elimina sus archivos.
3. Navegar por muchas miniaturas/GIF mientras se limpia o invalida la caché. Los niveles fijados deben mantenerse y las descargas deben terminar sin bloqueos.
4. Probar snapshots y trazos incrementales de PaiDraw durante un cambio de ronda y una reconexión. El dibujo no debe duplicarse ni perderse.
5. Volver a comprobar entrada/salida del editor, PauseLayer y posición de estadísticas en el garaje con los mods habituales.

La validación realizada fue de compilación, análisis estático y pruebas independientes. La integración visual dentro del juego y la ejecución en Android/macOS no se simulan como verificadas. El servidor privado, Playwright/Chromium y el renderizador GPU de Linux no están disponibles en este entorno y se registran como `SKIP` en la suite opcional.
