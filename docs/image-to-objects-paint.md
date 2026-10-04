# Pintura: curvas y fragmentos

La fusión de fragmentos acepta bloques, trazos y círculos pequeños. Prueba la orientación principal, la pendiente de los escalones y las rotaciones existentes para sustituir grupos por rectángulos rotados o elipses. Un grupo puede incluir piezas de varios píxeles y pendientes distintas de 45 grados.

Cada sustitución conserva los centros pintados, los contactos de las líneas finas y la cobertura interior muestreada a escala 8. Los círculos deben caber en su propio color, porque GD puede dibujarlos sobre rectángulos de otra hoja de sprites. Los detalles aislados y el fondo de reparación se conservan.

La búsqueda usa sectores de cuatro celdas y limita vecinos, tamaño del grupo y pasadas. La misma fusión se aplica al generar la geometría y, para imágenes estáticas en modo Pintura, después de reparar las costuras.

## Resolución y Auto

El valor inicial es 320 píxeles en el lado más largo. Se conservan los ajustes guardados de resolución. En modo Pintura, pulsar `+` después de 320 activa `Auto`; pulsar `−` desde Auto vuelve a 320. La selección se guarda entre sesiones.

Auto estima la pérdida de detalle al reducir la fuente y elige 320, 480 o hasta 680. Compara variaciones de color premultiplicado y de alfa, ignora textura de poco contraste y revisa el primer fotograma, el intermedio y el último. El incremento respeta la resolución nativa de las fuentes que superan 320; una fuente de 290 sigue usando la base de 320.

La fuente se conserva con resolución suficiente antes del análisis, tanto en la vista previa como al importar en segundo plano. El presupuesto de objetos y los límites de animación siguen aplicándose: si no cabe el dibujo, el plan puede reducir la resolución y mostrar `ajustado`.

## Validación sin compilar

Las cuatro imágenes se recalcularon desde las fuentes a **320 × 320** en un prototipo numérico Python. La paleta perceptual, la votación de color y la limpieza de manchas tenues se adaptaron del conversor, con un máximo de 24 colores. Los PNG de 320 son el resultado del nuevo cálculo; las vistas ampliadas usan esa misma geometría.

La base de comparación empaqueta filas iguales en rectángulos. Estos conteos corresponden al prototipo; el pipeline C++ también usa trazado de contornos, otras formas y reparación de costuras. El prototipo conserva el fondo completo y no aplica el presupuesto de objetos del mod.

| Imagen | Colores | Piezas iniciales | Después de fusionar | Auto estimado |
| --- | ---: | ---: | ---: | ---: |
| `4d3109fee0f39a47873b4ada69b9aa10.jpg` | 15 | 3818 | 3538 | 320 |
| `9dc0fc3231c31b069fa0e70a874bdf5b.jpg` | 18 | 9824 | 7912 | 680 |
| `19b783638a5ac1eac49d6bd1845d1039.jpg` | 21 | 6477 | 5464 | 320 |
| `651f9d90fb5742589c5eb17bc7180d8e.jpg` | 19 | 1673 | 1621 | 320 |

En las cuatro pruebas de 320 se conservaron todos los centros visibles de la cuadrícula limpia. Las pruebas geométricas sintéticas comprobaron pendientes suaves y pronunciadas, curvas finas conectadas, parches redondos y puntos separados. Una auditoría previa a 64 también comprobó las muestras interiores y la protección de centros ajenos.

El modelo numérico de Auto comprobó fuentes pequeñas y planas, detalles de alto contraste, colores ocultos bajo alfa cero, detalle en un fotograma intermedio y el límite nativo de una fuente de 400. En las imágenes proporcionadas elige 680 solo para la segunda.

La lámina está en `/home/fernando/Descargas/previsualizacion-pintura-320.png`. Los PNG individuales, las vistas ampliadas y los conteos están en `/home/fernando/Descargas/previsualizacion-pintura-320/`.

