# Imagen a objetos: Pintura

## Geometría y referencia

La revisión del 23 de octubre de 2026 usa el nivel local `gemini here` como
referencia de construcción: 3.588 de sus 3.614 objetos son bloques sólidos 211.
Sus bordes combinan bloques finos, rotación y rotaciones independientes de los
dos ejes. El conversor usa esas mismas transformaciones para los trazos y rellenos.
También ajusta discos, elipses y triángulos nativos cuando conservan la figura con
menos piezas. No usa coordenadas, colores ni nombres particulares de la imagen
de prueba para decidir la geometría.

En `Suave`, los contornos y las líneas se simplifican antes de ajustar los bloques.
Las cintas de espesor vertical constante pueden compartir remates verticales
mediante ejes independientes; los contornos también prueban remates inclinados.
La cobertura y las máscaras de otros colores limitan cada ajuste. Los rombos,
discos y triángulos simples pueden resolverse con una sola pieza. `Pixel` conserva
el empaquetado de celdas en bloques sin inclinar sus ejes.

El relleno estático también compite con franjas continuas en ambas orientaciones.
Una franja usa un bloque 211 con ejes independientes y, si cambia de ancho, un
triángulo nativo. Puede extenderse bajo colores que se dibujan después. Para
aproximaciones más grandes, las posiciones ocultas delimitan intervalos válidos
para una recta en lugar de obligar a reconstruir cada escalón del borde.

`GifSurfaceVectorizer` genera las superficies y ajusta elipses o cápsulas cuando
encajan. También busca el tramo continuo más largo que cumple los límites de
ambos bordes y prueba una pendiente compartida: si los dos corredores permiten
lados paralelos, un bloque con ejes independientes sustituye al bloque y su
triángulo. Los ajustes adaptativo y de tramos largos compiten por costo y calidad.
Los bordes visibles no retroceden más de 0,48 celdas, aunque el área tapada por
colores posteriores permita aproximaciones mayores. Esto evita exponer puntas
del relleno inferior al reducir el contorno que debería taparlas.
Después de ajustar los remates se vuelven a comprobar todas las filas visibles.
Un tramo vacío o que haya perdido esa cobertura se subdivide y se ajusta de nuevo.

`GifSurfaceOptimizer` compara las opciones con la fuente: compone la
cobertura en una rejilla común de 4 × 4 muestras y elige menos piezas dentro de
un margen acotado de error RGB. En fuentes transparentes conserva además nueve
muestras centrales por celda para impedir huecos nuevos. La optimización se
ejecuta después de las reparaciones; volver a reparar contra cada celda
reconstruiría los escalones. También beneficia al modo Libre. La protección de
alfa solo conserva muestras donde la fuente es visible: una pieza sobrante en
el fondo transparente ya no se convierte en cobertura obligatoria.

Cada región protege además su interior opaco: si una celda está rodeada por el
mismo color, exige cobertura de las 16 muestras. Repara las faltantes y cuenta
esas piezas antes de decidir si un candidato ahorra objetos. El fondo opaco ya
no puede ocultar un hueco de color en las métricas. Las uniones entre bandas y
entre bloque y triángulo prueban un solapamiento de 0,16 celdas, limitado por la
pendiente y por la silueta permitida, para cerrar costuras de las texturas nativas.
Las regiones anteriores y los espacios vacíos erosionados dos celdas están protegidos de invasiones:
una geometría inicial que las tapa se reconstruye antes de comparar costos.
La cobertura opaca distingue además la franja con antialias de la hipotenusa de
los triángulos 693/694. La textura UHD tiene alfa 95/255 en su diagonal; esos
puntos no se consideran relleno opaco del interior. Las reparaciones también
forman parte del costo de cada candidato.

Los remates ya no pueden extrapolar puntas lejos de la mancha. Cada lado y
vértice se comprueba a intervalos de media celda contra el área permitida, con
un límite de 0,65 celdas para bloques y triángulos. Un tramo que falla se divide
localmente y vuelve a ajustarse antes de recurrir a celdas rectas. Las curvas
nativas admiten una banda de antialias de hasta 1,25 celdas, además del control
de área y del error de la fuente; no generan vértices de unión puntiagudos.

