# Transparencia de IA en niveles de Geometry Dash

## 01. Resumen

Este paper propone un sistema reproducible para documentar la participación de inteligencia artificial en niveles de Geometry Dash. Su objetivo es que los creadores puedan explicar qué hicieron, qué herramientas usaron y qué partes de un nivel tienen procedencia conocida. Usar IA es compatible con crear, compartir y colaborar. El sistema no propone bloqueos, sanciones, reducción de visibilidad ni una valoración de la calidad artística basada en el origen.

La entrada principal es la cadena serializada del nivel: configuración, objetos y propiedades. El análisis combina reglas sintácticas, geometría, estructura de grupos, comparación con generadores y registros de procedencia. Se distingue entre una observación del archivo, una hipótesis estadística y una declaración verificable sobre una operación concreta. Una repetición de bloques puede provenir de una persona, una plantilla o un generador con IA. Esa ambigüedad forma parte del resultado.

La contribución es una arquitectura de análisis determinista, una especificación de evidencias, un protocolo de evaluación y un prototipo abierto que funciona en el navegador sin enviar niveles a un servidor. El prototipo reconoce patrones compatibles con el importador de Paimbnails y calcula métricas, pero responde «indeterminado» sobre autoría IA. No contiene un clasificador validado sobre niveles reales.

**Estado de esta edición:** propuesta metodológica y prototipo de extracción, versión 0.1, 12 de octubre de 2026. No es un estudio revisado por pares ni presenta precisión experimental de detección de IA. Los ejemplos numéricos de clasificación se identifican como hipotéticos. El código del mod se inspeccionó sin compilarlo.

**Palabras clave:** Geometry Dash, procedencia, inteligencia artificial, análisis de cadenas, generación procedural, marcas de agua, incertidumbre, autoría mixta, explicabilidad.

## 02. Qué significa detectar y qué significa transparentar

Hay cuatro preguntas diferentes. ¿Qué contiene el nivel? ¿Qué herramienta pudo producir una parte? ¿Hay evidencia de que se usó un modelo generativo? ¿Qué modelo y versión participaron? La primera suele admitir mediciones directas. Las restantes necesitan evidencias adicionales y pueden quedar sin respuesta.

Una herramienta puede ordenar grupos, vectorizar una imagen o colocar una plantilla sin usar un modelo generativo. Una persona puede pedir ideas a una IA y colocar todos los objetos manualmente. Un agente puede invocar un importador determinista para convertir una imagen dibujada por una persona. El artefacto final mezcla decisiones, transformaciones y fuentes; «creado con IA» necesita un alcance explícito.

Para este trabajo, «participación de IA» significa el uso declarado o documentado de un modelo aprendido para producir o transformar una contribución creativa: layout, decoración, texto, imagen, animación o instrucciones aplicadas al editor. El análisis separa esta categoría de automatización determinista. Es una definición operativa del paper; no pretende resolver todos los usos de la palabra IA.

El resultado debe presentar la contribución humana, las operaciones asistidas y la incertidumbre sin convertir un informe técnico en un juicio sobre el creador. Una política de transparencia puede ofrecer información voluntaria y accesible. El nivel sigue siendo compartible cuando la procedencia es desconocida.

| Pregunta | Evidencia necesaria | Respuesta legítima sin esa evidencia |
| --- | --- | --- |
| ¿Cuántos objetos hay? | Parseo válido y versión del analizador | No analizable si falla el formato |
| ¿Se parece a una salida del importador? | Coincidencia con reglas documentadas | Compatible o sin coincidencia observada |
| ¿Participó IA? | Declaración, registro de operación o inferencia validada | Indeterminado |
| ¿Qué modelo intervino? | Registro de proveedor/modelo/versión con alcance | No atribuible |
| ¿Qué porcentaje del proceso fue IA? | Historial y definición del denominador | No estimable desde la cadena sola |

## 03. La frontera de lo identificable

Representemos el proceso de creación como una función F. H es la secuencia de decisiones humanas, M las operaciones de modelos, P las herramientas procedurales, A los assets y S el serializador. La cadena observada es L.

```equation
(1) L = F(H, M, P, A, S)
```

F no es inyectiva: varios procesos pueden producir exactamente los mismos bytes. Por ejemplo, una persona y una IA pueden escribir «1,1,2,30,3,15;». Un exportador puede convertir dos diseños distintos al mismo orden de propiedades. Si dos procesos producen L idéntico, ningún algoritmo que vea únicamente L puede diferenciarlos en ese caso.

```equation
(2) F(H₁, M₁, P₁, A₁, S₁) = F(H₂, M₂, P₂, A₂, S₂) ⇒ observación idéntica
```

En una evaluación binaria con igual probabilidad previa, el mejor error posible está relacionado con la distancia de variación total entre distribuciones de observaciones. Cuanto más se solapen las distribuciones, menos información hay para separarlas.

```equation
(3) error* = (1 − TV(P₀, P₁)) / 2, con priors iguales y observación completa
```