`tests/paint_circle_regression.cpp` incluye seis regresiones C++ adicionales de fusión y cuatro de resolución automática. La revisión mediante un parser C++ no añadió errores respecto a los archivos iniciales y pasó `git diff --check`.

Las regresiones C++ y la comprobación en GD quedan pendientes. No se ejecutaron compilaciones, siguiendo `AGENTS.md`.

## Uniones y remates suaves — 2026-10-04

El trazado suave filtra el eje antes de simplificarlo y usa una tolerancia menor. Los bucles conservan la continuidad en su cierre. Los giros de más de 12 grados pueden unir los rectángulos con un círculo del grosor del trazo; los tramos rectos conservan su unión directa. La construcción se basa en los [remates y uniones redondos de SVG 2](https://www.w3.org/TR/SVG2/painting.html#StrokeShape).

Después de fusionar fragmentos, los extremos expuestos de trazos de hasta 32 celdas se prueban con remates circulares o elípticos. El rectángulo se acorta y el remate queda dentro de su extensión original. Cada sustitución conserva los centros y contactos de las líneas y las muestras interiores a escala 8. Los extremos cubiertos por otros objetos se conservan. Los círculos protegen nueve muestras centrales de cada celda de color ajeno; los círculos de contorno que crecen al fusionarse también vuelven a comprobar su frontera.

### Comprobación numérica sin compilar

Se descargaron tres dibujos de Wikimedia y se rasterizaron sus SVG antes de reducirlos a un lado máximo de 320. Las imágenes y el script reproducible están en `/home/fernando/Descargas/pintura-curvas-anime/`:

- [Animegirl, de j4p4n](https://commons.wikimedia.org/wiki/File:Animegirl.svg), CC0.
- [Wikipe-tan face, de Kasuga y Actam](https://commons.wikimedia.org/wiki/File:Wikipe-tan_face.svg), CC BY-SA 3.0.
- [Manga kid head, de El_Sato](https://commons.wikimedia.org/wiki/File:El_Sato_Manga_kid_head_(1).svg), CC0.

El modelo Python prueba ejes de líneas finas, simplificación y remates. Usa una paleta median cut de 24 colores, esqueletización de scikit-image y rectángulos por filas para rellenar áreas. No reproduce el ajuste completo de contornos, la fusión de fragmentos, la reparación de costuras, el presupuesto ni las máscaras de sprites del pipeline C++. Las comparativas son resultados del modelo y no capturas del mod.

| Dibujo | Resolución | Objetos del modelo previo | Objetos del modelo ajustado | Centros con otro color: previo → ajustado |
| --- | ---: | ---: | ---: | ---: |
| Animegirl | 107 × 320 | 5764 | 6574 | 450 → 376 |
| Wikipe-tan face | 320 × 320 | 13156 | 16919 | 1005 → 568 |
| Manga kid head | 284 × 320 | 12358 | 15153 | 675 → 468 |

Las comparativas siguen mostrando costuras: el modelo no ejecuta la reparación del mod. El aumento de objetos del modelo es de un 14–29 %; el conteo real y el efecto del presupuesto requieren ejecutar el pipeline C++.

La prueba aislada de remates pasó 77 combinaciones de 11 orientaciones y siete grosores. En 37 casos aceptó redondear; en todos conservó los centros y las muestras protegidas, no añadió cobertura fuera del rectángulo original a escala 8 y no añadió remates al repetir la operación. `remates-ampliados.png` muestra dos ejemplos sin deformar su proporción.

Se añadieron cuatro regresiones C++: cobertura y contactos en esas 77 combinaciones, uniones de giro suave, extremos ocultos y protección de muestras de primer plano. El parser C++ no añadió errores respecto al archivo inicial y `git diff --check` pasó. No se ejecutaron compilaciones ni estas regresiones C++, siguiendo `AGENTS.md`; falta comprobar el resultado importado en GD.