Las cápsulas se afinan contra su contorno mediante una pérdida robusta. Las
regiones redondas compiten por separado: un fragmento irregular de la ropa del
mismo color deja de impedir recuperar un ojo con cuerpo y dos extremos redondos.

Los rellenos y contornos de un color quedan debajo del siguiente color. La poda
elimina piezas tapadas, pero conserva las líneas finas que aportan cobertura:
ya no impone un ancho o área mínimos. Las reparaciones de costuras se ejecutan
también para Pintura estática. Si toda la fuente es opaca, un bloque de fondo
evita que el filtrado de las texturas abra transparencias interiores.

La paleta se ajusta por distancia perceptual en OKLab con pesos de frecuencia y
semillas elegidas por el error pendiente. Esto permite recuperar acentos pequeños,
como rubor y reflejos, aunque la mayor parte del dibujo use colores parecidos.
Se siguen fusionando colores demasiado próximos y respetando el límite elegido.
En Pintura/Libre con muestreo Suave sin dithering se eliminan dientes aislados
entre tonos cercanos, con un límite de error respecto de la fuente. Las medianas
de cada canal reajustan finalmente la paleta para reducir el error RGB sin añadir
objetos. Los bordes de contraste alto y los trazos finos están protegidos.

## Resolución y ajustes

Para nuevas preferencias, la interfaz inicia Pintura con resolución `Auto` y
muestreo `Suave`. Se conservan las preferencias que el usuario ya tenía guardadas.
Desde 320, `+` activa Auto; `−` vuelve a 320.

Auto estima la pérdida de detalle al reducir la fuente y elige 320, 480 o hasta
680 píxeles en el lado mayor. El análisis tiene en cuenta el alfa y varios
fotogramas. La subida respeta el tamaño nativo de fuentes mayores que 320;
las imágenes pequeñas o planas conservan la base de 320. La estimación ahora
detecta también detalles que ocupan una fracción moderada de la imagen.

El presupuesto de objetos y los límites de animación siguen aplicándose. Si el
dibujo no cabe, el pipeline puede reducir la resolución y mostrar `ajustado`.
La fidelidad depende de la resolución, la paleta, el presupuesto y la complejidad
de la fuente; la conversión sigue siendo una aproximación geométrica.

## Exportación y render de comprobación

`GifPrimitiveSerializer.hpp` serializa los objetos tanto para el editor como para
el banco de pruebas. Usa los centros y dimensiones del plan sin desplazar media
celda ni ampliar todas las piezas. Exporta las rotaciones independientes en las
propiedades GD 131 y 132. La marca de agua conserva la cobertura al dividir una
pieza inclinada.

La vista previa puede componer toda la geometría en una rejilla común de doble
resolución y reducirla una sola vez; así no crea costuras de antialias entre
rectángulos opacos contiguos. La validación visual independiente vuelve a leer
los objetos GD exportados y los renderiza en Python con las texturas reales del
juego y antialias de cuatro muestras por eje.

## Pruebas reproducibles

Las tres suites C++ usan el código de producción:

- `gif_import_regression.cpp`: cobertura, capas, presupuesto, animación, modos y progreso.
- `paint_circle_regression.cpp`: curvas, discos, fragmentos, contactos, remates, protección de colores y Auto.
- `image_to_editor_regression.cpp`: veintiún casos para acentos pequeños, ejes independientes, posición nativa, líneas finas, antialias, marca de agua, paletas con entradas sin usar, costo de superficies, picos, regiones redondas, interiores opacos, cierre de huecos, lados paralelos, piezas sobrantes en transparencia, invasiones y franjas de las texturas nativas.

Están incluidas en `tests/run_regressions.py --suite portable --sanitize`. Los
casos de codos comprueban cobertura y picos sin exigir una clase concreta de
remate: las cintas pueden unirse sin añadir discos.

Para pasar una imagen real por el conversor y volver a renderizar su nivel:

```powershell
python tests/image_to_editor_visual_regression.py --image "C:\Users\paimon\Downloads\HVSGRvyb0AEajgS.jpg" --reference-level "gemini here"
```