Esta formulación es un límite de identificación, no una medida obtenida sobre GD. La investigación de Sadasivan y colaboradores analiza límites y ataques de detección en texto generado. Aquí la extrapolación al problema de niveles es una inferencia de diseño, no un resultado experimental de ese trabajo sobre Geometry Dash. [Can AI-Generated Text be Reliably Detected?](https://arxiv.org/abs/2303.11156)

Por ello, el sistema no promete reconocer todos los niveles generados, demostrar ausencia de IA ni reconstruir cómo pensó un modelo. Puede medir regularidades, reconocer operaciones documentadas y evaluar hipótesis dentro de una población bien definida.

## 04. La cadena de pensamiento no es una huella recuperable

La cadena del nivel contiene propiedades de objetos y configuración. No contiene los estados internos, activaciones o pasos privados que produjo un modelo al trabajar. Un texto explicativo adjunto puede ser una explicación posterior y no una transcripción causal completa. La investigación de Turpin y colaboradores muestra casos donde explicaciones de razonamiento omiten influencias de las entradas. [Language Models Don't Always Say What They Think](https://arxiv.org/abs/2305.04388)

En este sistema, «traza» significa un registro observable de operaciones: invocar una herramienta, importar un asset, aplicar un patch, aceptar un cambio o modificar objetos. No significa acceder al pensamiento interno de una persona o una IA. Un proveedor puede entregar resúmenes o recibos; cada uno se etiqueta por lo que realmente acredita.

Los prompts y explicaciones pueden adjuntarse voluntariamente para contexto, con edición de información privada. No son necesarios para publicar un nivel ni bastan como prueba de autoría. La especificación no solicita secretos, claves, conversaciones privadas ni razonamientos internos.

## 05. Qué se inspeccionó en Paimbnails

El análisis del mod se realizó en Paimbnails-main, revisión Git a2419d80371e113e4f274c2998262f40dd91839f. El inventario de archivos y sus SHA-256 se publica junto al paper. Una revisión Git identifica la base; el hash por archivo identifica el contenido inspeccionado, incluso si un checkout tuviera cambios locales.

| Archivo del repositorio | Observación del código | Implicación para este trabajo |
| --- | --- | --- |
| src/features/gif-import/services/GifObjectEmitter.cpp | Emite IDs, posiciones con tres decimales, escalas con cuatro y flags repetidos | Hay rasgos léxicos de un emisor concreto; pueden cambiar al guardar |
| src/features/gif-import/services/ImageWatermark.cpp | Divide primitivas e introduce vueltas completas en rotación | Se puede buscar compatibilidad con esa marca |
| src/features/gif-import/services/ImageWatermark.hpp | Define los umbrales del detector existente | Los umbrales son reglas de ingeniería; no incluyen una FPR medida |
| src/features/gif-import/services/GifImportPipeline.cpp | Aplica la marca al plan de importación | La marca corresponde a la conversión de imágenes/GIF, no a su origen IA |
| src/hooks/LevelInfoLayer.cpp | Descomprime cuando corresponde y muestra aviso de imagen importada | El aviso existente trata del importador y el rendimiento |
| src/features/editor-physics/services/PhysicsTriggerEmitter.cpp | Emite objetos/triggers a partir de simulación y planificación | Una estructura muy regular también puede ser automatización convencional |
| src/features/collab-editor/CollabManager.cpp | Serializa objetos mediante getSaveString y transmite operaciones | La procedencia futura debe contemplar equipos y guardados intermedios |

El prototipo web no cambia esos archivos ni incorpora la clasificación al mod. La inspección es estática. No se afirma que las marcas sobrevivan a todas las operaciones del editor, plataformas o versiones; eso requiere pruebas específicas.

## 06. La representación del nivel

Una cadena descomprimida suele separar registros con punto y coma y representar propiedades como pares clave,valor. El primer registro puede ser configuración con claves k*. Los objetos usan claves numéricas. Un ejemplo deliberadamente pequeño es:

```text
kS38,1;1,211,2,30.000,3,15.000,21,100,128,1.0000,129,0.5000;
```

No es código fuente de un modelo ni un programa de lenguaje natural. Algunas propiedades sí participan en comportamiento mediante triggers. El análisis debe respetar los separadores, conservar propiedades desconocidas y consultar esquemas específicos de versión antes de interpretar su semántica.

```equation
(4) L = cabecera ; o₁ ; o₂ ; … ; oₙ ;
(5) oᵢ = {(k₁,v₁), (k₂,v₂), …}, con claves únicas en el parser del laboratorio
```

El perfil inspeccionado usa 1 como ID de objeto, 2/3 como posición, 6 como rotación, 21 como canal de color, 24/25 como capas, 57 como lista de grupos y 128/129 como escalas. En las familias 901, 1007 y 1268 examinadas, 51 identifica un destino de grupo. Esa lectura no se generaliza automáticamente a todos los objetos.

No se encontró un archivo .bro identificable en las carpetas examinadas. La extensión por sí sola no especifica una gramática. El paper no inventa su formato. Un adaptador futuro debe documentar el contenedor, su versión, la extracción de levelString y un fixture real antes de declararlo compatible. El laboratorio rechaza contenedores XML, JSON y .bro no documentados.

## 07. Ingesta, límites y conservación de evidencia

La ingesta recibe una cadena descomprimida o Base64/Base64url con gzip o zlib reconocido. Se registra el hash de la entrada y del texto descomprimido. El original debe conservarse cuando se necesite una auditoría: una normalización puede borrar precisamente las señales que se quieren estudiar.

Antes de crear estructuras grandes se limitan bytes, objetos y propiedades. El laboratorio admite 8 MiB de entrada, 32 MiB descomprimidos, 100.000 objetos, 512 pares por registro y 4.096 caracteres por valor. La lectura de descompresión comprueba el tamaño acumulado. El trabajo se ejecuta en un Worker cancelable; un temporizador interrumpe análisis que exceden 30 segundos.

Las claves duplicadas se rechazan. Resolverlas arbitrariamente como «gana la última» puede ocultar ambigüedad. Valores numéricos no finitos, registros incompletos y listas de grupos malformadas producen un error explícito. «No analizable» es un estado técnico separado de «indeterminado».

El laboratorio acepta un subconjunto documentado y puede rechazar cadenas que otro parser tolere. No es una implementación completa del formato de Geometry Dash. Los límites son decisiones de este prototipo; no límites universales del juego.

## 08. Tres representaciones, tres preguntas

La representación léxica conserva orden de claves, literales decimales, orden de objetos y cabecera. Sirve para estudiar exportadores. La representación estructural convierte propiedades conocidas a números, listas y relaciones. Sirve para medir geometría. La representación de ejecución requeriría el motor, estados de cámara, color, movimiento y tiempo. Sirve para evaluar lo que ve y juega una persona.

```equation
(6) X = (X_léxico, X_estructural, X_ejecución)
```

El prototipo implementa las dos primeras parcialmente. No ejecuta el nivel ni pretende decidir si es jugable. Un render bonito, una densidad alta o un ritmo extraño no constituyen evidencia de IA.

La versión web también calcula un hash de comparación sintáctica: ordena claves mediante comparación UTF-16 y el multiconjunto de objetos, preservando valores literales y multiplicidad. Cambiar el orden de los registros no cambia ese hash; cambiar «30» por «30.0» sí. Los hashes exactos conservan incluso espacios y saltos de línea exteriores; el parser los recorta solo para interpretar registros. No es un hash semántico: el orden de objetos puede importar al comportamiento de GD y omitirlo sería peligroso para una firma de identidad exacta.

```equation
(7) h_exact = SHA256(bytes exactos); h_multiset = SHA256(representación sintáctica documentada)
```

Las firmas de procedencia se vinculan al hash exacto, nunca solo al hash de comparación. El prototipo no usa JCS para ese hash sintáctico. El esquema de firma propuesto sí exige una implementación conforme de canonicalización JSON. [RFC 8785: JSON Canonicalization Scheme](https://www.rfc-editor.org/rfc/rfc8785)

## 09. Hipótesis y estados públicos

H₀ incluye creación manual, copiar y pegar, templates, importaciones de fuentes humanas y generadores deterministas sin participación generativa declarada. H₁ incluye una contribución de un modelo generativo al alcance analizado. Hᵤ recoge procesos no representados en el corpus. Estas categorías no ordenan calidad ni mérito.

Cada evidencia tiene un alcance: cadena completa, operación, intervalo temporal, asset o conjunto de objetos. Si se documenta una imagen de fondo asistida por IA, el informe no transfiere esa etiqueta al gameplay.

| Estado | Significado |
| --- | --- |
| Declarado por el creador | Hay una declaración explícita con alcance; no se ha autentificado una operación |
| Operación documentada | Un registro permite identificar entrada, salida, herramienta y alcance |
| Operación firmada y verificada | Se verifican firma, emisor confiable, vínculo al artefacto y vigencia según una política publicada |
| Compatible con herramienta | Se cumplen reglas del artefacto; no se autentifica quién las produjo |
| Indicio estadístico | Un procedimiento validado observa señales de una población definida |
| Indeterminado | La evidencia no permite atribuir participación de IA |
| No analizable | El formato o los recursos exceden el alcance del parser |

«No se observan señales» no equivale a «100 % humano». «Firma válida» no equivale a «todo el nivel es IA» ni a «la declaración es exhaustiva». Un emisor autentificado puede equivocarse o registrar solo parte de un flujo.

## 10. Algoritmo general propuesto

La arquitectura se denomina aquí PTL: Procedencia y Transparencia de Niveles. Este nombre identifica una propuesta del proyecto, no un estándar existente. El núcleo usa algoritmos convencionales: parsing, hashing, histogramas, grafos, comparación y evaluación estadística. No pide a otro LLM que adivine la autoría.

```text
PTL(entrada, esquemaGD, corpusVersionado, recibos, políticaDeConfianza):
    validar límites y extraer cadena con adaptador conocido
    conservar hashes exactos y descripción del contenedor
    parsear cabecera, objetos y propiedades sin perder lo desconocido
    calcular rasgos léxicos y estructurales por regiones
    comprobar compatibilidad con herramientas registradas
    verificar recibos disponibles y vincularlos al alcance exacto
    si hay evidencia autentificada: describir la operación cubierta
    si corpus/calibración/soporte son insuficientes: abstenerse de inferir IA
    en otro caso: evaluar hipótesis y reportar incertidumbre calibrada
    producir informe con alternativas, límites y versión de reglas
    no aplicar sanciones ni alterar la publicación del nivel
```

En la edición 0.1 están implementados la ingesta, hashes, parseo, métricas básicas, compatibilidad con la marca conocida e informe. Segmentación avanzada, corpus, comparación de generadores, recibos y calibración son una especificación para investigación. Su estado se mantiene visible en la página y en el informe.

## 11. Rasgos léxicos de la cadena

Se mide el histograma de secuencias de claves por objeto, cantidad de decimales literales, notación científica, propiedades opcionales y longitudes de registros. Un serializador puede escribir siempre «1,2,3,21,64,67,121,134,128,129». Otro puede ordenar claves o suprimir defaults. Estos rasgos identifican una ruta de exportación con más facilidad que el origen creativo.

```equation
(8) H_claves = −Σₛ p(s) log₂ p(s)
(9) D_claves = maxₛ conteo(s) / N
```

Una entropía pequeña indica que pocos órdenes dominan. No indica que un modelo haya generado el nivel. El guardado del editor puede reserializar objetos y borrar patrones del emisor original. Las métricas se calculan antes y después de esos guardados al construir el corpus.

El prototipo cuenta precisión decimal literal de x, y y escalas. «1.0000» y «1» son valores numéricamente iguales pero evidencias léxicas diferentes. Esta separación evita mezclar una firma de escritura con una decisión geométrica.

## 12. Cuantización, rejillas y offsets

Se comprueba si las posiciones se alinean con pasos candidatos. La rejilla del editor, un importador de píxeles y una construcción manual pueden dar el mismo patrón. Se debe comparar con fuentes convencionales antes de interpretar el resultado.

```equation
(10) d_h(x) = |x/h − round(x/h)|
(11) Q(h) = Σᵢ 1[d_h(xᵢ) ≤ ε y d_h(yᵢ) ≤ ε] / N_pos
```

El laboratorio prueba h ∈ {0,25; 0,5; 1; 3; 5; 10; 15; 30} y ε = 0,0001 en coordenadas relativas al paso. La cuadrícula está anclada en el origen. Un diseño trasladado puede perder alineación aunque conserve su estructura.

La especificación avanzada propone estimar offsets a y b con residuos modulares y evaluar Q(h,a,b). Debe separar búsqueda y evaluación: escoger el mejor paso entre muchos aumenta la coincidencia esperable bajo H₀. La selección múltiple se calibra sobre un conjunto independiente.

```equation
(12) Q(h,a,b) = meanᵢ 1[d_h(xᵢ−a) ≤ ε y d_h(yᵢ−b) ≤ ε]
```

## 13. Repetición, simetría y gramáticas

El prototipo agrupa tuplas literales de ID, rotación, escalas, color y capas, omitiendo posición y grupos. Su fracción de repetición cuenta objetos adicionales a la primera aparición de cada tupla. No reconoce una estructura decorativa completa.

```equation
(13) R_simple = (N − número de tuplas distintas) / N
```

Para motivos completos se propone particionar por ventanas espaciales, trasladar cada motivo a un origen común y comparar ID, distancias relativas, orientación y propiedades. La normalización de grupos usa una renumeración consistente dentro del motivo. Cada relación se preserva; eliminar IDs sin conservar referencias destruiría información.

Simetría refleja o rota geometría alrededor de candidatos documentados. Una puntuación de similitud puede ser Jaccard entre descriptores cuantizados. Estas comparaciones deben acompañarse de coincidencias concretas y tolerancias. El prototipo no las implementa todavía.

```equation
(14) J(A,B) = |A ∩ B| / |A ∪ B|
```

Una gramática procedural produce secuencias regulares. Una persona también puede construir así. El objetivo es reconocer reutilización y posibles familias de herramientas, no declarar que una estructura repetida es pensamiento artificial.

## 14. Geometría, paletas y densidad

El histograma de IDs resume la variedad de objetos. Su entropía y el número de IDs distintos pueden describir una conversión dominada por primitivas. Los canales de color son referencias: dos canales pueden terminar con el mismo color y un solo canal puede cambiar mediante triggers.

```equation
(15) H_ID = −Σⱼ p(ID=j) log₂ p(ID=j)
```

Una auditoría visual completa necesitaría resolver color inicial, HSV, blending, capas, cámara y estados temporales. El paper propone contar cobertura y densidad en regiones bajo condiciones de render fijadas. No convierte el número de canales en diversidad perceptual ni el número de objetos en complejidad artística.

La densidad local puede resumirse como Nᵣ/Aᵣ, pero el área de bounding boxes puede ser engañosa: objetos transparentes, fuera de cámara o de gran escala cambian el denominador. Se debe publicar cómo se calcula el área y qué objetos se excluyen.

## 15. Grupos, triggers y ejecución

Un grafo útil requiere tipos de nodos y aristas: pertenencia a grupo, referencia a destino, dependencia temporal y modificación de propiedades. El esquema por versión decide qué significa cada campo para cada objeto. Un único campo 51 no basta para reconstruir todos los triggers.

```equation
(16) G = (V_objetos ∪ V_grupos, E_pertenencia ∪ E_destino ∪ E_tiempo)
```

El laboratorio calcula membresías y destinos directos solamente para move 901, alpha 1007 y spawn 1268. Informa destinos sin miembros directos como un dato, no como un error. Un grupo vacío puede tener uso indirecto, comportamiento válido o pertenecer a una parte omitida.

La versión avanzada propone componentes fuertemente conexas, fan-in, fan-out, profundidad, motivos de animación y cadenas temporales. Se deben comparar con compiladores de física, keyframes y animación convencional: estos generan grafos altamente regulares sin modelos generativos.

No se infiere intención del creador a partir de errores o grupos huérfanos. Tampoco se emite una valoración de jugabilidad sin ejecutar y probar las condiciones necesarias.

## 16. La marca existente del importador

En el código inspeccionado, ciertas primitivas se dividen con proporciones aproximadas 0,381966 y 0,438447. Las rotaciones se pueden incrementar en 2160 o 3960 grados; son vueltas completas adicionales. El detector admite familias de vueltas 6, 7, 11 y 12 y observa flags en campos 64, 67, 121 y 134.

Para una pareja se comprueba el canal de color, diferencia angular módulo 360, escala compatible en el eje no dividido, proporción de escalas y distancia geométrica esperada. El prototipo reproduce la comparación de pares consecutivos del detector inspeccionado; no afirma invariancia frente a ordenar todos los objetos de otra manera.

```equation
(17) r = min(|s₁|,|s₂|) / (|s₁| + |s₂|)
(18) marca_r ⇔ |r−0,381966| < 0,003 o |r−0,438447| < 0,003
(19) distancia_esperada = 30 × (|s₁| + |s₂|) / 2
```

La regla existente acepta una marca de rotación con al menos tres flags, tres marcas de rotación o al menos tres parejas geométricas con las dos variantes observadas. Los nombres internos «signed» se refieren a flags acompañantes. No hay una firma criptográfica, una clave privada ni autenticación del emisor en ese detector.

```equation
(20) compatible ⇔ flaggedRotations > 0 ∨ rotations ≥ 3 ∨ qualifiedPairs ≥ 3
```

Una coincidencia significa «compatible con reglas del importador Paimbnails». Alguien puede reproducir esos objetos y una operación del editor puede eliminar las vueltas. La tasa de coincidencias accidentales y la supervivencia necesitan medirse. El umbral tres no equivale por sí mismo a una probabilidad de error conocida.

Una imagen humana importada y una imagen generada por difusión pasan por el mismo convertidor. La marca no separa esos orígenes. El informe muestra «provesAI: false» y «authenticated: false». El conteo web recorre todos los objetos; el C++ puede devolver antes al alcanzar el umbral y dar conteos parciales.

## 17. Familias de modelos y rutas de producción

Cubrir «todos los modelos existentes» no puede significar tener una huella fiable de cada proveedor, versión, fine-tune y flujo privado. La propuesta organiza rutas de producción y permite incorporar perfiles versionados. Una familia desconocida produce abstención, no una atribución inventada.

| Ruta | Operación habitual | Evidencia posible en el nivel | Principal confusión |
| --- | --- | --- | --- |
| Modelo de lenguaje que escribe cadena | Generación token a token de texto estructurado | Orden de claves, errores de sintaxis, patrones geométricos | Serializadores humanos y scripts |
| Modelo de lenguaje que escribe un script | Código que produce objetos | Huella del script o exportador | El código final puede ser idéntico al humano |
| Agente que llama herramientas | Secuencia de acciones en el editor | Patches, recibos y diffs cuando se registran | Misma herramienta invocada manualmente |
| Generación de imagen/video | Asset seguido de conversión a objetos | Huella del convertidor | Arte humano convertido con el mismo proceso |
| Modelo de layout, grafo o tiles | Distribución de estructuras aprendida | Estadísticas de motivos y dependencias | PCG convencional y templates |
| Modelo condicionado por una referencia | Transformación parcial de una obra | Regiones mixtas e historial | Copia, interpolación o edición manual |
| Optimización con modelo aprendido | Selección/ajuste de propuestas | Registro de optimización | Búsqueda, solvers y simulación sin aprendizaje |
| Procedimiento sin modelo aprendido | L-systems, reglas, ruido, templates, vectorización | Regularidad procedural | Se confunde fácilmente con IA si H₀ es pobre |

Los transformers usan atención para procesar representaciones de secuencias; el artículo fundacional describe esa arquitectura. La arquitectura no implica una marca universal en un archivo de GD. [Attention Is All You Need](https://arxiv.org/abs/1706.03762)

En una ruta autoregresiva, una distribución condicionada produce tokens. Temperatura, muestreo, prompts y posprocesado cambian la salida. El detector propuesto no conoce esos parámetros desde una cadena final ni utiliza la siguiente ecuación como huella.

```equation
(21) p(z₁…zₘ | contexto) = ∏ₜ p(zₜ | z₁…zₜ₋₁, contexto)
```

Los modelos de difusión descritos por Ho y colaboradores aprenden un proceso de denoising. Si la imagen pasa luego por vectorización, el archivo conserva sobre todo las decisiones de esa conversión; demostrar el origen del asset requiere evidencias del asset o su creación. [Denoising Diffusion Probabilistic Models](https://arxiv.org/abs/2006.11239)

Generadores adversariales, modelos latentes, modelos de grafos y otros sistemas también pueden integrarse por estas rutas. No se presupone conocer sus pesos o razonamiento. El trabajo sobre PCGML ofrece un marco para generación de contenido mediante aprendizaje; no proporciona un detector universal de niveles. [Procedural Content Generation via Machine Learning](https://arxiv.org/abs/1702.00539)

## 18. Registro de herramientas y modelos

Cada perfil identifica la herramienta, versión, configuración, plataforma, esquema GD, fuente del fixture y transformaciones aplicadas después. Los perfiles de un serializador y de un modelo se almacenan por separado. Si dos modelos pasan por el mismo emisor, comparten huella de emisor sin convertirse en el mismo modelo.

Un perfil de modelo incluye el identificador literal reportado por el proveedor, fecha de ejecución, versión cuando exista y evidencias de la invocación. «Alias de proveedor» no se presenta como versión inmutable si el proveedor puede cambiar su significado.

| Campo del registro | Propósito |
| --- | --- |
| profileId / revision | Identidad de la regla reproducible |
| operationFamily | Texto, script, herramienta, asset o layout |
| emitter / emitterVersion | Separar el convertidor del modelo |
| modelIdentifier / modelVersion | Registrar lo documentado; admitir unknown |
| gdSchema / exportPath | Evitar comparar formatos incompatibles |
| evidenceFixtureHashes | Vincular reglas a salidas inspeccionadas |
| measuredDomain / exclusions | Delimitar población válida |
| transformationsTested | Guardar, reordenar, trasladar, renumerar y otras operaciones |
| evaluationRevision | Métricas y fecha del estudio |

Una marca de un convertidor de assets no es un perfil de su modelo de origen. Un perfil sin evaluación solo permite mostrar compatibilidad con una regla, con su versión y limitaciones.

## 19. Comparación con generadores conocidos

Se propone obtener salidas de herramientas con entradas y seeds documentados. La distancia entre un nivel y un perfil combina bloques de rasgos, cada uno normalizado por su distribución de referencia. Los pesos se fijan antes del test final y no se eligen para favorecer un resultado concreto.

```equation
(22) d(x, perfil) = Σ_b w_b × d_b(x_b, perfil_b), con w_b ≥ 0 y Σ_b w_b = 1
```

Distancias posibles incluyen diferencia de histogramas, distancia de Wasserstein unidimensional para escalas o separación entre descriptores de motivos. La elección debe justificarse por la variable y validarse. Un vecino cercano es «similar a este perfil»; no acredita que la herramienta produjo el archivo.

Para reconocer una salida concreta se puede comparar hash exacto y después un diff documentado. Para reconocer un motivo se puede usar un hash de descriptor y verificación de la geometría original. Hashes aproximados no sustituyen una prueba de autenticidad.

No se implementa esta comparación de corpus en el laboratorio 0.1 porque no existe todavía un corpus publicado y calibrado para este proyecto. La marca conocida es una regla explícita independiente de ese futuro registro.

## 20. Inferencia estadística sin otro modelo generativo

Si se reúne un corpus adecuado, un clasificador estadístico clásico puede usar razones de verosimilitud sobre rasgos discretizados. El algoritmo de inferencia es determinista una vez congelados bins, conteos y parámetros. No necesita consultar un modelo generativo, pero sí necesita ejemplos etiquetados para estimar sus distribuciones.

```equation
(23) P(f_b=v | H_c) = (n_c,b,v + λ) / (n_c,b + λ K_b)
(24) score(x) = Σ_b log[P(f_b(x) | H₁) / P(f_b(x) | H₀)]
```

λ es suavizado positivo y Kᵦ la cantidad de bins. La suma supone una factorización que puede ser falsa: precisión decimal y orden de claves dependen del mismo emisor. El score no se convierte directamente en una probabilidad de IA. Se requiere evaluar dependencia, agrupar señales redundantes y calibrar fuera del entrenamiento.

Una alternativa es regresión logística con regularización o un árbol pequeño cuyas decisiones puedan inspeccionarse. «Algorítmico» no elimina la necesidad de datos, ni convierte reglas arbitrarias en evidencia. El paper especifica estas alternativas; el prototipo no contiene parámetros entrenados ni scores de autoría.

La hipótesis nula debe incluir importadores y generadores sin IA. Comparar únicamente niveles manuales simples con niveles generados complejos mediría complejidad o herramienta, no participación de IA.

## 21. Calibración, población y tasa base

Una probabilidad tiene sentido solo respecto a una población, una definición de etiqueta y unas condiciones de captura. La investigación de Guo y colaboradores estudia el desacuerdo entre confianza y frecuencia real de acierto, y métodos de calibración. Aquí se toma esa distinción como requisito de evaluación; no se transfieren sus resultados numéricos a GD. [On Calibration of Modern Neural Networks](https://arxiv.org/abs/1706.04599)

Para un detector binario, π es prevalencia, TPR sensibilidad y FPR tasa de falsos positivos. El valor predictivo positivo depende de las tres cantidades.

```equation
(25) PPV = π TPR / [π TPR + (1−π) FPR]
```

Ejemplo hipotético: entre 10.000 niveles, π = 1 %, TPR = 90 % y FPR = 1 % darían 90 verdaderos positivos y 99 falsos positivos. De los 189 resultados positivos, aproximadamente el 47,6 % pertenecería a la clase objetivo. Estos números son aritmética de una simulación, no rendimiento de Paimbnails.

La calibración puede usar regresión isotónica sobre una partición separada. Se publica el número de observaciones por intervalo y su incertidumbre. Un histograma de confianza sin tamaño de muestra es incompleto.

```equation
(26) Brier = meanᵢ (pᵢ − yᵢ)²
```

El laboratorio de tasa base ilustra la fórmula con parámetros elegidos por el lector. Nunca aplica esos parámetros al nivel cargado ni muestra el resultado como probabilidad de ese nivel.

## 22. Abstención y herramientas desconocidas

El algoritmo debe abstenerse cuando el soporte es pequeño, el parser no entiende propiedades relevantes, el perfil pertenece a otra versión, la región es demasiado corta o el artefacto está lejos de las distribuciones conocidas. Un score alto en una población ajena no es evidencia fuerte.

```equation
(27) emitir inferencia ⇔ soporte ≥ n_min ∧ dominio válido ∧ error controlado ∧ sin contradicción material
```

Los umbrales se fijan con validación. n_min no es una constante universal: depende de la familia y el tipo de evidencia. Una firma de operación puede cubrir un solo asset; una señal léxica puede necesitar cientos de objetos.

Una distancia grande frente al corpus significa «fuera del dominio». No significa «nuevo modelo de IA». El prototipo se abstiene siempre de atribuir IA porque carece de ese corpus y su evaluación.

## 23. Autoría mixta y segmentación

La unidad de análisis preferida es una contribución. Un nivel puede contener gameplay manual, fondo generado, decoraciones de equipo, un GIF humano importado y triggers compilados mediante física. Una etiqueta global perdería esa información.

La segmentación propuesta combina capas, grupos, regiones espaciales y marcas temporales cuando se conocen. Segmentos solapados conservan sus referencias. Una región geométrica no equivale necesariamente a una unidad de autoría; un colaborador puede editar el mismo grupo que otro.

```equation
(28) cobertura_objetos = |objetos finales vinculados a operación documentada| / |objetos finales analizados|
```

Ese porcentaje describe un vínculo documentado, no «porcentaje de creatividad IA». Deben definirse identidad persistente de objetos, cambios posteriores, contribuciones derivadas y tratamiento de borrados. Un índice del array no es identidad persistente después de un nuevo guardado.

También pueden reportarse cobertura visual bajo un render fijado o número de operaciones aceptadas. Estos denominadores responden preguntas diferentes y no se promedian como si midieran lo mismo. El paper no inventa horas humanas a partir de objetos.

## 24. Procedencia voluntaria y manifiesto

Se propone un manifiesto adjunto al nivel, con declaración del creador y recibos por operación. «No declarado» es distinto de «declarado sin IA». La declaración incluye alcance, herramienta, tipo de intervención y edición posterior, admitiendo «desconocido» cuando corresponde.

```json
{
  "schema": "paimbnails.ai-provenance/0.1",
  "artifact": {
    "sha256": "<64 caracteres hexadecimales de la cadena exacta>",
    "hashScope": "utf8-decoded-level-string-exact-v1",
    "gdVersion": "<versión observada>"
  },
  "declaration": {
    "aiUse": "assisted",
    "scope": "background",
    "statement": "Se generó una imagen de fondo y después se editó manualmente."
  },
  "operations": [],
  "receipts": []
}
```

Este ejemplo es una plantilla y no está firmado. Se adjunta un JSON Schema para validar su forma. Validar un schema no valida veracidad, firma, identidad, confianza o completitud. Un manifiesto sin recibos mantiene el estado «declarado por el creador».

Las operaciones registran hash de entrada, hash de salida, familia de operación, herramienta, referencia de modelo si existe, alcance y operaciones padre. Los assets tienen sus propios hashes. Una entrada no disponible puede omitirse, dejando explícita la falta de cobertura; no se inventa su contenido. No se incrustan prompts privados por defecto.

## 25. Firma, confianza y cadena de operaciones

Para autentificar un recibo se propone firmar la canonicalización JCS de un payload definido. La firma se verifica con una clave pública obtenida de una política de confianza explícita. El recibo contiene la firma por separado; no se firma recursivamente a sí mismo.

```equation
(29) payload = UTF8(JCS(campos documentados del recibo sin firma))
(30) signature = Ed25519.Sign(privateKey, payload)
(31) aceptado = firmaVálida ∧ emisorConfiable ∧ vínculoExacto ∧ políticaVigente
```

Ed25519 está descrito en RFC 8032. La especificación aquí exige interoperabilidad y pruebas con vectores conocidos antes de implementar un verificador. El prototipo web 0.1 no verifica firmas ni administra claves. [RFC 8032: Edwards-Curve Digital Signature Algorithm](https://www.rfc-editor.org/rfc/rfc8032)

El campo operationSha256 se define como SHA-256 de UTF-8(JCS(objeto de operación completo)). Los inputHashes y outputHash de una operación son hashes de los bytes exactos de sus artefactos; una cadena descomprimida usa UTF-8 y un archivo de imagen usa sus bytes originales. Las representaciones derivadas requieren operaciones separadas. El payload firmado incluye todos los campos del objeto payload del schema publicado, incluido operationSha256. El verificador debe comprobar que operationId coincide con esa operación y que su hash, padres y vínculos son coherentes.

Una clave que llega dentro del mismo manifiesto no se considera confiable automáticamente. Un hash de entrada/salida no prueba que ocurrió una operación si nadie confiable la atestigua. Un reloj local no proporciona un timestamp confiable. Revocación, rotación de claves, recibos falsos y emisores comprometidos pertenecen al modelo de confianza.

La vinculación entre operaciones usa hashes de outputs/inputs y padres explícitos. Las colaboraciones necesitan varios padres; por eso se modela un DAG de procedencia y no una única cadena lineal. Una raíz de Merkle puede facilitar prueba de inclusión, pero no demuestra que no existan eventos omitidos. El registro debe describir qué partes del flujo observó.

## 26. Relación con C2PA y marcas de agua

C2PA define un marco de credenciales y afirmaciones de procedencia con validación criptográfica. Es una referencia útil para separar procedencia, firma y juicios sobre el contenido. Un manifiesto propio con nombres similares no se vuelve compatible con C2PA. La integración necesitaría seguir su especificación, sus formatos y su validación. [C2PA Technical Specification 2.2](https://spec.c2pa.org/specifications/specifications/2.2/specs/C2PA_Specification.html)

Para un asset generado, un recibo o credencial de ese asset se conserva como ingrediente de la conversión. Si el convertidor produce objetos, su recibo enlaza el hash del asset con el hash del resultado. Credenciales del asset sin ese vínculo no identifican automáticamente qué objetos del nivel proceden de él.

Kirchenbauer y colaboradores proponen una marca estadística durante generación de texto basada en preferencias de tokens y una prueba de detección. Eso supone un esquema de marca concreto; no demuestra que toda salida de toda IA esté marcada. Su aplicación a cadenas de objetos requeriría un diseño específico y evaluación de restricciones del formato. [A Watermark for Large Language Models](https://arxiv.org/abs/2301.10226)

La marca geométrica de Paimbnails se introdujo después de planificar una conversión y no autentifica un modelo. Una marca puede desaparecer por edición o aparecer por copia. Ausencia de marca y ausencia de credenciales nunca prueban ausencia de IA.

## 27. Por qué no copiar DetectGPT directamente

DetectGPT compara probabilidades del modelo y perturbaciones para estudiar curvatura de log-probabilidad en texto. El método presupone acceso a probabilidades del modelo de interés y utiliza otro modelo para perturbaciones. No es un algoritmo universal aplicable directamente a una cadena GD sin recursos adicionales. [DetectGPT: Zero-Shot Machine-Generated Text Detection using Probability Curvature](https://arxiv.org/abs/2301.11305)

Una perturbación aleatoria de claves o IDs puede invalidar un nivel o cambiar su significado. Un cambio «pequeño» en texto puede alterar un trigger crítico. Un serializador muy restringido puede dar alta probabilidad a cadenas humanas y artificiales por igual.

Por ello PTL no usa perplejidad de prosa como prueba de IA en objetos ni adopta números de precisión de noticias como si fueran precisión en Geometry Dash. Si se investiga una señal de probabilidad en este dominio, se trata como experimento separado, con modelos concretos, licencias, esquema válido y dataset propio.

## 28. Modelo de amenazas y transformaciones

La transparencia debe seguir siendo útil bajo ediciones normales y también describir manipulación. Reordenar propiedades, guardar en el editor, renumerar grupos, trasladar objetos, copiar una región o añadir ruido puede cambiar unas señales y conservar otras.

| Transformación | Qué puede cambiar | Resultado correcto |
| --- | --- | --- |
| Guardado con otro exportador | Orden de claves y decimales | No tratar cambio de emisor como cambio de autoría |
| Normalización de rotaciones | Vueltas adicionales de la marca | Marca no observada; origen indeterminado |
| Reordenamiento de objetos | Parejas consecutivas y ejecución en casos relevantes | Documentar sensibilidad; no prometer invariancia |
| Copia de marca conocida | Compatibilidad de importador | No autentificar emisor ni IA |
| Renumeración consistente de grupos | IDs literales | Comparar relaciones con esquema adecuado |
| Edición humana posterior | Hash exacto y parte de la geometría | Vincular una nueva operación, conservar alcance previo |
| Eliminación de manifiesto | Procedencia disponible | Indeterminado, sin presunción contra el creador |
| Recibo firmado por clave arbitraria | Apariencia de autenticación | Rechazar confianza implícita |
| Obra manual muy procedural | Rasgos estadísticos | Contrastar con H₀ adecuado y admitir abstención |
| Modelo/proveedor no visto | Distribución nueva | Fuera del dominio; modelo no atribuible |

El análisis no ejecuta instrucciones escritas en textos del nivel. Las cadenas se tratan como datos. La interfaz muestra contenido con textContent y no interpreta propiedades como HTML. Se interrumpe un Worker al cancelar o superar el tiempo establecido.

## 29. Privacidad y minimización

El laboratorio analiza localmente. Seleccionar un archivo o pegar una cadena no la envía al servidor ni a un proveedor de IA. El resultado permanece en memoria hasta reemplazarlo o limpiar la sesión; la descarga ocurre solo cuando la persona la solicita.

En un sistema futuro con recibos, la publicación debe admitir pseudónimos, campos opcionales y alcance agregado. No se exige publicar conversaciones, prompts íntegros ni razonamiento interno. Un hash no anonimiza necesariamente un asset: si otra persona posee el original puede compararlo.

Los registros operativos pueden contener información comercial o de colaboradores. Su divulgación necesita una decisión consciente del creador y una definición de permisos de equipo. El prototipo no crea cuentas, telemetría de niveles, ranking de sospecha ni un historial persistente.

## 30. Construcción del corpus

La evaluación propuesta reúne cuatro cohortes: creación manual documentada, automatización sin modelo aprendido, participación generativa documentada y niveles mixtos con alcance conocido. La procedencia de las etiquetas es más importante que su apariencia visual.

No se etiqueta un nivel como IA porque otro detector lo diga. Las etiquetas deben venir de sesiones consentidas, herramientas instrumentadas o fuentes con evidencia verificable. Los niveles de origen incierto se mantienen separados para evaluación abierta, no se fuerzan como humanos o IA.

Se registran estilo, longitud, complejidad, versión GD, plataforma, exportador, presupuesto de objetos y colaboración. Las salidas de cada modelo se obtienen con variantes de prompts y condiciones. Se emparejan tareas para que complejidad y herramienta no predigan trivialmente la clase.

La separación de entrenamiento, calibración y test se realiza por creador, sesión, nivel base, template, asset y familia de generador. Duplicados, remix y derivados permanecen en la misma partición. Compartir una plantilla entre train y test puede inflar la precisión.

No se presenta una cifra universal de muestras suficiente. Un estudio piloto estima variabilidad y prevalencia; un cálculo de potencia fija el tamaño requerido para el objetivo de error, antes de observar el test final.

## 31. Protocolo de evaluación

El test final se congela y se publica su procedencia permitida, hashes, splits, reglas y scripts. Se evalúan métricas por cohorte y por familia; la media global puede ocultar falsos positivos sobre importadores convencionales.

```equation
(32) TPR = TP/(TP+FN); FPR = FP/(FP+TN)
(33) precisión = TP/(TP+FP); recall = TPR
(34) cobertura_decisión = decisiones_emitidas / casos_evaluables
```

Se reportan curvas ROC y PR, intervalos de confianza, calibración, error entre decisiones emitidas, abstención y tiempo/memoria. Para regiones se evalúan localización y alcance. Para atribución de modelo se incluye la opción «desconocido» y se prueba dejando familias completas fuera del entrenamiento.

Las transformaciones del apartado 28 se aplican antes de medir supervivencia. Un test de reordenar objetos no basta para demostrar robustez al guardado en GD. Los casos críticos requieren versiones y plataformas reales del editor.

Se realizan ablaciones: quitar rasgos léxicos, quitar geometría, quitar grupos y quitar perfiles de herramientas. Si desaparece el rendimiento al cambiar exportador, el sistema aprendió principalmente la huella del exportador. Esa conclusión se publica.

Para cero falsos positivos observados en n negativos independientes, el límite superior unilateral aproximado al 95 % es 3/n. Es una aproximación útil, no un sustituto de intervalos exactos ni una garantía en otra población.

```equation
(35) FPR_superior ≈ 3/n cuando se observan 0 falsos positivos
```

Por ejemplo, 300 negativos sin errores solo permiten aproximadamente un límite del 1 %, no «0 % de falsos positivos». La correlación por creador o plantilla reduce el tamaño efectivo de la muestra.

## 32. Ejemplos interpretados con alcance

| Caso | Observación | Informe defendible |
| --- | --- | --- |
| Persona copia 500 bloques a rejilla | Q alto y repetición alta | Construcción regular; participación IA indeterminada |
| Dibujo humano pasado al importador | Marca compatible y primitivas | Compatible con conversión de imagen; no demuestra IA |
| Imagen generativa sin credenciales convertida | Misma marca del caso anterior | Origen del asset indeterminado desde la cadena |
| Imagen generativa con recibo y conversión vinculada | Dos operaciones verificadas con hashes | Operación IA documentada en fondo, con confianza y alcance explícitos |
| Agente decora usando un template humano | Registro de llamada y patch aplicado | Asistencia IA en la operación; procedencia del template separada |
| Simulación convencional emite triggers | Grafo regular y emisor conocido | Automatización convencional compatible; IA no atribuible |
| Modelo escribe la misma cadena que una persona | Bytes idénticos | No distinguible por análisis del archivo solo |
| Nivel mixto editado después de la generación | Hash nuevo e historial parcial | Contribución documentada y edición posterior; completitud desconocida |

Estas son situaciones de diseño, no un benchmark de niveles reales. Los ejemplos del laboratorio son fixtures sintéticos de mecánica y parser. No se deben usar para estimar sensibilidad, especificidad o prevalencia de IA en la comunidad.

## 33. Laboratorio reproducible

El inspector permite pegar una cadena o cargar un archivo de texto. Los ejemplos «rejilla», «marca del importador» y «triggers» muestran que regularidad, marcas y grupos responden preguntas diferentes. Todos terminan con participación de IA indeterminada.

La vista incluye distribución de IDs, rejillas candidatas, patrones léxicos, repetición simple, grupos, evidencias de compatibilidad y hashes. La descarga JSON conserva la versión y las limitaciones del analizador. El mapa representa centros de objetos en coordenadas del nivel; no es un render y no representa escala, capas ni comportamiento.

El simulador de tasa base permite cambiar prevalencia, sensibilidad y FPR en una población ficticia de 10.000 casos. La gráfica separa verdaderos y falsos positivos. No está conectado a las métricas del inspector.

La ejecución se puede cancelar y la entrada se puede borrar. El código del núcleo y del Worker se publica como archivos descargables. Puede inspeccionarse y ejecutarse fuera de esta página en un entorno con las APIs de texto, criptografía y descompresión necesarias.

## 34. Informe e interfaz de producto

El informe ordena la información por fuerza de evidencia: operaciones verificadas con alcance, declaración voluntaria, compatibilidad de herramientas y métricas descriptivas. La conclusión siempre muestra sus límites. La interfaz nunca sustituye «compatible» por «confirmado».

Una presentación posible sería: «El creador declara asistencia IA en la imagen de fondo. La cadena contiene patrones compatibles con el importador de Paimbnails. No se verificó un recibo del modelo. Gameplay y decoración restante: procedencia no determinada».

Si solo se analiza la cadena: «Se observaron 2.400 objetos y patrones repetidos. Eso también ocurre con templates y herramientas convencionales. No hay evidencia suficiente para atribuir uso de IA».

La revisión humana puede corregir alcance o adjuntar evidencia. Una corrección conserva versión y motivo; no construye un historial público de acusaciones. No se publican porcentajes de «sospecha» ni listas de creadores marcados por regularidad geométrica.

## 35. Plan de integración y criterios de salida

La primera fase publica metodología, código y límites. La segunda documenta adaptadores de formato y obtiene fixtures reales consentidos. La tercera instrumenta operaciones de herramientas para recibos voluntarios. La cuarta reúne corpus y evalúa inferencias estadísticas. La quinta ofrece etiquetas de procedencia con revisión y correcciones.

Cada fase tiene una condición de salida verificable. Un parser necesita fixtures y pruebas de errores. Un recibo necesita interoperabilidad criptográfica, política de confianza y pruebas de vínculo al artefacto. Un clasificador necesita test independiente, calibración, comparación con herramientas convencionales y abstención. Una interfaz necesita comprobar comprensión de las etiquetas con creadores.

El sistema solo pasa de «propuesta» a «validado en esta población» cuando existe evidencia publicada del estudio. La publicación de este paper no convierte la detección de IA en una función disponible del mod.

La creación asistida por IA sigue siendo una opción legítima del creador. El resultado esperado es una descripción más precisa del proceso y de lo que no se puede saber, con información verificable cuando las herramientas la proporcionan.

## 36. Glosario

| Término | Definición usada en este paper |
| --- | --- |
| Artefacto | Cadena, nivel o asset concreto observado |
| Autoría mixta | Contribuciones humanas, procedurales y de modelos en un mismo proyecto |
| Procedencia | Información sobre entradas, operaciones y origen con alcance |
| Huella léxica | Rasgos del texto serializado, como orden y decimales |
| Compatibilidad | Coincidencia con una regla; no autentifica origen |
| Marca de agua | Señal introducida por un procedimiento específico |
| Recibo | Registro de una operación y su vínculo entre entradas y salidas |
| Firma | Mecanismo criptográfico que autentifica un payload respecto a una clave |
| Confianza | Política sobre emisores, claves, vigencia y revocación |
| Calibración | Relación evaluada entre predicción y frecuencia de acierto |
| Abstención | Decisión de no atribuir por falta de evidencia adecuada |
| FPR | Fracción de negativos etiquetados erróneamente como positivos |
| Tasa base | Prevalencia de la clase en la población objetivo |
| H₀ | Procesos convencionales de referencia sin participación generativa objetivo |
| H₁ | Participación generativa en el alcance definido |
| Fuera del dominio | Caso no representado adecuadamente en la evaluación |
| Traza de operaciones | Eventos observables; no pensamiento interno |

## 37. Referencias y reproducibilidad

Las fuentes externas justifican conceptos concretos; ninguna presenta un detector validado de IA para niveles de Geometry Dash. Las fórmulas y reglas PTL son una propuesta de este documento salvo atribución explícita. Los límites y el algoritmo ejecutable se pueden comparar directamente con el código publicado.

1. Paimbnails, código inspeccionado: revisión a2419d80371e113e4f274c2998262f40dd91839f. Ver inventario de archivos y hashes publicado junto a esta edición.
2. Sadasivan et al. [Can AI-Generated Text be Reliably Detected?](https://arxiv.org/abs/2303.11156). Límites y ataques en detección de texto.
3. Turpin et al. [Language Models Don't Always Say What They Think](https://arxiv.org/abs/2305.04388). Fidelidad de explicaciones de razonamiento.
4. Vaswani et al. [Attention Is All You Need](https://arxiv.org/abs/1706.03762). Arquitectura transformer.
5. Ho, Jain y Abbeel. [Denoising Diffusion Probabilistic Models](https://arxiv.org/abs/2006.11239). Generación por difusión.
6. Summerville et al. [Procedural Content Generation via Machine Learning](https://arxiv.org/abs/1702.00539). Marco de generación de contenido mediante aprendizaje.
7. Kirchenbauer et al. [A Watermark for Large Language Models](https://arxiv.org/abs/2301.10226). Marca estadística de texto.
8. Mitchell et al. [DetectGPT](https://arxiv.org/abs/2301.11305). Detección de texto mediante curvatura de probabilidad.
9. Guo et al. [On Calibration of Modern Neural Networks](https://arxiv.org/abs/1706.04599). Confianza y calibración.
10. C2PA. [Technical Specification 2.2](https://spec.c2pa.org/specifications/specifications/2.2/specs/C2PA_Specification.html). Procedencia y validación de credenciales; se fija esta versión para reproducibilidad, sin afirmar que sea la más reciente.
11. IETF. [RFC 8785, JSON Canonicalization Scheme](https://www.rfc-editor.org/rfc/rfc8785). Canonicalización del payload de firmas propuesto.
12. IETF. [RFC 8032, EdDSA](https://www.rfc-editor.org/rfc/rfc8032). Especificación de firmas Ed25519.

Para citar esta edición: Paimbnails. «Transparencia de IA en niveles de Geometry Dash: análisis de cadenas, procedencia e incertidumbre». Paper técnico del proyecto, propuesta metodológica y prototipo 0.1, 12 de octubre de 2026. https://flozwer.org/paimbnails/papers/ai-level-transparency

**Transparencia de este documento:** redacción, diseño del algoritmo y prototipo preparados con asistencia de IA a petición del responsable del proyecto. El documento declara esa asistencia por coherencia con su objetivo. No se atribuye validación empírica, revisión científica o resultados de autoría al hecho de haber sido redactado.
