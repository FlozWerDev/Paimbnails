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