Requiere Clang++, Pillow, NumPy y los recursos locales de Geometry Dash. El nivel
de referencia es opcional. `--bench` admite un banco ya compilado y `--out` cambia
la carpeta de resultados. `--baseline-level` compara con una exportación anterior
y su PNG `-grid` para conocer la escala. La comparación exige mejorar el error
RGB, respeta el presupuesto y comprueba texturas, objetos y transparencias
interiores de las fuentes opacas.

Con `--baseline-level anterior.level --require-fewer-objects` también exige bajar
el conteo de objetos, además de mejorar el error RGB. El informe registra ambos
porcentajes, la calidad de bordes y PSNR para no confundir más resolución con
una mejora de eficiencia.

`--require-clean-interiors` exige cero píxeles interiores con color incorrecto en
el render nativo: regiones erosionadas dos celdas, diferencia máxima de canal
superior a 16 o alfa inferior a 250. Excluye dos píxeles del marco del lienzo,
que se filtra contra transparencia. Este control detecta huecos rellenos por el
fondo que la comprobación de alfa no podía distinguir. Dos pruebas Python
comprueban ese caso y evitan marcar los bordes con antialias como defectos.

`--detail-region 170,360,245,460` genera una comparación ampliada de esa zona de
la fuente. Se puede repetir la opción. Cada recorte vuelve a renderizar objetos
a escala 4; no amplía el PNG del render general.
`--background auto` prueba la eliminación automática del fondo por el mismo
pipeline de producción; la referencia se enmascara con el alfa de la rejilla.
La opción predeterminada `keep` conserva el fondo.

Se generan la exportación `.level`, un `.gmd` con compresión de datos, la paleta,
el render nativo, `comparison.png`, `conversion.log` y `report.json` con error RGB,
PSNR, error de bordes y conteos. El `.gmd` se vuelve a leer para verificar objetos
y colores. El proceso lee los guardados locales sin modificarlos.

El ensayo con la imagen adjunta usa 24 colores solicitados, Auto y un presupuesto
de 12.000 objetos y fondo conservado. La comparación mantiene 680 × 680 y 18 colores
en ambas versiones:

| Medida | Versión anterior | Interiores y contornos optimizados |
| --- | ---: | ---: |
| Objetos | 5.128 | 4.542 |
| Error RGB medio / 255 | 3,4555 | 3,2464 |
| Error de bordes | 2,6016 | 2,4533 |
| PSNR | 29,5473 dB | 30,5805 dB |
| Transparencias interiores | 0 | 0 |
| Píxeles interiores de color incorrecto | 491 | 0 |

El costo baja otro 11,4 %, el error RGB un 6,0 % y el error de bordes un 5,7 %.
PSNR sube 1,03 dB. Las métricas globales se acompañan de recortes ampliados del
aro, el moño, el pelo y la estrella. El nivel usa 3.586 bloques 211, 947
triángulos 693 y nueve círculos 3637. Los archivos quedan en
`build-regressions/image-to-editor/verified-final/`; la base conservada está
en `no-spikes-verified/`. Frente a los 10.557 objetos iniciales de
`efficiency-before/`, la reducción acumulada es del 57,0 %.

La misma fuente también pasa con eliminación automática del fondo: 680 × 680,
19 colores finales y 6.596 objetos, con cero defectos interiores detectados. El
informe queda en `auto-protected-verified/`. Esa variante conserva otra geometría
de transparencia y no se compara en costo contra la base con fondo conservado.
Su recorte incluye antialias contra transparencia en el contorno; el control de
interiores distingue ese filtrado de un hueco dentro del dibujo.

La regresión de una superficie inclinada con un detalle superpuesto baja de 96
a siete objetos, incluida la marca de agua y la base opaca, sin cambiar los
centros de píxel. Otra prueba recupera una cápsula inclinada y una elipse con
cuatro formas y menos de un 1 % de diferencia en los centros muestreados.
El caso adversarial de dos filas comprueba que los remates no extrapolen puntas
de cinco celdas: el saliente máximo muestreado a escala 8 no supera 0,5625 celdas,
con dos objetos y cobertura central completa también en los ajustes aproximados. La prueba de
regiones mixtas conserva el fragmento irregular y recupera la cápsula: 50 piezas
rectas pasan a 21 objetos, incluidos sus dos círculos.
La prueba de huecos opacos pasa de 120 centros sin el color correcto a cero y
reduce tres piezas a dos. El contorno ondulado conserva todos los centros con
29 piezas frente a 69 del ajuste adaptativo. Otra prueba permite tapar el lado
derecho de una cinta inclinada: el ajuste paralelo pasa de dos piezas a un bloque
211 sin perder cobertura visible.
La regresión de transparencia elimina dos piezas que sobran fuera del dibujo
sin recortar su región visible. Otra comprueba que una capa posterior no tape el
interior de la anterior, incluso si ambas estaban representadas por un solo
bloque. El caso de dos triángulos que completan un rectángulo tiene en cuenta sus
diagonales semitransparentes y termina con un único bloque sólido.

