# Blur y Vert en imagen a objetos

En el mismo popup, el boton **Modo** incluye **Blur** y **Vert** despues de Circulos.

- **Blur** filtra la fuente con un shader gaussiano separable antes de generar la paleta y construirla con glow circular. El boton de Glow pasa a controlar el filtro: fino, suave, alto o desactivado. Desactivar el filtro conserva la construccion con glows.
- **Vert** reconstruye las filas con pares de degradados verticales nativos enfrentados. Los tramos horizontales del mismo color comparten objetos. Este modo usa decoraciones de degradado, no triggers Gradient con vertices anclados a grupos.
- **Base: negra / Base: nivel** permite conservar los tonos oscuros o mezclar la luz con el escenario. La base negra cuesta un objeto y un canal adicionales, incluidos en el presupuesto.

Los ajustes se guardan. La vista previa utiliza las mascaras alfa de los objetos seleccionados, con mezcla aditiva y compensacion del brillo de los glows. GIF y video conservan el planificador de frames y triggers; el presupuesto reduce la resolucion cuando hace falta.

## Recursos nativos

Los bindings `GeometryDash.bro` exponen `ObjectToolbox::m_allKeys`, `intKeyToFrame` y `GameObject::createWithKey`, pero no una lista fija de IDs para cada textura. La seleccion consulta el catalogo del juego en el hilo GL y compara mascaras de transparencia completas. Los workers reciben una copia inmutable.

Si el catalogo no contiene un glow circular completo adecuado, se forma con cuatro cuartos de glow nativos orientados alrededor del centro. En los recursos locales, `d_gradient_02_001.png` coincide con el cuarto radial y `d_gradient_01_001.png` con la rampa vertical, en SD, HD y UHD. Nunca se sustituye un glow ausente por un cuadrado.

El shader se ejecuta en dos pasadas ponderadas por alfa; hay una ruta CPU equivalente si GL falla. La cache de fuente incluye la intensidad del filtro, para que cambiar de modo no reutilice accidentalmente la fuente desenfocada.

## Validacion

Se comprobaron las mascaras de los recursos SD/HD/UHD y la normalizacion de energia de un campo de color uniforme mediante scripts de lectura y calculo. `git diff --check` paso. Se agregaron regresiones C++ de brillo, empaquetado vertical, animacion, presupuesto y recursos ausentes.

No se ejecutaron compilaciones ni las regresiones C++, conforme a AGENTS.md. Queda pendiente comprobar el shader, el popup y el resultado importado dentro de GD.
