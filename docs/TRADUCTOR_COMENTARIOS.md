# Traductor de comentarios y descripciones

Implementado el 4 de noviembre de 2026. Traduce los comentarios y la descripción
del nivel a un idioma elegido, y permite traducir lo que escribes antes de
publicarlo.

## Motor gratuito

El endpoint por defecto es el de Google gtx:

```
https://translate.googleapis.com/translate_a/single?client=gtx&dt=t&sl=auto&tl=<idioma>&q=<texto>
```

No hay clave ni cuenta. El servicio detecta el idioma de origen, así que el mod
solo envía el texto y el idioma de destino. La petición lleva `dt=t` (sin él gtx
responde sin segmentos traducidos) y un `User-Agent` de navegador: sin esas dos
cosas Google contesta `429` a las peticiones del juego.

El respaldo es `clients5.google.com/translate_a/t`, también gratuito y sin
clave. `lingva.ml` está devolviendo `403` desde octubre de 2026, así que solo se
usa si tú apuntas el endpoint a una instancia propia.

**Cada usuario puede usar su propio endpoint.** En el panel del traductor hay un
campo `Tu endpoint de traduccion` con botones `Guardar` y `Predeterminado`. Se
acepta cualquier URL `http(s)`:

- Un proxy propio que reenvíe a gtx y añada su propia cuota o caché.
- Una instancia de Lingva (`https://mi-lingva/api/v1/auto`); el mod detecta que
  no es Google y usa el formato `/origen/destino/texto`.
- `translate_a/single` o `translate_a/t` de Google; el mod añade solo los
  parámetros que falten (`client`, `dt`, `sl`, `tl`, `q`).

Si el endpoint del usuario falla, se intenta el integrado y después el segundo
endpoint de Google. Las respuestas se guardan en memoria por idioma y texto, así
que volver a ver un comentario ya traducido no hace otra petición.

Las peticiones pasan por el mismo control de sesión que el resto de funciones
online del mod: si la sesión no está autorizada, la traducción no sale.

## Dónde aparece

- **Popup de los tres puntos**: botón `TRAD` en la misma fila que `COPY`,
  `DELETE` y `REPORT`, a la izquierda de todo el grupo. Si la fila llena el
  marco, se apila encima para no tapar ningún botón. Abre el panel del
  traductor.
- **Celda de comentarios**: con el traductor activo aparece una `T` a la
  izquierda de los iconos de like y borrar, medida en tiempo de ejecución para no
  taparlos ni invadir el nombre o el icono del autor. Traduce ese comentario; al
  pulsarla otra vez vuelve al original.
- **Descripción del nivel**: en la ventana de información, una `T` junto a la
  descripción traduce el texto y lo restaura al pulsarla de nuevo.
- **Add Comment**: botón `TRAD` bajo los botones de emote y fuente. Traduce el
  texto escrito al idioma que elijas antes de enviarlo.

## Ajustes

Se guardan como valores del mod, no en `mod.json`:

| Clave | Significado | Defecto |
| --- | --- | --- |
| `translator-enabled` | muestra la `T` en celdas y descripción | `false` |
| `translator-auto` | traduce al cargar comentarios y descripción | `false` |
| `translator-target` | idioma de destino | idioma de la interfaz |
| `translator-compose-target` | último idioma elegido en Add Comment | `translator-target` |
| `translator-endpoint` | endpoint propio del usuario | Google gtx |

El idioma de destino siempre se puede cambiar desde el panel. Al cambiarlo, las
celdas abiertas pierden la traducción anterior y vuelven al texto original.

El selector de Add Comment guarda su propia elección en
`translator-compose-target`. Antes marcaba siempre `translator-target` y no
guardaba nada, así que volvía a mostrar español aunque se hubiera traducido a
otro idioma. Se guarda aparte para que traducir lo que escribes al inglés no
cambie el idioma al que se traducen los comentarios que lees.

## Idiomas disponibles

Inglés, español, francés, alemán, italiano, portugués, neerlandés, ruso y
húngaro. Son los idiomas que `chatFont.fnt` puede dibujar: latín completo y
cirílico. Chino, japonés, coreano y árabe necesitan glifos que el juego no
incluye y no se ofrecen.

Los nombres de idioma se dibujan con `chatFont.fnt` para conservar `ñ`, `ç`,
`ß`, `ő`, `ã` y cirílico. Los textos de la interfaz del traductor están en los
catálogos español/inglés de `Localization.cpp`.

El panel sigue la estética nativa de GD: fondo `GJ_square01.png`, esquinas
`dailyLevelCorner_001.png`, paneles interiores `square02b_001.png` con
encabezado dorado, toggles estándar de cocos y botones `ButtonSprite` con los
textos `GJ_button_0X.png` del juego.