Estos resultados corresponden a esa configuración y al render nativo; no son una
captura del juego. Las tres suites C++ pasan con ASan/UBSan, las 37 pruebas Python
del render pasan y la compilación Release completa genera e instala el paquete
local del mod. Queda la comprobación manual dentro de GD al reiniciar el juego.

## Contornos continuos — 30 de octubre de 2026

En Pintura/Libre estático con muestreo Suave y optimización, un candidato nuevo
filtra los bordes de las bandas antes de simplificarlos. Las bandas comparten
las posiciones de unión, en lugar de ajustar cada extremo por separado. Un
remate corto e inclinado que no supera el control de silueta se reajusta
localmente, sin descartar el resto del contorno. El candidato conserva las
elipses y cápsulas de regiones completas; evita aproximar manchas irregulares
con discos pequeños, que pueden dejar un borde ondulado.

La evaluación de error ahora se limita a cada región y una franja de dos celdas
alrededor. El fondo y las regiones lejanas ya no aportan margen para deteriorar
un detalle pequeño. Las curvas continuas pueden usar más piezas si reducen el
error RGB o el error de las transiciones respecto de la fuente, dentro del
margen de fidelidad existente. El límite por color es tres veces el coste del
ajuste barato más ocho piezas; el presupuesto global del importador sigue
aplicándose. Las reparaciones de interior cuentan en ese coste. Pixel mantiene
su ruta exacta.

La imagen adjunta de esta prueba es `HVppPagbkAAsl0n.jpg`, **768 × 768**, SHA-256
`EBDE22A1DF007CC0AD07CAEB529E7BF05192993A8C363193C87AB8E5733BC3DB`.
Ambas conversiones usan la misma fuente, Auto, 24 colores solicitados,
presupuesto 12.000, fondo conservado, rejilla 680 × 680 y 20 colores finales.
La comparación vuelve a leer los objetos y usa las texturas de GD, con cuatro
muestras por eje, a 768 × 768.

| Medida | Antes | Contornos continuos |
| --- | ---: | ---: |
| Objetos | 2041 | 4021 |
| Error RGB medio | 3,92997 | 3,75817 |
| Error de bordes | 2,41853 | 2,22289 |
| PSNR | 27,7532 dB | 28,5705 dB |
| Transparencias interiores | 0 | 0 |
| Píxeles interiores de color incorrecto | 0 | 0 |

El error de bordes baja un 8,09 % y el RGB un 4,37 %. El coste aumenta un
97,01 %: esta modificación prioriza la continuidad de las curvas. No implica
que todos los bordes de cualquier fuente queden perfectos. La paleta y las
esquinas presentes en el original también influyen en el resultado.

En la curva sintética de regresión, el mayor salto entre bandas pasa de
1,09375 a 0,03125 celdas, con 23 piezas frente a 73. Añadir margen al lienzo
dejaba cambiar 32 muestras de un detalle idéntico; ahora no cambia ninguna.
También se comprueban la mezcla de curvas y esquinas y un remate inclinado que
antes perdía toda su geometría en una orientación. Las tres suites C++ del
importador pasan con ASan/UBSan y advertencias habilitadas.

Resultados y recortes de cara, lazo y pelo:
[comparación](../build-regressions/image-to-editor/continuous-curves/verified/comparison.png),
[métricas](../build-regressions/image-to-editor/continuous-curves/verified/report.json) y
[nivel para importar](../build-regressions/image-to-editor/continuous-curves/verified/HVppPagbkAAsl0n.gmd).

## Trazos girados y uniones circulares — 30 de octubre de 2026

