# Idiomas y caracteres de GD 2.2091

Auditoría local del 31 de octubre de 2026. Se revisaron los bindings que usa el
mod y las fuentes de la instalación de Geometry Dash.

## Qué contiene el SDK

`GeometryDash.bro` declara el campo `CCTextInputNode::m_unicodeText`, utilidades
para fuentes y conversión de emojis Unicode. No contiene catálogos de
traducciones ni una lista de idiomas de la interfaz de GD. La declaración de
`CCTextInputNode::setUnicodeText` no tiene dirección para Windows en estos
bindings; esta revisión no la llama ni añade hooks sobre ella.

El enum **LanguageType**, en `CCCommon.h` de cocos2d, contiene **13 idiomas del
sistema operativo**. No son trece traducciones nuevas del juego ni un límite
del número de idiomas que puede traducir el mod.

## Cobertura de las fuentes instaladas

| Fuente | Glifos únicos | Caracteres internacionales comprobados |
| --- | ---: | --- |
| `bigFont.fnt` | 98 | No tiene ñ, vocales acentuadas ni cirílico |
| `goldFont.fnt` | 96 | No tiene ñ, vocales acentuadas ni cirílico |
| `chatFont.fnt` | 634 | Cubre las muestras latinas y rusas de la tabla siguiente |

| Idioma del enum | Muestra cubierta por chatFont | Traducción actual del mod |
| --- | --- | --- |
| Inglés | Sí | Disponible |
| Español | Sí: ñ, Ñ, áéíóú, ü, ¿, ¡ | Disponible |
| Francés | Sí: acentos, ç, æ, œ | Pendiente |
| Italiano | Sí: vocales acentuadas | Pendiente |
| Alemán | Sí: ä, ö, ü, ß, ẞ | Pendiente |
| Neerlandés | Sí: acentos, diéresis, ĳ | Pendiente |
| Ruso | Sí: alfabeto ruso completo, incluida Ё/ё | Pendiente |
| Húngaro | Sí: acentos y ő/ű | Pendiente |
| Portugués | Sí: acentos, ç, ã/õ | Pendiente |
| Chino | No | Necesita fuentes y traducción |
| Japonés | No | Necesita fuentes y traducción |
| Coreano | No | Necesita fuentes y traducción |
| Árabe | No | Necesita fuentes, dirección/escritura adecuadas y traducción |

Dentro de ese enum hay **siete candidatos adicionales** con sus muestras
cubiertas por la fuente disponible: francés, italiano, alemán, neerlandés,
ruso, húngaro y portugués. Esto no demuestra cobertura de cualquier texto
posible de esos idiomas. Otros alfabetos, como el griego, también tienen
glifos en chatFont aunque no figuren en el enum. No se puede deducir un
recuento exhaustivo de idiomas solamente del `.bro`.

El traductor de comentarios usa exactamente estos nueve idiomas dibujables y
descarta los que necesitan fuentes nuevas; ver
[TRADUCTOR_COMENTARIOS.md](TRADUCTOR_COMENTARIOS.md).

## Cambios aplicados

- El perfil conserva únicamente el botón nativo de **My Socials**; las redes
  individuales permanecen dentro de su popup.
- [UnicodeLabel.hpp](../src/utils/UnicodeLabel.hpp) usa `geode::Label` con
  `chatFont.fnt` como respaldo de bigFont/goldFont. Solo afecta a las etiquetas
  del mod que lo utilizan; no modifica las fuentes globales de GD.
- Las pestañas del perfil pueden mostrar **Reseñas** y el selector de bienvenida
  puede mostrar **Español** sin perder la ñ.
- El traductor conserva sus catálogos español/inglés. Los otros siete idiomas
  necesitan sus propios textos y revisión de las pantallas que todavía usan
  cadenas bilingües directas. No se presentan como traducciones terminadas.

## Reproducir la auditoría

```powershell
python tests/audit_unicode_languages.py `
  --sdk "$env:GEODE_SDK" `
  --bindings build-clang/_deps/bindings-src `
  --resources 'C:/Program Files (x86)/Steam/steamapps/common/Geometry Dash/Resources' `
  --output build-clang/unicode-language-audit.json
```

La salida JSON incluye glifos ausentes por muestra y hashes de las fuentes.
Un paquete de texturas puede sustituirlas, por lo que hay que repetir la
comprobación cuando cambien los recursos.

## Verificación del cambio

- Cuatro regresiones del perfil/comentarios pasaron con AddressSanitizer y
  UndefinedBehaviorSanitizer, incluida la ocultación repetida de redes y la
  conservación del botón nativo de My Socials.
- Una prueba temporal dentro de GD completó 102 comprobaciones: fuentes de
  respaldo, glifos de los nueve idiomas con cobertura, tamaño de las etiquetas
  y actualización del texto. El paquete de prueba se retiró al finalizar.
- El mod se compiló, empaquetó e instaló. El SHA-256 del paquete generado y el
  instalado coincide:
  `98575b4b1dcd32ce3bd0fc22b910547068a340ceb21465240dc88b2dfd188221`.
- No se verificó visualmente el perfil completo en esta prueba; se validaron
  las etiquetas con el motor real y el comportamiento del perfil con fixtures.

Fuentes primarias:

- [GeometryDash.bro usado por el build](https://github.com/geode-sdk/bindings/blob/6f295839044600ceb772407caaa90ad4aeb02a45/bindings/2.2091/GeometryDash.bro)
- [Enum LanguageType de cocos2d](https://github.com/geode-sdk/geode/blob/57415d94ed47f6f439e2440ffde3fe20b063a063/loader/include/Geode/cocos/platform/CCCommon.h)
- [Label de Geode y fuentes de respaldo](https://github.com/geode-sdk/geode/blob/57415d94ed47f6f439e2440ffde3fe20b063a063/loader/include/Geode/ui/Label.hpp)