El panel mide 440x290 para caber en una pantalla de 320 puntos; antes medía
400 de alto y `PaimonPopup::show` lo reducía al 73 %, por eso todo se veía
pequeño. Tiene tres secciones: los dos interruptores con una línea de ayuda
cada uno, la cuadrícula de idiomas con el aviso `Los comentarios se traducen a`
en la fila del encabezado, y el endpoint en una sola fila (campo, `Guardar` y el
icono `GJ_updateBtn_001.png` para volver al predeterminado). No hay botón
`Listo`: cada cambio se guarda al momento y se cierra con la X. Las fichas de
idioma (`TranslatorLanguageGrid.*`) son las mismas del selector de Add Comment:
insignia con el código, nombre nativo y una marca en el idioma elegido.

## Privacidad

El texto del comentario o de la descripción, sin etiquetas de color de GD ni
glifos privados, se envía al endpoint configurado por el usuario. El mod no
guarda el texto ni el resultado en disco; solo mantiene una caché en memoria
mientras el juego está abierto.

## Código

- `src/features/translator/TranslatorText.*`: limpieza del texto y corte UTF-8.
- `src/features/translator/TranslatorLanguages.*`: tabla de idiomas.
- `src/features/translator/TranslatorConfig.*`: ajustes persistidos y validación
  del endpoint del usuario.
- `src/features/translator/TranslationService.*`: peticiones, caché, formato gtx
  y Lingva, y respaldo al endpoint integrado.
- `src/features/translator/ui/`: panel de ajustes, selector de idioma y el
  toggle reutilizable de descripción.
- `src/hooks/CommentOptionsPopupTranslator.cpp`: botón `TRAD` del popup.
- `src/hooks/CommentCellTranslator.cpp`: botón `T` de cada comentario.
- `src/hooks/InfoLayer.cpp`: botón `T` de la descripción del nivel.
- `src/hooks/ShareCommentLayer.cpp`: botón `TRAD` de Add Comment.

## Verificación

- `tests/translator_text_regression.cpp` cubre etiquetas de color, glifos
  privados, emojis, texto vacío y cortes UTF-8.
- Los tres endpoints se probaron desde esta máquina: gtx y
  `clients5.google.com/translate_a/t` responden `200` con `Hola Mundo`;
  `lingva.ml` responde `403`.
- El mod compiló, se empaquetó y se instaló. El SHA-256 del paquete generado y
  el instalado coincide:
  `5EC1AA97DD1D82DE3C16EC99E73707C72D059A1E5CD1348C91DE2C5345F5A17B`.

El primer botón `TRAD` quedaba fuera del marco porque `m_buttonMenu` está
centrado en el alert y se estaba usando una coordenada del `m_mainLayer`, que
ocupa la pantalla completa. Ahora se calcula desde el botón `REPORT`, que sí
está dentro del popup, y se limita al área del fondo. El hook se registra como
`PaimonCommentOptionsTranslator -> CommentOptionsPopup::init`, confirmado en los
logs de Geode.

El comentario se traducía pero seguía mostrando el texto original porque
escribir en el `TextArea` no repinta la celda en 2.2091. Ahora el texto se
aplica reejecutando `CommentCell::loadFromComment` con
`GJComment::m_commentString` cambiado temporalmente y restaurado justo después:
es el mismo camino que usa GD al reciclar celdas, así que reconstruye el
`TextArea` de verdad. Un flag interno evita que ese rebuild borre el botón y su
estado.

Al desactivar el traductor la traducción no se deshacía porque `applyText`
comparaba contra `m_commentString`, que ya se había restaurado, y salía sin
repintar. La celda guarda ahora `displayed`, el texto realmente pintado, y las
decisiones de restaurar se toman contra ese valor. Al desactivar también se
restaura la descripción del nivel, no solo se oculta su botón.

La `T` de la celda ya no usa un desplazamiento fijo. Se ancla al contador de
likes y solo mide los controles de esa fila (like y borrar) dentro de una
ventana corta a su izquierda; el botón del nombre y la píldora del ID de nivel
quedan fuera de la medición, así que la `T` no se va al extremo izquierdo ni tapa
el icono o el nombre. El `TRAD` del popup hace lo mismo con todos los botones de
la fila de `REPORT`, de modo que `DELETE` y `COPY` siguen siendo pulsables. La
`T` de la descripción conserva su anclaje al bloque de texto en `InfoLayer.cpp`.
Las URLs del endpoint se validan en `normalizeEndpoint`; no se aceptan esquemas
distintos de `http(s)`.

Los comentarios del perfil rediseñado perdían el nombre del autor porque
`hideVanillaChrome` (`RedesignedProfile.cpp`) ocultaba por ID todo el árbol de
`ProfilePage`, y las celdas de comentario reutilizan los mismos IDs
(`username-label`, `username-menu`) que la cabecera del perfil. Ese recorrido ya
no entra en las listas de comentarios ni en las celdas, así que el autor vuelve a
verse junto a su icono. La fila de iconos del perfil (`icon-background`) sigue
ocultándose como parte del chrome.