La siguiente revisión añade otro candidato al convertidor principal estático
Pintura/Libre, Suave y con optimización. Sigue el eje de los trazos, mide su
anchura sobre la fuente y emite rectángulos 211 con rotación convencional y
círculos 3637 en las uniones. Los rectángulos terminan en el eje de la unión:
no prolongan sus esquinas cuadradas por fuera del disco. El filtro del eje
reduce la oscilación de las escaleras digitales antes de simplificarlo.
El resto de superficies puede seguir usando bandas, bloques y triángulos;
no se fuerza una representación circular cuando empeora la fuente.

La estimación de anchura incluye la franja antialias de la imagen original.
Cada pieza se comprueba con 4 × 4 muestras por celda para impedir salientes
profundos sobre otras regiones. El control de interiores también distingue
el borde filtrado de la textura circular de su zona completamente opaca,
tanto en CPU como en GPU. La eliminación de atajos diagonales falsos en el
esqueleto se aplica al nuevo candidato; la ruta previa conserva su conectividad.

La simplificación de bandas mide la desviación perpendicular al borde. Las
bandas cuya variación total de anchura no supera 0,6 celdas pueden usar una
sola pieza paralela: conservan el eje y la anchura media, desplazando cada
extremo del borde como máximo 0,15 celdas. Todos los candidatos siguen sujetos
al control local de fidelidad, reparaciones y presupuesto. Esto compensa el
coste de los discos, sin presentar una reducción sustancial de objetos.

Se compara la misma imagen adjunta de 768 × 768 y la misma configuración
descrita arriba, contra la revisión de 4021 objetos:

| Medida | Revisión anterior | Trazos redondeados |
| --- | ---: | ---: |
| Objetos | 4021 | 4018 |
| Círculos 3637 | 8 | 500 |
| Rectángulos 211 con rotación convencional no axial | 1 | 483 |
| Triángulos 693 | 1530 | 1072 |
| Error RGB medio | 3,758169 | 3,754751 |
| Error de bordes | 2,222894 | 2,193656 |
| PSNR | 28,570486 dB | 28,558514 dB |
| Transparencias interiores | 0 | 0 |
| Píxeles interiores de color incorrecto | 0 | 0 |

Hay 419 círculos en el color del contorno azul exterior. El error de bordes
baja un 1,315 %; el RGB baja un 0,091 %. El coste queda prácticamente igual
(tres objetos menos, un 0,075 %). El PSNR empeora ligeramente: la mejora de
borde no supone mejorar todas las métricas ni eliminar toda irregularidad
de la paleta o del raster original. El render conserva filtrado en el límite
del lienzo; esos píxeles no se cuentan como huecos interiores.

La curva de regresión usa 58 objetos, incluidos 34 trazos girados y 19 círculos.
La curva inclinada conserva su cobertura con cinco piezas frente a once.
También pasan las pruebas de continuidad digital, bifurcaciones diagonales,
anchura con antialias y opacidad del borde circular. Las tres suites C++ pasan
con ASan/UBSan y advertencias habilitadas. La prueba real de DirectCompute
confirma igualdad de cobertura y del plan completo entre CPU y GPU.

Estos resultados usan las texturas nativas de GD y no son una captura del juego.
Se revisaron recortes ampliados de cara, lazo y pelo. Resultados reproducibles:
[comparación](../build-regressions/image-to-editor/curve-efficiency/verified/comparison.png),
[lazo ampliado](../build-regressions/image-to-editor/curve-efficiency/verified/detail-2.png),
[métricas](../build-regressions/image-to-editor/curve-efficiency/verified/report.json) y
[nivel para importar](../build-regressions/image-to-editor/curve-efficiency/verified/HVppPagbkAAsl0n.gmd).

La compilación del mod quedó bloqueada antes de compilar el código: el
[manifiesto](../mod.json) declara GD 2.2091, mientras el SDK Geode local 5.11.2
y sus bindings están configurados para 2.2081. Esta revisión no se ha empaquetado
ni instalado. El paquete generado e instalado sigue siendo el anterior,
SHA-256 `EFE1D88F6AFADFA52E0BC29E221B34FCC43CE560E5A20AE71972F5B6280416F8`.
