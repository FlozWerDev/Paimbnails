# Paimon Shop (local)

Abre **Paimon Hub → General → Paimon Shop**. La entrada aparece en los dos estilos
del hub. Esta version funciona en el equipo del usuario, sin peticiones al servidor.

- **Crear** captura los ajustes guardados de Custom Layout, Paimon RTX,
  Beat Shaders o un pack con los tres. Guarda primero los cambios de tu editor.
- Agrega nombre, descripcion y una captura **PNG de hasta 1 MB, 1920 × 1080**.
  El autor es el nombre actual del jugador; no hace falta iniciar sesion para
  usar la biblioteca local.
- La galeria muestra la imagen, el autor y la categoria. Tiene busqueda y paginas.
- Abre una tarjeta para ampliar la preview, revisar su contenido, aplicar,
  exportar o eliminar el preset.
- **Exportar** crea un JSON portable con la configuracion y la imagen incluida.
  **Importar** agrega ese archivo a la biblioteca sin cambiar los ajustes activos.
- **Aplicar** reemplaza solo los ajustes de esa categoria y guarda un respaldo.
  **Restaurar** recupera el estado anterior al ultimo preset aplicado. En un pack,
  un fallo de escritura intenta revertir las partes ya cambiadas.
- El layout se actualiza al regresar al menu principal. Se conservan los defaults
  y offsets propios de la instalacion. Se requiere tener las fuentes del layout.

Los datos viven dentro del directorio de guardado del mod:

```text
paimon-shop/
  presets/index.json       # Metadatos; las imagenes no se cargan al listar.
  presets/<id>.json        # Un preset y su PNG.
  previous-config.json     # Respaldo de los ajustes, consumido al restaurar.
```

Los archivos del Shop usan `format: "paimon-shop"`, `version: 1`, `title`,
`description`, `author`, `accountID`, `createdAt` (segundos Unix), `category`,
`configuration` y `preview` (PNG en base64). La categoria es `layout`, `rtx`,
`beat-shaders` o `pack`. La configuracion incluye exclusivamente los componentes
de esa categoria. RTX usa su esquema vigente (6). Se validan versiones, tamanos,
categorias, valores y cabeceras PNG antes de importar. La UI decodifica la imagen
antes de agregarla. Los IDs locales se generan dentro de la biblioteca.

`ShopLibrary` separa el almacenamiento de `ShopConfiguration` y de la interfaz.
La conexion futura al servidor puede transportar el mismo formato y mantener las
operaciones locales de importacion, exportacion y respaldo. El autor de un JSON
local es metadato y no constituye identidad verificada para publicarlo online.

Validacion sin abrir Geometry Dash:

```powershell
python tests/paimon_shop_regression.py
cmake --build build-clang --target Paimbnails --parallel 8
```

La regresion ejecuta el protocolo y el repositorio reales con ASan/UBSan.
Tambien ejecuta la orquestacion de configuraciones de produccion con dobles del
motor: rechazo si falla el respaldo, aplicacion por categoria, restauracion,
shaders no disponibles y rollback de packs cuando falla una parte. No
reemplaza la comprobacion visual y de controles dentro del juego.
