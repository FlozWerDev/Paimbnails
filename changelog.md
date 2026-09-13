# <cy>v1.1.2</c>

![](flozwer.paimbnails2/paim_Paimon.png?height=32) Llega <cl>Paimon RTX</c>: trazado de rayos en tiempo real sobre todo el juego, y la <cl>Interpolacion de Fotogramas</c>, que dibuja entre pasos de fisica para quitar los micro-tirones. Llegan tambien los <cl>Niveles de Perfil</c>: XP, rangos e insignias calculados con las estadisticas publicas de cualquier jugador. Ademas, <cl>Level Thumbnails</c> deja de ser incompatible y se puede tener puesto a la vez. Y abre <cl>Paimon Versus</c>: duelos 1v1 con rango propio, sobre el boton Versus que el juego tenia sin usar.

- <cg>Transiciones rehechas desde cero</c> sobre el ciclo de vida nativo de Cocos, sin mover los nodos de las escenas ni escribir el puntero interno de escena de GD. El editor admite una linea de tiempo determinista, grupos de comandos simultaneos y previews que regresan al editor conservando los cambios.
- Nuevo modo <cg>Stinger al estilo OBS</c>: importa imagenes, GIF o videos, los prepara como spritesheets paginados y permite elegir por separado la duracion y el punto exacto en que cambia la escena. GIF y PNG conservan transparencia; el backend de video actual reproduce imagen sin audio ni alpha.
- La importacion de medios se hace fuera del hilo de juego, con cache persistente, precarga de los perfiles elegidos y limites de memoria. Durante una transicion solo se cambia el rectangulo del spritesheet; no se decodifica ni se suben texturas por fotograma.
- Nuevo <cg>Custom Hover</c> para animar botones al pasar el cursor o tocarlos, con presets, configuracion por boton y una opcion para vincular todos los botones.
- Corregida la compilacion de macOS/iOS que impidio publicar inicialmente esta version: el seguimiento del teclado usa ahora el evento portable de Geode en vez de modificar `CCKeyboardDispatcher` en plataformas donde ese binding no se puede modificar.

---

## ![](frame:GJ_starBtn_001.png?height=18) <cy>Paimon Versus</c>

- El boton <cg>Versus</c> del menu de creador, ese que solo abria un "proximamente", pasa a abrir un <cg>hub de duelos 1v1</c>. Se reviste con arte propio y lleva en la esquina un <cg>chip con tu rango</c>, asi que la escalera se lee desde el menu sin entrar. Si otro mod se lleva ese boton por delante, se anade uno nuevo en la misma fila.
- El hub va en <cg>tres columnas con nombre</c> - tu rango, el formato y el resto - en vez de una rejilla de iconos sin rotular. Cada formato es una fila con su glifo, su nombre, a que se gana y cuanto dura, y debajo quedan las dos maneras de entrar, <cg>clasificatoria</c> y <cg>amistoso</c>, cada una diciendo lo que se juega. Arriba a la derecha se lee si <cg>Globed esta conectado</c>, que es lo unico que la cola clasificatoria exige.
- Los <cg>amistosos ya se juegan</c>, sin esperar a ninguna fase: se abre una sala con un <cg>codigo de seis letras</c> que se copia y se pasa, o se escribe el codigo o el nombre del rival. No mueven el Elo y admiten cualquier formato, incluido <cg>Amistoso</c>, que no impone reglas. Con el hub abierto se vigila la sala, asi que un reto que llega de fuera abre el duelo solo, y la revancha de un amistoso vuelve a la misma persona en vez de a la cola.
- La ficha del duelo ensena los <cg>tres pasos</c> - aceptar, vetar, jugar - con el que toca encendido, las dos fichas enfrentadas con su rango y su record, y si la partida <cg>mueve el Elo o no</c>. Ese dato lo manda el servidor, asi que un amistoso jugado en formato Carrera ya no se anuncia como clasificatoria al terminar.
- <cg>Dos escaleras independientes</c>, clasico y plataforma, cada una con su Elo, su historial y su colocacion. Son habilidades distintas y mezclarlas rompe el emparejamiento.
- El rango VS es un <cg>sub-rango</c> del sistema de niveles de perfil: reusa los mismos veinte rangos y colores, con <cg>divisiones IV a I</c> hasta Paragon y sin ellas por encima. La insignia es la medalla del rango dentro de un marco de laurel con espadas cruzadas.
- <cg>Cinco duelos de colocacion</c> por modo antes de ver un numero, con el factor K a 48 para que cinco partidas basten. A partir de ahi baja a 32, luego a 16 y a 12 en lo mas alto. Una racha de tres suma un 15% y ganar por mucho margen hasta otro 20%, sin pasar nunca de un 35% extra.
- <cg>Diez formatos</c>: carrera, muerte subita, mejor intento, contrarreloj, escalera por tramos, ruleta, tira y afloja, rey de la colina, relevos 2v2 y amistoso. Cada uno dice a que se juega y que se manda por la red.
- <cg>Ruleta</c>: cada cierto porcentaje te toca una carta, buena para ti o mala para el rival. Los hitos los sortea el servidor y los dos clientes los reconstruyen con la misma semilla, asi que la carta cae sin ida y vuelta. Quien va por detras saca mejores cartas, hasta un 35% de desplazamiento en la tabla de rarezas.
- <cg>24 cartas en cuatro rarezas</c>, con arte propio tenido por rareza. Ninguna toca la fisica, las hitboxes, la geometria ni la velocidad: solo camara, capas sobre la pantalla, audio, HUD, checkpoints y escudos. Una carrera con cartas sigue siendo una carrera legitima.
- El tiempo real dentro del nivel va por los <cg>eventos de servidor de Globed</c>, que ya tiene una sesion abierta entre los dos jugadores: cuatro eventos binarios, dirigidos solo al rival, con un tope de ocho por segundo. Globed es <cg>dependencia opcional</c>; sin el se juega igual, solo que sin ver al rival.
- Todo lo que hay que recordar - cola, Elo, historial, temporadas, la lista de niveles - vive en un <cg>servidor propio</c>, que se identifica con el mod-code que el mod ya emite. No hay cuenta nueva ni pantalla de registro.
- <cg>Clasificacion</c> por modo, con global, amigos y temporada, y tu fila resaltada. Al lado, el <cg>historial</c> de tus ultimos veinte duelos con el rival, el formato y el Elo que se movio.
- En cualquier perfil aparece un <cg>chip con el rango VS</c> junto al del nivel; al tocarlo salen las dos escaleras del jugador con su record, y en el de otro un boton de <cg>retar</c> que le abre el duelo directamente por su nombre.
- Ganar un duelo da <cg>XP al nivel de perfil</c>: 60 de base mas 2 por punto de Elo, con tope de 600 al dia, y 15 por jugar aunque pierdas. Entra como una fuente mas en la ficha de progresion, junto a estrellas, demons y maestria, y vale 0 para quien nunca haya jugado uno.
- La <cg>Ruleta</c> funciona: el servidor sortea entre seis y nueve hitos y manda la semilla antes de la cuenta atras, los dos clientes reconstruyen la misma lista, y cruzar un hito reparte carta sin ida y vuelta. La mano son <cg>dos huecos</c> y se lanzan con <cg>Q</c> y <cg>E</c>, rebindables desde los ajustes.
- Las cartas se aplican de verdad: niebla y escarcha como capas sobre el HUD, terremoto con la sacudida de camara del propio juego, zoom y espejo sobre la capa de objetos reescritos cada fotograma, lastre ocultando el porcentaje, interferencia silenciando musica y efectos, antifaz escondiendo tu icono, escudo por el modo seguro de Globed, baliza y calavera sobre la pila de checkpoints, y rebote devolviendo la siguiente carta ofensiva. Todo con <cg>caducidad dura</c>: si el rival se desconecta con una carta puesta, el temporizador la retira igual.
- <cg>Temporadas</c> de ocho semanas, con su ficha propia: el numero, lo que queda, tu mejor rango y que le pasa al Elo al cerrar. El reset suave y la decadencia por inactividad los aplica un cron nocturno en el servidor.
- <cg>Mutadores rotativos</c> una semana si y otra no: <cg>sin practica</c> y <cg>sin pausa</c>. Solo se anuncian los que el duelo hace cumplir de verdad.
- Se apaga entero, o por partes (HUD y cartas aparte), desde <cg>Modulos > Menu > Versus</c>.

---

## ![](frame:GJ_bigStar_001.png?height=18) <cy>Niveles de Perfil</c>

- Cada perfil pasa a tener <cg>nivel, rango e insignias</c>, sacados de las estadisticas publicas que el juego ya ensena. Junto al nombre aparece un chip con la insignia del rango y el nivel; al tocarlo se abre la ficha completa, tanto en tu perfil como en el de cualquiera.
- <cg>XP por estadistica</c>: 5 por estrella, 6 por luna, 2 por diamante, 20 por moneda de usuario, 100 por moneda secreta y 1500 por punto de creador. Los <cg>demons pagan segun su dificultad</c> - de 100 el facil a 800 el extremo, y los de platformer un cuarto mas - leyendo el desglose que el servidor manda de cualquier usuario, en vez de cobrar lo mismo por todos. Los semanales, los de gauntlet, los map packs y las dificultades altas suman extra por encima.
- <cg>200 niveles repartidos en 20 rangos</c>, de Wanderer a Paimon. Cada rango cambia el color, la silueta de la medalla (disco, escudo, hexagono, estrella, corona) y los efectos: brillo desde el sexto, destello desde el noveno, latido desde el trece, chispas desde el dieciseis y satelites en los tres ultimos.
- <cg>Toda la ficha usa la interfaz nativa de GD</c>. Las medallas de rango, las placas de insignia y los brillos son <cg>arte propio</c> pintado en blanco y negro para que el juego lo tinte con el color del rango o de la rareza, y todas las barras salen del <cg>asset de barra de progreso del juego</c>, cada una con su color. Las pestanas son las de GD y las tarjetas son paneles <cg>GJ_square</c>. Ya no queda nada dibujado a mano con poligonos, que era lo que hacia que la ficha desentonara con el resto del juego.
- Las insignias pintan <cg>dos colores a la vez</c>: el marco lleva el de la rareza y la cara hundida de detras el de la categoria, asi que <cg>cada una de las 124 se ve distinta</c> de un vistazo. Hay <cg>un marco por rareza</c> - liso, achaflanado, con remaches, con gemas en los cuatro lados, con corona y banda, y con doce puas - y <cg>doce colores de categoria</c>. Abajo va el objetivo abreviado (<cg>500</c>, <cg>35K</c>, <cg>#100</c>) para separar las dos de la misma familia que comparten rareza.
- Los iconos de dificultad pasan a las caras <cg>sin el rotulo</c> que el juego usa en las listas: a tamano de casilla el texto "DEMON" era una mancha y las caras ya se distinguen solas por color.
- <cg>124 insignias</c> en doce categorias - camino, estrellas, lunas, diamantes, monedas, demons, extremos, platformer, eventos, maestria, creador y ranking - con seis rarezas. Las bloqueadas van en gris con candado, las desbloqueadas de rareza epica para arriba llevan brillo propio, y el detalle de cada una ensena la barra de cuanto falta.
- La ficha tiene tres pestanas que se deslizan: <cg>Resumen</c> (medalla grande con anillo de progreso, barra de XP y el siguiente rango), <cg>Fuentes</c> (de donde sale cada XP, ordenado y con barras) e <cg>Insignias</c> (rejilla filtrable por categoria, con detalle al tocar).
- <cr>El desglose de demons se cobraba aunque la cuenta no tuviera ni uno</c>. Es una cadena que manda el servidor y se guardaba tal cual, asi que un desglose viejo o de otra cuenta repartia XP e insignias de demons a quien no habia pasado ninguno. Ahora manda el <cg>total de demons que lleva el propio juego</c>: si el desglose cuenta mas de los que hay, se tira entero. Al perfil afectado le baja el XP a lo que de verdad tiene, sin tarjeta de subida de nivel.
- Al terminar un nivel entra una <cg>tarjeta por la izquierda</c> con el XP ganado y la barra llenandose. Si cruzas de nivel la barra llega al tope, destella y sigue en el siguiente, la insignia estalla y el numero cambia solo. Las insignias nuevas entran despues en su propia tarjeta. No bloquea la pantalla ni tapa los botones del final de nivel.
- Sale igual con <cl>Profile Redesign</c> puesto o quitado, y se apaga entero desde <cg>Modulos > Perfil > Profile Levels</c>.

---

## ![](frame:GJ_colorBtn_001.png?height=18) <cy>Paimon RTX</c>

- Postproceso de <cg>trazado de rayos en espacio de pantalla</c> sobre todo el juego. Del fotograma ya dibujado se deduce la superficie (relieve por el contraste, normales por sus derivadas, emisores por el brillo) y se marchan rayos sobre ella para sacar <cg>luz rebotada con el color del objeto que la refleja</c>, <cg>oclusion de contacto</c> y <cg>reflejos</c>. Encima van bloom por cadena de niveles, rayos de luz volumetricos, mapeo de tonos (ACES, Filmico, Uncharted 2, Reinhard), balance de color y lente (aberracion cromatica, vineteado, grano, nitidez). Se configura entero en <cg>Extras > Paimon RTX</c>: 58 controles repartidos en cinco pestanas.
- <cg>Reescrito el muestreo y el filtrado</c>. La <cr>estatica que se veia en toda la pantalla</c> venia de rotar el abanico de rayos con ruido blanco: cada pixel elegia un giro sin relacion con sus vecinos. Ahora las direcciones van <cg>estratificadas</c> (el rayo i cubre el sector i de la circunferencia, no uno al azar) y el abanico se rota con <cg>Interleaved Gradient Noise</c>, que reparte los giros de forma ordenada dentro de cada bloque 3x3 y deja el error donde el filtro se lo puede comer.
- Filtro <cg>a-trous de varias pasadas</c> con corte por bordes, en vez de un solo desenfoque 3x3. Cada pasada dobla su alcance, asi que tres cubren **15x15** pixeles con **27** muestras en lugar de 225.
- El historial temporal ahora se <cg>reproyecta con el movimiento real de la camara</c>, y se recorta contra media mas menos desviacion del vecindario en vez de contra su minimo y maximo. Eso permite subir la realimentacion del **35%** al **85-90%** sin que arrastre estelas, y es de donde sale la mayor parte de la limpieza.
- <cr>Los presets estaban al reves</c>. <cg>Ultra</c> subia los rayos pero bajaba el filtrado, asi que salia mas ruidoso que <cg>Rendimiento</c>; 8 rayos siguen siendo muy pocos para integrar la luz por fuerza bruta. Ahora cada escalon sube muestras y filtrado a la vez, y los presets guardados se migran solos al abrir el juego.
- Los pasos de cada rayo crecen en <cg>progresion geometrica</c>. Los primeros caen muy juntos (contacto nitido) y los ultimos se separan (alcance largo barato), asi que **14** pasos llegan mas lejos que los **26** uniformes de antes.
- <cr>Toda la cadena trabajaba sobre la senal de pantalla</c>, no sobre luz. Sumar, desenfocar y promediar valores en sRGB es mezclar numeros que no son cantidades de luz: de ahi salian los <cr>halos lechosos</c> y los grises sucios. Ahora se pasa a <cg>lineal</c> para hacer las cuentas y se vuelve al final, con un par de curvas que se cancelan exactamente.
- <cr>El mapeo de tonos se aplicaba encima de una imagen que el juego ya habia mapeado</c>. Pasar ACES otra vez sube los medios tonos y deja el blanco en **0.8**, asi que la pantalla salia <cr>gris y lavada</c> aunque estuviera todo a cero. Como hacen los inyectores de postproceso sobre juegos LDR, ahora se aplica primero la <cg>inversa</c> del mapeo para volver a un rango alto plausible, se suma ahi la luz, y se mapea al final: con los efectos apagados la imagen sale <cg>identica</c> a la del juego.
- La piramide del bloom pasa a los filtros de <cg>Jimenez (Call of Duty: Advanced Warfare)</c>: **13** muestras al bajar y carpa de **9** al subir, en vez de un box de 4 que se comia tres cuartas partes de los pixeles y dejaba el halo <cr>latiendo</c> en cuanto un brillo cruzaba medio texel. El primer nivel promedia ademas por <cg>Karis</c>, que es lo que impide que un unico pixel muy brillante salga parpadeando como una luciernaga.
- Al recomponer ya no se suman los niveles, se <cg>interpolan</c>. Sumando, la energia se multiplicaba por el numero de pases: la misma fuerza daba un halo <cr>cinco veces mas fuerte</c> con cinco pases que con uno. Ahora los pases solo cambian la anchura, con controles nuevos de <cg>rodilla</c> (el corte del umbral deja de ser una linea dura), <cg>anchura</c> y <cg>anamorfico</c>.
- El bloom se alimenta tambien de la <cg>luz trazada</c>, no solo de la imagen: antes lo unico de la pantalla que no brillaba era justo lo que iluminaba RTX.
- La <cg>oclusion</c> deja de ensuciar lo que emite - un cartel encendido no se apaga por tener una pared cerca -, la <cg>aberracion cromatica</c> se abre con el cuadrado del radio (centro limpio, esquinas separadas) en vez de por toda la pantalla, la <cg>nitidez</c> usa el nucleo de <cg>CAS</c> y ya no deja cerco blanco en los bordes, el <cg>grano</c> vive en los medios tonos y el <cg>vineteado</c> respeta la relacion de aspecto. La salida lleva <cg>ruido de dithering</c> de un LSB, que es lo que quita las bandas de los degradados.
- Nueva <cg>exposicion automatica</c> opcional, al estilo de la adaptacion de MagicBloom y ENB: mide el brillo medio de la pantalla en el mip mas alto y persigue el gris medio con la inercia que se le ponga. Apagada por defecto.
- Los ajustes de color guardados se <cg>migran</c>: las fuerzas y el balance de antes eran numeros de otro espacio y reusarlos dejaria la imagen lavada, asi que vuelven a los valores por defecto. Coste, ruido y ambito se conservan.

---

## ![](frame:GJ_sTrendingIcon_001.png?height=18) <cy>Interpolacion de Fotogramas</c>

- Nueva funcion en <cg>Extras > Interpolacion</c>. GD simula a <cg>240 pasos por segundo</c> y dibuja cuando toca refrescar, asi que cada fotograma ensena un estado que lleva parado entre cero y un paso entero. Ese resto cambia de fotograma en fotograma y es lo que se ve como <cr>micro-tirones</c> a 144, 165 o 240 Hz. Ahora se guarda el estado de los dos ultimos pasos y se dibuja el punto intermedio que pide el reloj real, asi que el movimiento avanza lo mismo en cada fotograma.
- Se interpolan la <cg>camara</c> (posicion, zoom y rotacion), el <cg>fondo, el plano medio y las dos capas de suelo</c>, los <cg>dos jugadores</c> y, si se activa, los <cg>objetos que mueven los triggers</c>. Cada grupo tiene su propio interruptor y los objetos llevan tope para que un nivel cargado no se descontrole.
- Tres modos de <cg>latencia</c>: <cg>Suave</c> deja un paso de fisica de retraso (unos <cg>4 ms</c>) y no adivina nunca, <cg>Equilibrado</c> deja medio y <cg>Sin retardo</c> dibuja el presente exacto prolongando el ultimo paso. La <cg>fuerza</c> mezcla entre lo que dibujaria el juego y la interpolacion completa.
- No toca la simulacion: los valores buenos se restauran en cuanto termina el dibujado, asi que <cg>fisica, replays y porcentajes salen identicos</c>. Los saltos (portales de teletransporte, checkpoints, el suelo dando la vuelta, zooms instantaneos) se detectan por velocidad y ese fotograma se dibuja sin interpolar para que no se arrastren.

---

## ![](frame:GJ_optionsBtn_001.png?height=18) <cy>Rendimiento</c>

- <cl>Paimon RTX</c>: menos coste sin tocar la imagen. El trazado tiene <cg>tope de 1280 px</c> de lado largo (a 1440p y 4K la escala al 100% se disparaba sin que se notara), los reflejos usan un **40%** menos de pasos que el rebote difuso, y la aberracion cromatica solo lee de mas cuando esta puesta.
- <cl>Paimon RTX</c>: pensado para que no cueste FPS. El trazado corre a una fraccion de la pantalla (del **20%** al **100%**), con <cg>calidad adaptativa</c> que baja la resolucion sola si no llegas a los FPS objetivo, salto de fotogramas, acumulacion temporal con recorte anti-estela y cuatro presets (**Rendimiento**, **Equilibrado**, **Calidad**, **Ultra**). Eliges tambien donde se aplica: nivel, menus, editor, y si sigue trazando en pausa.

---

## ![](frame:GJ_hammerIcon_001.png?height=18) <cy>Editor</c>

- <cl>Autobuild</c>: <cg>analizar un nivel por id</c>. Escribes el id (o usas el nivel abierto), Autobuild lo descarga y lo lee como lo leerias tu: <cg>capa Z</c>, si el objeto cae en la rejilla de 30, su escala, su giro y que canal de color lo pinta. Con eso separa <cg>estructura, decoracion, fondo, primer plano y triggers</c>, y saca la paleta diciendo que pinta cada canal. Un fondo hecho con los mismos bloques que el suelo <cr>ya no pasa por estructura</c>, porque la capa que le puso el autor pesa mas que el parecido.
- <cl>Autobuild</c>: <cg>mineria de motivos</c>. En un nivel real todo el playfield es una sola mancha conectada, asi que buscar formas repetidas por componentes no encuentra nada. Ahora se compara el <cg>vecindario de cada objeto</c> y se agrupan los que se repiten, ordenados por <cg>rareza</c>: un pilar con un pincho encima dice mas del nivel que cinco baldosas mas de suelo. Cada forma repetida sale como plantilla lista para construir, y los <cg>triggers nunca se copian</c>.
- <cl>Autobuild</c>: <cg>editor de plantillas</c> en el engranaje de cada una. Quita de golpe todo lo de un tipo (pinchos, adornos, triggers), cambia el <cg>peso</c> de cada pieza, la duplica o la borra, abre y cierra sus <cg>ocho bordes</c>, y mueve los canales de color sin tocar los fijos. Cada pieza se ve dibujada y nada se escribe hasta pulsar Guardar.
- <cl>Autobuild</c>: si algun objeto queda mal clasificado, una linea <cg>id tipo</c> en <cp>config/autobuild/objects.txt</c> lo corrige sin recompilar.
- <cl>Simulador de Fisicas</c>: <cr>la vista previa ensenaba una cosa y el nivel hacia otra</c>. Los cuerpos con salida por <cg>triggers</c> no corren un solver dentro de GD, corren el grafo de Advanced Follow, y aun asi la vista los dibujaba con el solver del lab. Ahora <cg>cada cuerpo se dibuja con el backend al que va a compilar</c>: keyframes con el solver, triggers con el mismo modelo que arma el grafo. Se ve la gravedad a saltos del bucle Spawn, el rebote que entra solo al entrar el sensor, las rampas hechas cuadrado y los presets que <cg>esperan al jugador</c> quietos donde estan.
- <cl>Simulador de Fisicas</c>: <cr>la velocidad inicial de un cuerpo reactivo iba 30 veces pasada</c>. Se escribia en pixeles por segundo dentro de un campo que Advanced Follow lee en bloques, asi que cualquier lanzamiento salia disparado contra el tope de velocidad.
- <cl>Simulador de Fisicas</c>: <cr>el rebote contaba dos veces</c>. El trigger de choque ya multiplica el eje golpeado por el rebote del cuerpo, y encima sumaba un impulso del tamano de un rebote entero: una caja con rebote 0.35 salia despedida mas alto de lo que habia caido. Ese impulso pasa a ser solo el empujon corto que saca al sensor del bloque, y los sensores se montan <cg>a caballo del borde</c> del cuerpo en vez de colgando por fuera, que era todo el hueco que quedaba entre la caja y el suelo.
- <cl>Simulador de Fisicas</c>: los keyframes horneados <cg>trazan el objeto sobre el que GD gira el grupo</c>. Las poses del solver son del centro de masa, pero GD mueve un grupo alrededor de su group parent, asi que un cuerpo que giraba <cr>terminaba en la partida en otro sitio que en la vista</c>. Ahora el emisor elige ese objeto, escribe el recorrido sobre el y lo registra como parent cuando el cuerpo tiene varias piezas.
- <cl>Simulador de Fisicas</c>: <cg>cada cuerpo gira sobre su masa de verdad</c>. El origen pasa a ser el centroide por area y no el centro de la caja envolvente, la masa se saca de la forma que ocupa (una rampa pesa el medio bloque que llena, un orbe su circulo) y la inercia sale de esa misma forma: un disco cuesta la mitad de girar que el cuadrado en el que cabe.
- <cl>Simulador de Fisicas</c>: la animacion horneada <cg>se corta cuando todo se duerme</c>, en vez de gastar un keyframe por muestra sobre una escena ya quieta.
- <cl>Simulador de Fisicas</c>: nuevo compilador <cg>100% nativo de GD</c>. Cada cuerpo dinamico puede salir como keyframes horneados o como un grafo en tiempo real de <cg>Advanced Follow, Edit Advanced Follow, Collision Blocks, Collision, Spawn y Rotate</c>; el nivel reproduce el resultado sin necesitar un runtime de fisicas del mod.
- <cl>Simulador de Fisicas</c>: panel por cuerpo para cambiar entre <cg>fijo/dinamico</c>, triggers/keyframes, fuerza, tamano de sensores, P1/P2 y presets <cg>Empujable, Rebotador, Pesado, Flotante, Iman, Pendulo y Explosion</c>. Explosion separa cada objeto en su propio subgrupo y Control ID.
- <cl>Simulador de Fisicas</c>: reserva segura de Group/Block/Control IDs, validacion del grafo antes de tocar el nivel y rollback completo si GD no reconoce un trigger o un objeto llega a su limite de grupos. Recompilar la misma seleccion reemplaza su sistema anterior.
- <cl>GIF a Objetos</c>: nuevo modo <cg>Circulos</c>, el sexto del boton <cg>Modo</c>. Dibuja la imagen entera con <cg>elipses y nada mas</c>: busca donde la mancha es mas gorda, ahi planta la mas grande que quepa y la estira hacia donde la mancha da de si probando doce giros y cuatro cinturas —con el radio entero sale un disco, con un tercio un huso—, y repite con lo que queda hasta no dejar celda sin pintar. Sale un mosaico de <cg>circulos de todos los tamanos</c>, cada uno estirado a lo suyo, que no se parece a nada de lo que hacen los otros modos. Como todos los objetos son el mismo circulo de GD, o sea la misma hoja de sprites, aqui el orden entre ellos si manda: por eso este modo no remata las costuras con parches cuadrados —le quedarian debajo pase lo que pase— ni se le pide el aprobado de fidelidad de Pintura, que lo unico que conseguiria es mandarlo a bajar la resolucion por algo que es a proposito. Sobre nueve imagenes de prueba cuesta **220 -> 140** objetos de media y va casi cuatro veces mas rapido que Pintura.
- <cl>GIF a Objetos</c>: en <cg>Pintura</c>, una mancha en diagonal deja de salir en <cr>una escalera de cuadraditos</c>. El ajuste de rectangulo probaba <cr>seis grados alrededor del eje de inercia</c>, y una mancha simetrica no tiene eje: el de un rombo —que es el borde en diagonal de cualquier silueta cerrada— sale recto, asi que la caja que se probaba era la del cuadro y acertaba la mitad. Ahora se prueban tambien los giros de los <cg>lados de la envolvente convexa</c>, que es donde apoya siempre la caja de area minima. Un rombo pasa de **68 -> 2** objetos y una rejilla de dieciseis, de **160 -> 32**; sobre ocho dibujos llenos de diagonales, **88 -> 64** objetos de media y mas parecidos al original. La caja girada solo entra si su borde recto cae sobre el hueco: contra otro color, ese color reclamaria despues cada media celda partida con su propio cuadradito y acabaria costando mas que la escalera que venia a quitar.
- <cl>GIF a Objetos</c>: nuevo modo <cg>Libre</c>, el quinto del boton <cg>Modo</c>. En vez de dibujar solo con el cuadrado, el circulo y los dos triangulos, usa <cg>toda la decoracion de GD</c>: la primera vez que lo eliges, el mod recorre la lista de objetos del propio juego, se queda con los que son decoracion y admiten color, y rasteriza cada uno en una lamina para saber que forma pinta. Ningun id esta escrito a mano, asi que la biblioteca es la que tenga tu version instalada. Por cada mancha del dibujo busca el objeto que la cubre entera —en las cuatro vueltas y volteado— y solo lo suelta si <cg>sale mas barato</c> que pintarla a base de rectangulos; lo que ningun objeto cubre bien lo sigue haciendo el modo Pintura. Cada color compite contra su version a pinceladas, asi que el modo Libre <cg>nunca puede costar mas objetos</c> que Pintura. En la escena de prueba: **87 -> 79** objetos con la misma fidelidad. La vista previa ya ensena las siluetas reales, porque salen del sprite del objeto.
- <cl>GIF a Objetos</c>: el importador va <cg>entre tres y cinco veces mas rapido</c>. Perfilandolo, <cr>tres cuartas partes del tiempo se iban en calcular el seno y el coseno del giro de un objeto</c>... una vez por cada punto que se le preguntaba, y en una imagen grande eso son **228 millones** de veces. Ahora el giro se resuelve una sola vez por figura y cada fila se recorta al tramo que la figura puede tocar. Ademas, los pases del modo <cg>Render</c> y el trazado de cada color <cg>van a la vez</c> en varios nucleos, recogiendo el resultado en orden para que el plan no dependa de cual acabe antes. Medido a dos nucleos: Pintura a 128 px **38s -> 12s**, Render **19s -> 4.2s**, Art **1.6s -> 0.6s**. Con mas nucleos, el Render escala mas.
- <cl>GIF a Objetos</c>: la <cg>reduccion de la imagen la hace la tarjeta grafica</c>. Un video de 1080p obliga a promediar dos millones de pixeles por frame para acabar en una rejilla de 64 celdas; ahora eso se hace de una pasada en la GPU, mitad a mitad, con el color <cg>ponderado por alfa</c> para que el negro transparente del borde de un sprite no deje orla oscura. Si no hay contexto de GL, la misma cuenta se reparte entre hilos. Todo lo que viene detras —paleta, cuantizado, mascaras de fondo— trabaja ya sobre la imagen pequena.
- <cl>GIF a Objetos</c>: los rectangulos ahora <cg>se tragan el hueco que nadie ve</c>. Dos que se podian juntar se quedaban separados porque su caja comun pisaba una celda de al lado; cuando esa celda ya lleva el mismo color o la tapa una capa de mas arriba, salir gratis es exactamente lo que convierte dos objetos en uno. Medido sobre <cg>38 imagenes de todo tipo</c> (emojis, logos, fotos, texto, degradados): **240 -> 228** objetos de media con el dibujo igual de parecido.
- <cl>GIF a Objetos</c>: los <cg>GIF animados</c> se podan mirando las pistas juntas y no cada una por su lado. Un objeto de una pista que solo repetia el color que ya pintaban los fijos <cr>se salvaba</c> porque dentro de su pista era el unico que tocaba esa celda. Los rectangulos de los fijos tampoco pueden crecer donde una pista se enciende, para que no la tapen en su frame.
- <cl>GIF a Objetos</c>: la silueta que aparece en todos los frames ya <cg>no se copia una vez por frame</c>. Se dibuja una sola vez y la corren <cg>triggers Move</c>, con el desplazamiento repartido en lo que dura el frame para que ademas se vea como movimiento y no como parpadeo. Una figura que cruza seis frames pasa de **14 -> 3** formas y el GIF de la prueba de modo Art baja de **32 -> 17** triggers; en una escena con dos siluetas cruzando un fondo, **932 -> 566** objetos. El fondo que la silueta tapaba se reconstruye con lo que hay debajo, asi que deja de costar una pista por frame.
- <cl>GIF a Objetos</c>: el plan con movimiento se prueba <cg>al final y compitiendo</c>, no dentro de la busqueda de resolucion. Solo se queda si baja el total de objetos al menos un **5%**, si la reproduccion sigue por debajo del tope de triggers y si en modo Pintura no pierde fidelidad. Metido en la busqueda, el borde que la silueta deja en el fondo se leia como que la rejilla iba grande y <cr>respondia bajando la resolucion</c> para arreglar algo que no era la resolucion.
- <cl>GIF a Objetos</c>: acepta <cg>video</c> (mp4, mov, m4v, mpg, avi, wmv) con el mismo decodificador que ya usan los thumbnails. Reparte las capturas por toda la duracion, saca el ritmo de las marcas de tiempo reales del archivo y entra en el mismo optimizador que un GIF.
- <cl>GIF a Objetos</c>: nuevo interruptor de <cg>Glow</c> (suave o alto). Los colores que de verdad se leen como luz se duplican un poco mas grandes y por detras, en su propio canal <cg>mezclado y a media opacidad</c>. Va despues de elegir el plan a proposito: si entrase en la busqueda de resolucion, el halo contaria como diferencia contra el original y la busqueda responderia bajando la rejilla.
- <cl>GIF a Objetos</c>: <cr>la capa de fondo dejaba el orden a medias</c>. Todos sus bloques iban al mismo Z-Order sin mirar su color, asi que donde dos se pisaban decidia GD y no el plan. Ahora cada color baja lo suyo, en el mismo orden que arriba.

---

## ![](frame:GJ_garageBtn_001.png?height=18) <cy>Creador de Iconos</c>

- <cg>El lienzo pasa a ser el taller</c>. Antes tocar el icono solo cambiaba de zona y todo lo demas se hacia con sliders. Ahora <cg>tocas una capa y se elige esa capa</c> (saltando a su zona sola), la arrastras para moverla, la estiras por las <cg>cuatro esquinas</c> y la giras con el tirador de arriba. Al mover se <cg>imanta</c> al centro y a los bordes del cuadro guia, con una linea que avisa cuando esta pegado.
- <cg>Zoom y encuadre</c>: la rueda acerca donde apuntas, arrastrar el vacio mueve la vista y el boton <cg>Ajustar</c> lo devuelve todo a su sitio. Los controles flotan dentro del propio lienzo para no gastar altura.
- <cr>El panel de la derecha era un scroll con cuatro tarjetas seguidas</c>, asi que elegir una capa y pintarla obligaba a bajar y subir todo el rato. Ahora son <cg>cuatro pestanas</c> -- Capas, Pintura, Forma e Icono -- y cada una ocupa el panel entero. Fuera de Capas queda arriba una tira fija con la capa que estas tocando.
- <cl>Capas</c>: cada fila lleva su <cg>miniatura de verdad</c> en vez de un cuadradito de color, mas ojo, <cg>candado</c> (una capa bloqueada no se mueve desde el lienzo) y las flechas de orden. Las chips de zona tambien ensenan una mini-vista de lo que hay dentro.
- <cg>Los numeros se escriben</c>. Cada control lleva slider, <cg>casilla</c> y flechas de paso, asi que se puede poner un giro de **45** exacto en vez de pelearse con el slider. Ademas la escala se separa en <cg>ancho y alto</c>, y hay seis botones de <cg>alineado</c> contra el cuadro recomendado.
- <cg>Temas para el icono entero</c>. En la pestana Icono hay una rejilla (fuego, hielo, oro, veneno, arcoiris... y <cg>tus colores de jugador</c>) y un toque pinta cuerpo, detalle, cupula y brillo de todas las partes a la vez, con un solo paso de deshacer. Antes lo mismo se hacia zona por zona.
- <cg>Biblioteca de pinturas</c>: guardas como esta pintada una capa con nombre y la vuelves a usar en cualquier otra, o de golpe en toda la zona.
- <cg>Cuentagotas</c>, <cg>codigo hexadecimal</c> y <cg>colores recientes</c> para no repetir la vuelta por la rueda de color cada vez.
- <cg>Partir de un icono oficial</c> ya se puede desde dentro del editor, rellenando todas las zonas de una vez, y el selector de formas trae una pestana <cg>Mis iconos</c> para reaprovechar el dibujo de otro icono tuyo (mas un salto por id, que pasar paginas hasta el **137** no era manera).
- Nuevo boton <cg>Probar</c>: ensena el icono ya compuesto como se vera en el juego, tintado igual que lo tinta GD, con brillo si/no, colores reales si/no, tres tamanos y el icono vanilla al lado para comparar.
- <cl>Galeria</c>: filtro por gamemode, <cg>favoritos</c> (que salen siempre arriba), insignia de <cg>En uso</c> en el icono que tienes puesto y orden tambien por fecha de creacion.
- Mientras arrastras en el lienzo solo se vuelve a dibujar <cg>la zona activa</c> y no las cinco, que es lo que permite que el arrastre se vea en tiempo real.

---

## ![](frame:GJ_starsIcon_001.png?height=18) <cy>Peticiones de Miniatura</c>

- Llega el sistema de <cg>peticiones desde Discord</c>. En el servidor se pulsa un boton, se rellena un formulario con el <cg>ID del nivel</c>, la <cg>dificultad</c> - que se elige con las caras del propio juego -, si es <cg>platformer o classic</c>, una descripcion y un video opcional. La peticion se guarda y el equipo la revisa.
- En <cg>Paimon Hub > Miniaturas > Peticiones</c> se ve la cola entera desde el juego, con la <cg>cara de dificultad</c> y su brillo de rate, el tipo de nivel, quien la pidio y en que quedo. Se filtra por todas, pendientes o enviadas, cada fila abre el nivel y, si la peticion traia video, tambien lo abre.
- Cuando el equipo la acepta elige <cg>dificultad y rate</c>, de star rate a mythic, y esa es la cara que el mod dibuja. Mientras nadie decida se ensena la que pidio quien la mando.
- Se apaga desde <cg>Modulos > Miniaturas > Peticiones de Miniatura</c>.

---

## ![](frame:GJ_infoIcon_001.png?height=18) <cy>Avisos de Miniaturas Nuevas</c>

- La tarjeta pasa a llevar <cg>marco blanco de popup</c>. El borde se pintaba con el color dominante de la miniatura, asi que cada aviso salia de un color distinto y ninguno se parecia a las ventanas del juego. Ahora va el marco claro con una sombra por detras, que se ve igual sobre un menu claro que sobre uno oscuro.
- <cr>La estela de featured salia mas grande que la cara de dificultad</c>. La moneda se colgaba aparte y a otra escala; ahora la pinta <cg>el propio icono de dificultad</c>, con la proporcion que usan las listas del juego.
- Tarjeta redisenada: la columna de dificultad queda separada por una linea, el aviso entra en un <cg>chip</c> con el color del nivel y el <cg>panel oscuro se difumina</c> hacia la derecha, asi que el ultimo tercio sigue siendo la miniatura. La barra de tiempo va pegada al borde de abajo.
- Animaciones repasadas: el contenido entra <cg>escalonado</c> por detras del marco, deslizar aterriza con estirado y rebote en vez de frenar en seco, caer aplasta la tarjeta al tocar el suelo, el brillo que la barre es un <cg>degradado</c> y no un cuadrado de bordes duros, y el vaiven de reposo arranca desde cero en lugar de saltar al acabar la entrada.

---

## ![](flozwer.paimbnails2/paim_Paimon.png?height=18) <cy>Paimon del menu</c>

- La Paimon escondida ya no se queda <cr>siempre encima del logo</c>: ahora <cg>mira todos los botones del menu</c> y se mete <cg>debajo de uno</c>, con la cabeza tapada y solo la cara asomando, como si estuviera vigilando. Cada vez que entras elige otro. Descarta los botones que no tienen sitio debajo y los que la taparian entera, asi que siempre queda algo que ver, y se pone por detras del boton, que sigue clicandose igual.
- Con la <cl>Guia de Paimon</c> puesta deja de esconderse y <cg>te espera asomada en el boton del Paimon Hub</c>, que es desde donde se abre el chat. Antes se quedaba donde le tocase por azar y habia que buscarla para preguntarle algo.
- La <cg>explosion se rehizo entera</c>. De los cinco sonidos que sorteaba, <cr>dos no existen en el juego</c> y otros dos eran el de salir de un nivel y el de acabarlo, asi que la mayoria de los clicks no sonaba a explosion o no sonaba a nada. Ahora se monta <cg>por capas</c> - chasquido, cuerpo grave y cola - con el tono cambiado en cada click, y una de cada tres veces cae un segundo estallido. De los cinco efectos de particulas, <cr>tres tampoco existian</c>; ahora sale una de las tres explosiones del juego mas escombros y un destello.
- Se apaga desde <cg>Modulos > Menu > Paimon Escondida</c>.

---

## ![](frame:GJ_sDownloadIcon_001.png?height=18) <cy>Tienda de Iconos</c>

- <cr>Un icono descargado desaparecia al reiniciar el juego</c>, y ademas se quedaba desequipado. La tienda solo le recordaba a <cl>More Icons</c> nuestra carpeta al abrirla, pero More Icons resuelve el icono que llevas puesto durante la pantalla de carga y, si no lo encuentra, <cr>da el guardado por muerto y lo borra</c>. Ahora cada icono deja un <cg>install.json</c> al bajarse y todos se registran <cg>en la pantalla de carga</c>, antes de que More Icons pase por ahi. Los que ya tenias instalados se recuperan solos.
- <cg>La tienda se rediseno</c>. Arriba y abajo van dos <cg>bandas</c> con linea dorada que separan la rejilla del fondo: en la de arriba quedan el titulo, el buscador, los filtros y el contador del catalogo, y en la de abajo los resultados, el orden y el paginador dentro de una <cg>pastilla</c>. Con eso entran <cg>cinco iconos por fila y dos filas enteras</c> en vez de una y media.
- Las tarjetas se rehicieron: <cr>el nombre se montaba encima del hueco de la vista previa</c>, asi que el icono y el texto se pisaban. Ahora el hueco, el nombre y el autor tienen su franja, y lo que ya tienes descargado se marca con un <cg>borde verde</c> ademas del tilde de la esquina, que se ve sin leer la tarjeta.
- El boton de <cg>Filtros</c> lleva un <cg>punto dorado</c> cuando hay un gamemode filtrado: antes el filtro se quedaba puesto y la unica pista estaba dentro del popup.
- La ficha del icono es <cg>mas grande</c>, con la vista previa a mayor tamano y el <cg>gamemode en una chapa debajo</c>, que libera una fila entera para la descripcion.

---

## ![](frame:chestIcon_001.png?height=18) <cy>Compatibilidad</c>

- <cl>Level Thumbnails</c>: ya es <cg>compatible</c>. Se quita el cartel de inicio que pedia desactivarlo y se pueden tener los dos mods puestos a la vez.

---

## ![](frame:GJ_optionsBtn_001.png?height=18) <cy>Correcciones</c>

- <cl>Efectos de la Muerte</c>: el sonido que eligieras <cr>se colaba fuera del nivel</c>. Se sustituia el explode_11 del juego mirase donde mirase, asi que tambien sonaba en la explosion de la Paimon del menu y en el efecto de click del cursor. Ahora solo se cambia dentro del nivel o del editor.
- <cl>Efectos de Click</c>: el sonido <cg>Cofre</c> <cr>no sonaba</c>, apuntaba a un archivo que no viene con el juego.
- <cl>Cancion Dinamica</c>: la escucha en streaming - la que suena mientras el juego se descarga la cancion del nivel - <cr>no hacia caso al volumen</c>. Va por un canal propio, fuera del grupo de musica del juego, asi que bajar el control no la tocaba y se seguia oyendo aunque estuviera en <cg>0</c>. Ahora sigue el volumen mientras suena, y en 0 se corta en vez de quedarse descargando de fondo.

---

# <cy>v1.1.1</c>

![](flozwer.paimbnails2/paim_Paimon.png?height=32) Actualizacion centrada en el <cl>editor</c>: modos <cg>Pintura</c> y <cg>Render</c> de GIF a Objetos, el <cl>Simulador de Fisicas</c> con el arte y las formas reales de cada objeto, <cl>Autobuild</c> adaptable, y una tanda larga de arreglos.

---

## ![](frame:GJ_hammerIcon_001.png?height=18) <cy>Editor</c>

- <cl>GIF a Objetos</c>: modo <cg>Pintura</c>, un tercer modo en el boton <cg>Modo</c> que dibuja como se hace el art a mano. Saca el contorno de cada mancha de color, lo suaviza y lo traza con rectangulos finos rotados; los circulos quedan para extremos realmente redondos. Junta motas y trazos alineados, cierra huecos con una base compacta por detras, descarta objetos tapados y exige al menos 95% de similitud en cada frame.
- <cl>GIF a Objetos</c>: nuevo modo <cg>Render</c>. Prueba el dibujo en varias resoluciones, compara tanto la forma general como el detalle fino y se queda con el resultado mas claro sin disparar el numero de objetos. Todos los modos muestran su avance; con <cg>Run background</c> el analisis sigue fuera del panel y los objetos aparecen por lotes mientras puedes continuar editando.
- <cl>GIF a Objetos</c>: ahora tambien acepta <cg>imagenes fijas</c> (**PNG**, **JPG**, **WEBP**, **BMP**, **TGA**...), no solo GIFs.
- <cl>GIF a Objetos</c>: en <cg>Pintura</c> el circulo es el unico objeto que GD no ordena junto a los cuadrados, asi que <cr>se colaba por encima del dibujo</c> (se veian manchas gigantes tapando todo). Ahora solo se usa donde nada se pinta encima y el resto sale con cuadrados girados, que si respetan el orden.
- <cl>GIF a Objetos</c>: contornos mas limpios en <cg>Pintura</c>. Cada tramo se alarga solo lo justo para cerrar la esquina (antes sobresalia siempre y dejaba pinchos) y el borde exterior ya no pinta un halo alrededor de la silueta.
- <cl>GIF a Objetos</c>: las <cg>imagenes fijas</c> llegan hasta **320 px** de resolucion (los GIFs siguen en 160 para no tardar minutos), con el paso del boton mas grande a medida que subes.
- <cl>GIF a Objetos</c>: se acabaron los <cr>picos en el borde</c> del modo <cg>Pintura</c>. Las celdas sueltas se remataban con un cuadrado girado que asomaba media celda por cada punta; ahora van con un cuadrado justo, y ninguna figura puede asomar sobre un color que quede debajo. Los remates de punta tampoco clavan un rombo: la linea se alarga sola.
- <cl>GIF a Objetos</c>: la <cr>orla de pixeles</c> que dejaba el suavizado del dibujo original ya no se dibuja. Cuando una hebra fina es la mezcla de los dos colores que separa (o sea, antialias y no dibujo de verdad) se va al mas parecido de los dos, asi que el borde queda de una tinta y no de ocho.
- <cl>GIF a Objetos</c>: optimizador de objetos mucho mas fino en <cg>Pintura</c>. Se descarta todo objeto que no cambie el dibujo (antes solo los tapados del todo), los cuadrados de una misma mancha se empaquetan juntos aunque salgan de pasadas distintas y los que comparten un lado se fusionan en uno. En pruebas: **174 -> 73** y **80 -> 44** objetos con el dibujo identico.
- <cl>GIF a Objetos</c>: arreglado que <cr>media imagen se volviera transparente</c> pasando de **181 px** de resolucion. La rejilla guardaba el numero de celda en un hueco de 16 bits, y de 32768 celdas en adelante el numero daba la vuelta y esa mitad se perdia; a **320 px** encima reaparecia descolocada.
- <cl>GIF a Objetos</c>: quitada la limpieza a color entero que corria por encima de **80 px** en <cg>Pintura</c>. Difuminaba el dibujo antes de elegir la paleta, y de ahi salia el resultado emborronado con los pixeles a la vista. A **240 px**: **5449 -> 2819** objetos y mas parecido al original.
- <cl>GIF a Objetos</c>: la paleta de <cg>Pintura</c> ahora se la llevan los colores del dibujo y no la orla del antialias. Cada celda pesa por lo plana que es su vecindad, y las cajas se parten por donde mas variedad de color hay en vez de por tamano, asi que ya no salen cuatro azules identicos y sin hueco para el detalle chico.
- <cl>GIF a Objetos</c>: las celdas sueltas rodeadas de otro color se funden con el vecino. No se veian y costaban un objeto cada una: a **200 px** eran la mitad del total.
- <cl>GIF a Objetos</c>: la revision de <cg>Pintura</c> ahora se mide contra la imagen tal cual, no contra la version ya limpia, asi que el porcentaje dice de verdad lo que se parece. Y cuando no llega al 95% ya <cr>no encoge el dibujo</c> hasta que pase: el parecido no mejora al encoger, asi que se queda a la resolucion que pediste y el porcentaje te lo dice.
- <cl>GIF a Objetos</c>: en <cg>Pintura</c> los rectangulos ahora <cg>cruzan por encima de lo que otra capa tapa despues</c>. Un color no tiene que rodear al que va por encima: pasa por debajo de una tirada y el de arriba lo tapa. Donde antes iba un reguero de cuadraditos de una celda ahora va un rectangulo entero.
- <cl>GIF a Objetos</c>: <cr>el trazo del dibujo salia hecho una banda gorda</c> en <cg>Pintura</c> cuando compartia color con una zona rellena (el contorno de un personaje que nace de un mechon macizo, por ejemplo). El grosor se media en la mancha entera y mandaba el del mechon; ahora se mide tramo a tramo, y la parte maciza y el trazo se separan para que cada uno lleve lo suyo: rectangulos grandes la maciza, tiras y circulos el trazo.
- <cl>GIF a Objetos</c>: el eje de un trazo ya no se recorta mas de lo que el trazo mide de ancho. Una linea ondulada <cr>se aplanaba entera</c> y luego hacian falta docenas de parches de una celda para devolverle la forma.
- <cl>GIF a Objetos</c>: las <cg>motas que flotan sueltas</c> (la trama de puntos del fondo de un dibujo, por ejemplo) ya no se dibujan, y el limite de lo que se considera mota va con el tamano del dibujo en vez de con su lado. El limite va corto a proposito: en un dibujo hecho a pixel el detalle chico esta puesto adrede y no se puede tocar.
- <cl>GIF a Objetos</c>: <cr>a baja resolucion el dibujo salia deformado</c> en <cg>Pintura</c>. Una mancha compacta no tiene eje, asi que trazarla como si fuera una linea daba una losa girada atravesada en mitad del dibujo; ahora solo se traza como linea lo que de verdad da varias anchuras de largo, y lo demas va por su contorno. La figura girada tampoco se admite si su caja ocupa mucho mas que las celdas que tiene que tapar, y asomar sobre lo que otra capa tapa despues ya no sale gratis al elegir figura. A **48 px** el parecido sube de **84%** a **93%**.
- <cl>GIF a Objetos</c>: en total, a **96 px**: **846 -> 709** y **1719 -> 1344** objetos, y con mas parecido al original que antes en las dos.
- <cl>GIF a Objetos</c>: arreglado un <cr>crash al cerrar el panel mientras procesaba</c>. El aviso que llega del hilo de trabajo cogia el panel y soltaba la referencia en la misma linea, antes de usarlo: si el panel ya estaba cerrado, esa referencia era lo unico que lo mantenia vivo y se borraba justo antes. Pasaba lo mismo al terminar de decodificar un GIF, una imagen o al elegir archivo.
- <cl>Simulador de Fisicas</c>: la vista previa ahora sale con el <cg>fondo y el piso del nivel</c>, con los mismos colores que tenga el editor. Antes se pedian como si fueran trozos de un spritesheet y GD nunca los devolvia, asi que <cr>el recuadro quedaba en negro</c>. El piso va donde va de verdad (**y = 0**) y se mueve con la camara, para que se note si un cuerpo va a caer por debajo.
- <cl>Simulador de Fisicas</c>: cada objeto se dibuja con <cg>su propio arte</c>. Se reconstruye el objeto por su ID con su color, detalle, escala, giro y espejo, en vez de estirar su primer frame sobre la caja de colision: si pones un cubo sale ese cubo, y si pones otro sale el otro.
- <cl>Simulador de Fisicas</c>: <cg>detecta la forma de cada objeto</c>. Los orbes, anillos y monedas colisionan como circulos con el radio que usa GD, las rampas como el triangulo que de verdad ocupan (antes te frenaban como un cuadrado) y lo que este girado fuera de los 90 grados usa sus esquinas reales. La vista previa dibuja el contorno de cada forma y el estado te dice cuantos bloques, rampas y redondos entraron.
- <cl>Simulador de Fisicas</c>: <cr>los orbes y anillos no se podian capturar</c>. El filtro descartaba toda la familia de objetos con efecto para quitar los triggers, y en GD los orbes son de esa familia; ahora solo se saltan los triggers de verdad.
- <cl>Simulador de Fisicas</c>: <cg>zoom y arrastre</c> en la vista previa. Rueda del raton o los botones de lupa (hasta **x12**), arrastra dentro del recuadro para mirar otra zona y el boton de reset vuelve al encuadre automatico. El zoom actual sale arriba junto a la vista.
- <cl>Simulador de Fisicas</c>: al abrirlo ya se ven los cuerpos capturados en su sitio, sin tener que pulsar <cg>Previsualizar</c> primero.
- <cl>Simulador de Fisicas</c>: la vista previa deja de pintar contornos encima del arte. Las <cg>hitboxes</c> pasan a una casilla junto al zoom y arrancan apagadas, asi que el recuadro ensena los objetos del juego y nada mas; el unico contorno que queda es el del objeto que tengas elegido.
- <cl>Simulador de Fisicas</c>: el objeto de la vista previa <cr>se montaba dos veces</c>. Se creaba por su ID (que ya lo deja montado) y encima se le repetia el montaje a mano, asi que varios salian con el detalle duplicado o fuera de sitio; ahora solo se le devuelven el sprite de detalle y el brillo, que dentro de un nivel viven en otras capas. Si un ID no se puede crear, se copia sprite a sprite lo que el editor esta dibujando en ese momento.
- <cl>Simulador de Fisicas</c>: <cg>formas no cuadradas de verdad</c>. La silueta del arte se traza una sola vez por ID (se dibuja el sprite en chico y se saca su envolvente convexa de hasta 8 lados) y se mete dentro de la hitbox que da GD: un pincho choca y resbala como el triangulo que es, una sierra como un disco, y lo que llena su caja sigue siendo un cuadrado.
- <cl>Simulador de Fisicas</c>: <cg>toca un objeto en la vista para elegirlo</c>, y el engranaje abre sus ajustes. Cada cuerpo lleva su masa, escala de gravedad, friccion, rebote y su propio lanzamiento (velocidad y giro), y cada objeto dentro del cuerpo puede llevar friccion y rebote aparte; lo que no toques sigue lo que digan los controles del lab.
- <cl>Simulador de Fisicas</c>: el piso de la vista pasa a ser <cg>la textura de suelo del nivel</c> repetida a lo ancho, en vez de una franja de color plano.
- <cl>Simulador de Fisicas</c>: arreglado un <cr>crash al hornear</c>. Los keyframes se armaban a partir del texto de guardado de objetos sueltos, y al volver a leerlos GD entraba en `GJEffectManager::getColorSprite` sin canales de color detras y se caia ahi. Ahora cada keyframe y cada trigger se crean por el mismo camino que usa el editor cuando pones un objeto a mano.
- <cl>Simulador de Fisicas</c>: el trigger de la animacion ya <cg>dura lo que duro la simulacion</c>. Se quedaba con la duracion que trae un trigger recien puesto, asi que la caida se reproducia en una fraccion del tiempo y no se parecia a la vista previa.
- <cl>Simulador de Fisicas</c>: al abrir el laboratorio <cg>ya no se captura solo</c> lo que tuvieras seleccionado como cuerpo A; lo eliges tu con <cg>Elegir A</c>.
- <cl>Simulador de Fisicas</c>: boton de <cg>dinamico / fijo</c> al lado de la vista, para cambiar el cuerpo que tengas elegido sin abrir sus ajustes.
- <cl>Simulador de Fisicas</c>: <cr>la vista salia en negro</c>. El fondo se pedia con el indice tal cual del nivel y GD los numera desde uno, asi que un nivel con indice cero no devolvia ninguna textura; ademas los fondos y suelos casi negros ahora se aclaran lo justo para distinguirlos, y la capa oscura de encima baja de 85 a 45.
- <cl>Autobuild</c>: modo <cg>Onda</c> con <cg>plantilla adaptable</c>. Cada pieza se elige mirando sus <cg>ocho vecinos</c> (tambien las diagonales), como el Auto-Build nativo, en vez de solo los cuatro lados. La plantilla guarda ademas como estaba armada la muestra, asi que aprende de la disposicion original y no solo de pares sueltos.
- <cl>Autobuild</c>: reutiliza una misma pieza <cg>girada y reflejada</c>. Una esquina capturada una vez sirve para las cuatro orientaciones, con interruptores propios de <cg>Rotar referencias</c> y <cg>Reflejar referencias</c> (apaga el espejo para texto o arte asimetrico) y <cg>Evitar repetir</c> para no clavar la misma variante dos veces seguidas.
- <cl>Autobuild</c>: <cg>Reglas estrictas</c> ahora conserva vecindades, diagonales y costuras aprendidas; al apagarlo conecta piezas con bordes parecidos en vez de rendirse. El solver aguanta el triple de vuelta atras (**400 -> 1200**), asi que rellena zonas grandes sin dejar huecos forzados.

---

## ![](frame:GJ_infoIcon_001.png?height=18) <cy>Imagenes importadas</c>

- <cl>Marca de agua invisible</c>: las imagenes y GIFs que importas al editor se firman siempre <cg>dentro de su propia geometria</c>, partiendo trazos reales en pares redundantes. No se anade ningun logo ni objeto visible de mas, y la firma no se puede desactivar.
- <cl>Advertencia de imagen</c>: al abrir un nivel descargado que lleva una imagen o GIF convertido a objetos sale un aviso, para que sepas por que el nivel puede ir pesado. Solo el aviso es opcional: se apaga en <cg>Ajustes > Image Object Warning</c> o desde Modulos.

---

## ![](frame:chestIcon_001.png?height=18) <cy>Extras</c>

- <cl>Reporte de crasheos</c>: si el juego crashea, al siguiente arranque se manda el crashlog de Geode junto con el log de esa sesion, para que se vea donde revento. El nombre de usuario que aparece en las rutas se reemplaza antes de mandarlo, se manda una sola vez por crasheo y se apaga en <cg>Ajustes > General > Send Crash Reports</c>.

---

## ![](frame:GJ_optionsBtn_001.png?height=18) <cy>Correcciones</c>

- <cl>Perfil redisenado</c>: los datos de la tira de estadisticas <cr>ya no eran clickeables</c>. Ahora cada uno vuelve a abrir su desglose nativo (estrellas y lunas los niveles completados, demons el desglose por dificultad), reenviando el toque al boton original del juego.
- <cl>Perfil</c>: las celdas de nivel del perfil salian en compacto pero <cr>con el layout de RobTop sin ajustar</c>, asi que los textos se pisaban. Ahora se ajustan igual que en el buscador.
- <cl>Editor de layout</c>: el cuadrado de escalado <cr>se comia la esquina del boton</c> y no se podia mover. Su zona de toque se ajusto a lo que se dibuja y va justo donde aparece el cuadrado.
- <cl>Paimon del menu</c>: el globo de texto salia <cr>inclinado</c> siguiendo al boton, y la Paimon escondida se giraba dos veces. El texto se lee siempre en horizontal.
- <cl>Guia Paimon</c>: la version salia con la <cr>v doble</c> (vv1.1.0).
- <cl>Modulos</c>: el buscador tambien busca sobre los nombres y descripciones <cg>traducidos</c>, asi que buscar en espanol encuentra el modulo aunque su nombre interno este en ingles.
- <cl>Capturadora</c>: el boton de carpeta <cr>daba error aunque la carpeta si se abriera</c>. Windows devuelve fallo cuando otro mod ya tomo el subsistema COM, y eso no significa que no haya abierto nada.
- <cl>Texture Studio</c>: <cr>las texturas de Geode salian deformadas</c> en los packs generados. Las hojas de Geode y de otros mods se guardan sin plist para no pisar el del mod instalado, pero la copia que sirve PackGen es de una version concreta: con un Geode mas nuevo su plist apunta a un atlas de otro tamano y cada sprite se recorta donde no toca. Ahora cada hoja se recoloca al atlas que tienes instalado antes de guardarla, y los sprites que tu version anadio se quedan sin colorear en vez de romperse. <co>Hay que volver a exportar el pack.</c>


# <cy>v1.1.0</c>

![](flozwer.paimbnails2/paim_Paimon.png?height=32) **La actualizacion mas grande hasta ahora.** Mas de <cg>25 funciones nuevas</c>, el <cl>Collab Editor</c> abierto para todos y una tanda larga de arreglos.

---

## ![](frame:GJ_hammerIcon_001.png?height=18) <cy>Editor</c>

- <cl>Musica del Editor</c> (**Ctrl+M**): panel chico arriba a la izquierda para escuchar tu musica mientras construyes. Reproduce, pausa, cambia de cancion, busca en la barra, controla el volumen y arrastra el panel donde quieras. Se calla solo al probar el nivel y vuelve donde iba al salir del playtest. Usa la misma biblioteca que la Musica del Menu.
- <cl>Autobuild</c> (boton en la barra o **Ctrl+B**): decoras una zona una vez y la repites donde quieras.
- Modo <cg>Onda</c>: aprende que pieza puede ir al lado de cual y rellena bloques y decoracion de forma coherente. Modo <cg>Sellos</c>: guarda grupos enteros y suelta uno en cada sitio.
- Construye sobre marcadores (bloques **467**, **143** y **146**), sobre la seleccion o rellenando toda el area.
- Conserva colores, HSV, grupos, capas y orden Z del objeto original, e importa solo los canales de color que se usan.
- Trae <cg>Otra semilla</c>, <cg>Deshacer</c> y <cg>Ver nivel</c> para probar resultados sin cerrar el panel.
- Las plantillas son archivos sueltos en la carpeta del mod (se comparten) y acepta librerias **.tblib** de otros autobuilders.
- <cl>Collab Editor</c>: se acaba la beta cerrada, <cg>ya esta disponible para todos</c>.
- <cl>Collab</c>: sincronizacion rehecha para assets de miles de objetos. Envio con confirmacion y reintentos, verificacion automatica con auto-reparacion de desyncs, y las selecciones gigantes ya se sincronizan al moverlas o rotarlas.
- <cl>Collab</c>: invitaciones de amigos con cartel para entrar de una, chat de voz tipo walkie-talkie con boton de **Mic**, y cada colaborador con su cursor personalizado.
- <cr>Se quitaron los 57 modulos de editor tipo BetterEdit</c> (HideUI, Object Search, Reference Image, Level Backups, Grid Control, Editor History, View Panel...) porque provocaban crashes. El color picker (**Ctrl+G**), la rotacion con **Alt + click derecho**, los filtros de Mis Niveles y Autobuild siguen funcionando.

---

## ![](frame:GJ_garageBtn_001.png?height=18) <cy>Iconos y garage</c>

- <cl>Extras del Kit</c>: un solo boton en el garage junta Iconos Copiados, Tienda, Degradados y Kit del P2. No duplica botones: se los pide prestados al garage y se los devuelve al cerrar.
- <cl>Creador de Iconos</c>: arma tus propios iconos. Pintas cada pieza con color plano, degradado lineal o radial, o una imagen tuya, y el resultado se instala solo en More Icons.
- <cl>Creador de Iconos</c>: galeria de proyectos, historial de cambios, paletas, editor de degradados con vista previa en vivo y opcion de compartir.
- <cl>Tienda de Iconos</c>: iconos de la comunidad desde **iconsgallery.pages.dev**, con buscador, filtros y ficha de cada uno.
- <cl>Icon Gradients</c>: degradados de GPU para tus iconos, en garage, partida, menu, perfiles y comentarios. Set aparte para el P2 (o colores volteados), puntos movibles con el teclado y precarga de shaders para evitar tirones.
- <cl>Separate Dual Icons</c>: el jugador 2 con sus propios iconos, colores, estela y efecto de muerte.
- <cl>My Icon Sets</c>: el boton de carpeta de Copied Icons guarda tu kit completo (los 9 gamemodes, colores, glow, estela y muerte) con nombre, para volver a ponerlo cuando quieras. Hasta **100** sets.
- <cl>Copy Icons</c>: cada icono abre su ficha con zoom. Ves si lo tienes desbloqueado, el porcentaje del logro y como se consigue, con el asset de ![](frame:GJ_moonsIcon_001.png?height=13) lunas, ![](frame:GJ_starsIcon_001.png?height=13) estrellas, diamantes, monedas u orbes y la cantidad exacta.
- <cl>Sonidos de muerte propios</c>: metes tus mp3/wav (de a uno o una carpeta entera) y suenan al morir. Al azar o en orden, sin repetir el mismo dos veces, con volumen y tono.

---

## ![](frame:newMusicIcon_001.png?height=18) <cy>Audio</c>

- <cl>Cancion Dinamica</c>: al darle play la musica <cg>ya no se corta</c>. Se hunde bajo un filtro submarino y sigue sonando mientras se descarga el nivel; solo se apaga cuando el nivel arranca de verdad. Si cierras el popup, sale del agua sola.
- <cl>Cancion Dinamica</c>: panel propio en **Paimon Hub > Audio**. Punto de inicio, que cancion suena si el nivel tiene varias, volumen, fundido y si suena en los niveles oficiales.
- <cl>Cancion Dinamica</c>: el buceo trae 4 estilos (Submarino, Amortiguado, Profundo y Radio) mas modo Personalizado con corte de agudos y graves, reverb y tono, con boton de <cg>Probar</c>.
- <cl>Volumen Dinamico</c>: mide el volumen real de cada cancion (**LUFS**) y evita el salto al pasar de una tranquila a un drop fuerte. Modos Adaptativo, Fijo y Personalizado.
- <cl>Menu Music</c>: buscador de **Newgrounds** dentro del juego. Top 5 semanal, busqueda por nombre o artista, escuchas antes de bajar y la descarga entra directo a tu biblioteca.
- <cl>Menu Music</c>: navegador de la musica del juego <cg>por etiquetas</c>, para buscar por estilo en vez de por ID.
- <cl>Menu Music</c>: 8 efectos de audio (Slow + Reverb, Dreamy, Bass Boost, Nightcore, Underwater, Concert Hall y Lofi) mas <cg>audio espacial</c> con presets, movimiento en orbita o vaiven y un escenario donde mueves de donde viene el sonido.

---

## ![](frame:gj_twitchIcon_001.png?height=18) <cy>Level Requests</c>

- Recibe pedidos de niveles desde tu <cg>pagina web</c> y desde los chats de <cg>Twitch</c>, <cg>YouTube</c>, <cg>Kick</c> y <cg>TikTok</c>. Todas las fuentes caen en la misma cola.
- Comandos configurables (**!req**, **!request**...), limite de cola y overlay para **OBS** en `http://localhost:21680/overlay`.
- Tu pagina es **flozwer.org/request/tu_usuario**, sacada de tu cuenta de Geometry Dash, con botones para copiar el link y abrirlo.
- Para mandarte un nivel hay que registrarse con una cuenta de GD de verdad: Paimon da un codigo, la persona lo comenta en su perfil y el servidor lo comprueba. El nombre que ves en la cola es <cg>una cuenta real</c>.
- Los pedidos de la web traen el recado que escribio la persona y su link de YouTube. Un boton azul abre el mensaje entero junto a quien lo mando, el nivel y su creador.
- La linea de abajo de cada fila (el **@**, la ID y el creador) va en la fuente del juego y mas grande, con el creador en dorado, para leerla mientras transmites.
- La pestana del navegador sale con <cg>el cubo del streamer</c>, dibujado con los sprites del propio juego, asi que varias paginas abiertas se distinguen de un vistazo.
- El boton de la cola se movio del menu principal a la pantalla de busqueda online, junto a los demas filtros. Sigue mostrando el contador.
- <cg>Aviso en pantalla</c> cuando llega un request nuevo, por encima de todo (jugando, en el editor y en el menu). Panel propio con vista previa donde eliges esquina, tamano, duracion, animaciones de entrada y salida, sonido y que datos ensena.

---

## ![](frame:GJ_starsIcon_001.png?height=18) <cy>Cursor</c>

- <cl>Efectos de click</c>: nueva pestana con vista previa donde clickeas y mantienes apretado para probar al momento.
- <cg>20 estallidos</c> al hacer click (onda, impacto, corazones, estrellas, confeti, chispas, fuego artificial, burbujas, nieve, tinta, rayos, circulo magico, pixeles, humo, monedas, notas, petalos, flor, galaxia y bola de fuego), otro distinto al soltar.
- <cg>9 efectos</c> mientras mantienes apretado, <cg>8 reacciones</c> del propio cursor y <cg>10 sonidos</c> con volumen y tono.
- <cg>18 presets</c> listos (Amor, Fiesta, Tormenta, Invocacion, Monedas, Volcan, Retro, Cosmos...) y engranaje propio por efecto: cada uno guarda su tamano y velocidad aparte, y los retoques se conservan aunque cambies de preset.
- Funcionan <cg>en telefono</c>: salen donde tocas la pantalla. Ya no hace falta raton ni cursor personalizado, asi que tambien sirven en PC con el cursor normal.
- <cl>Estela del cursor rehecha de cero</c>: 18 efectos, 6 modos de color (un color, degradado, arcoiris, arcoiris largo, al azar y por velocidad) y 30 presets. Las estelas viejas se convierten solas.
- <cl>Tienda de cursores</c>: busca cursores de **rw-designer** y **custom-cursor.com** sin salir del juego, con vista previa e instalacion en un toque.

---

## ![](frame:GJ_infoIcon_001.png?height=18) <cy>Informacion y navegacion</c>

- <cl>Avisos de Miniaturas Nuevas</c>: cuando alguien publica una miniatura, te entra una tarjeta con los datos del nivel (cara de dificultad, estrellas, monedas, likes y descargas) dibujados <cg>encima de la miniatura recien subida</c>, que hace de fondo. Tocarla abre el nivel.
- <cl>Avisos de Miniaturas Nuevas</c>: <cg>feed en vivo</c>. El juego mantiene una conexion abierta con el servidor, asi que la tarjeta entra <cg>en el momento</c> en que alguien publica, no en la siguiente consulta. Si se cae, reconecta sola y la consulta periodica recupera lo que se haya perdido. Al subir tu una miniatura, la tarjeta sale al instante con la propia respuesta del servidor.
- <cl>Avisos de Miniaturas Nuevas</c>: **11 animaciones de entrada** (deslizar, elastica, espiral, desplegar, voltear, caer, zoom...), **9 de salida** y **4 de reposo**, mas sitio en pantalla, tamano, duracion, oscurecido del fondo, zoom lento de la miniatura, barra de tiempo, brillo, sonido y boton de <cg>Preview</c> para verlo al momento.
- <cl>Paimon Info Suite</c>: paquete nuevo de modulos, cada uno se enciende por separado y con interruptor maestro.
- <cg>Info extendida</c>: tus intentos, saltos, mejores runs y graficos sobre el thumbnail, mas todos los campos ocultos del nivel en pestanas, copiables al tocarlos.
- <cg>IDs a la vista</c> en celdas, comentarios y perfiles, con color, opacidad, posicion y opcion de esconderlos hasta que aprietes **Shift**.
- <cg>Saltar de pagina</c> pasando el tope de 999, <cg>busqueda avanzada</c> con presets guardados y <cg>seguimiento de progreso</c> con mapa de calor de muertes.
- Perfiles de usuarios sin cuenta, herramientas de comentarios (ir a una pagina, ver IDs, estimar fecha exacta), relleno de nombres verdes en blanco y enriquecimiento opcional con **GDHistory**.
- Los modulos que pisan a **BetterInfo** se apagan solos si lo tienes instalado.
- <cl>Mensajes redisenados</c>: bandeja estilo GD con iconos de jugador, contador de no leidos, buscador local, respuesta rapida, atajo al perfil y seleccion de leidos para limpiar en masa.
- <cl>For You</c>: motor de recomendaciones nuevo. Aprende de lo que juegas, te gusta y guardas, mezcla varias fuentes y te dice <cg>por que</c> te recomendo cada nivel. Panel de etiquetas para dirigirlo a mano.
- <cl>Leaderboard Layout</c>: eliges que datos se ven en cada fila del ranking y en que orden, con presets listos.
- <cl>Input Scroll</c>: rueda del raton sobre una casilla numerica para subir o bajar el valor. Con el modificador, decimales de a **0.10**.
- <cl>Search History</c>: popup mas compacto (icono de dificultad, fecha y contador). Tocar una entrada busca directo.
- <cl>Quick Hub</c> redisenado: rueda minimalista con <cg>seleccion por sector</c> (basta apuntar hacia la opcion, ya no hay que acertar el icono). La opcion apuntada crece y estrena aro de color, y el centro te dice que vas a abrir.
- <cl>Quick Hub</c>: al hacer <cg>click derecho</c> en un boton del juego se guarda su direccion completa (ruta de node ids, pantalla, receptor del callback, texto, tag y posicion), asi que <cg>vuelve a encontrarlo aunque otros mods muevan la interfaz</c>. Si no estas en su pantalla, el radial <cg>te lleva alli y lo pulsa</c>.
- <cl>Quick Hub</c>: los botones que no se pueden pulsar en la pantalla actual salen apagados y te dicen donde viven.
- <cl>Quick Hub</c>: popup con pestanas <cg>Activos</c> y <cg>Anadir</c>, vista previa real de la rueda, y los botones capturados se pueden <cg>reeditar o borrar</c>. Al capturar eliges nombre, icono, forma (circulo, cuadrado o suelto) y color.

---

## ![](frame:GJ_sTrendingIcon_001.png?height=18) <cy>Jugabilidad y rendimiento</c>

- <cl>Golden Best</c>: el porcentaje se pone <cy>dorado</c> mientras superas tu record. Tambien en practica, plataformas, start pos y modo test, con color propio si quieres.
- <cl>Rendimiento en Gameplay</c>: panel nuevo que apaga de golpe lo que mas cuesta mientras juegas (efectos de fondo, de gameplay, del jugador, glow, gradientes, shaders, particulas, decoracion, suelo y transiciones), cada cosa por separado.
- <cl>Smooth Level Transitions+</c>: nueva entrada y salida de los niveles con 4 estilos, duracion e intensidad propias y control de que se anima. La salida copia la entrada, tiene su propio estilo o se apaga.
- <cl>Modulos</c>: registro central. Cada funcion del mod tiene un id fijo y un nombre visible, ordenada por donde trabaja, para prenderlas y apagarlas desde un solo sitio.

---

## ![](frame:GJ_colorBtn_001.png?height=18) <cy>Capturas y Texture Studio</c>

- <cl>Explorador de Assets redisenado</c>: vista previa arriba a la izquierda, buscador por ID, contador de objetos ocultos y boton de contraer o expandir todo.
- Las categorias se pliegan tocandolas (antes salian cientos de IDs de golpe), la casilla se pone <co>ambar</c> cuando solo una parte esta oculta, y cada ID trae un boton <cg>Solo</c>.
- <cl>Editar Capas</c>: ahora es un arbol que se pliega, con contador por grupo (3/7), casilla ambar a medias, buscador de rama y vista previa en vivo al lado.
- <cl>Texture Studio Fusion</c>: pegas tus imagenes o GIFs encima de los sprites del juego. Rellenado por color, 3 modos de mezcla, opacidad, escala, giro, espejo y ajuste pixel a pixel, con exportacion animada.
- <cl>GIF a spritesheet</c>: convierte un GIF en un PNG en rejilla mas su JSON de tiempos.

---

## ![](frame:chestIcon_001.png?height=18) <cy>Extras</c>

- <cl>Modly</c>: navegador dentro del juego para los mods de **modly.web.app**, con catalogo, ficha de cada mod, comentarios y perfil del autor.
- <cl>Guia Paimon</c> (Paigorit V1.5): cobertura de todo el mod, frases de problema EN/ES, categorias Editor/Visuales, chips dinamicos y recomendaciones.
- <cl>Guia Paimon</c>: ahora entiende las repreguntas. Si sigues hablando del mismo tema responde sobre el tema anterior en vez de empezar de cero.
- <cl>Gradient Animations</c>: opcion <cg>Custom</c> para armar tu animacion desde cero. Hasta 4 capas con su movimiento, ritmo, fuerza, velocidad y punto de arranque, mas 6 recetas listas (Wobble, Heartbeat, Swirl, Glitch, Liquid y Vortex).
- <cl>Codigo de moderador</c>: nuevo flujo para crearlo o rotarlo verificandolo con un comentario en tu perfil, y boton para copiarlo.

---

## ![](frame:GJ_optionsBtn_001.png?height=18) <cy>Correcciones</c>

- <cl>Cancion Dinamica</c>: ya no se cuela un trozo de musica de menu por encima de la cancion del nivel al entrar.
- <cl>Level Requests</c>: arreglado el descentrado de las caras de dificultad y los brillos de featured, epic, legendary y mythic en la pagina web. Los PNG venian recortados al ras, asi que la pagina perdia el margen que usa el juego. Ahora comparten lienzo y se apilan igual que en el juego, y las dificultades guardan su tamano real entre si.
- <cl>Texture Studio</c>: arreglados los sprites descentrados en los packs exportados. Las hojas guardan algunos sprites girados 90 grados y el exportador los recortaba con el ancho y el alto cambiados. <co>Hay que volver a exportar el pack.</c>
- <cl>Capturadora</c>: arreglada la mini vista previa de Editar Capas y del Explorador de Assets, que salia diminuta en la esquina de un recuadro negro. Ahora llena su recuadro, se refresca sola y respeta si escondiste al Jugador 1 o 2.
- <cl>Capturadora</c>: los avisos de calidad grafica, Low Detail Mode y jugador muerto ahora salen en espanol.
- <cl>Capturadora</c>: el boton de carpeta ya abre la carpeta cuando tu usuario de Windows lleva acentos.
- <cl>Explorador de Assets</c>: ocultar un tipo de objeto ya no recorre el nivel entero por cada click, y abrir el panel en un nivel enorme ya no guarda una copia de todos los objetos.
- <cl>Perfiles</c>: se elimino el cache en disco de getGJUserInfo y ahora se usa el nativo del juego. Corrige el estado de amistad viejo (no aparecer como amigo despues de aceptar).
- <cl>Texturas rotas</c> al cambiar resolucion, pantalla completa o calidad de texturas: el juego recrea el contexto grafico y tira todas las texturas. Ahora el mod suelta las suyas antes y las rearma despues.
- <cl>Memoria</c>: las portadas y thumbnails que mirabas se quedaban en VRAM toda la sesion. Ahora solo se mantienen las ultimas y el resto se libera.
- <cl>Search History</c>: corregido que todas las entradas mostraran "Demon".

---

# <cy>v1.0.9</c>

**Fix Update**
- Se eliminaron varios archivos que provocaban crash.
- Collab Editor agregado en beta cerrada. Estara disponible para todos el 20 de julio de 2026 con v1.1.0.
- Editor: nuevo color picker con Ctrl + G.
- Editor: nueva rotacion con Alt + click derecho.

# <cy>v1.0.8</c>

**Resumen**
- Nuevo Paimon Hub con skin GD.
- Menu Music y Paimon Icons redisenados.
- Texture Studio mejorado con coloreado mas estable.
- Perfil redisenado y mas opciones visuales.
- Correcciones de rendimiento, audio y estabilidad.

# <cy>v1.0.7</c>

**Resumen**
- Busqueda de canciones por nombre.
- Filtros para Mis niveles.
- Menu Physics agregado.
- Nuevos ajustes para estas funciones.

# <cy>v1.0.6</c>

## English

**Fixes and Optimizations**
- Mod optimization (resolved bottlenecks in image, GIF and request loading).
- Bugs fixed.
- Custom cursor improved.
- Pet no longer crashes in the editor.
- Emote system optimized.

**New Features**
- History.
- Texture Studio Beta 1.
- New button system.
- Popups with new animations.
- Profile gradient.
- Video with audio in profile.
- Message notification.
- New Paimbnails UI.
- Editor Music (play your music while creating in the editor).

## Espanol

**Arreglos y Optimizaciones**
- Optimizacion del mod (resolucion de cuellos de botella en carga de imagenes, GIFs y peticiones).
- Bugs arreglados.
- Custom cursor mejorado.
- Pet sin crash en el editor.
- Sistema de emotes optimizado.

**Nuevas Funciones**
- Historial.
- Texture Studio Beta 1.
- Nuevo sistema de botones.
- Popups con animaciones nuevas.
- Degradado de perfil.
- Video con audio en perfil.
- Notificacion de mensajes.
- Nueva UI de Paimbnails.
- Editor Music (pones tu musica mientras creas en el editor).

# <cy>v1.0.5</c>

## English

**Mod Previews**
- New Mod Previews feature: when you open a Geode mod, if its repository has a previews/ folder with preview-1.png ... preview-10.png, Paimbnails shows a thumbnail strip in the Details tab. Click a thumbnail to open a full-screen gallery with prev/next navigation. Toggle it from the Mod Previews setting.
- Inspired by Alphalaneous (Mod-Previews).

**Paimon Agent Mode**
- New Agent Mode toggle button under Paimon in the guide chat. Pink (agent:off) means Paimon answers questions and shows you the way; blue (agent:on) means Paimon executes actions for you.
- Visual agent execution with real clicks: in agent mode, when you press Ask, the chat closes and an AgentPilot Paimon spawns on top of the running scene (z=99999, above any popup). She flies in a soft bezier curve through a chain of targets, really clicking buttons along the way - not just opening the final popup.
- Whitelist by prefix: the ClickInvoker only fires CCMenuItem::activate() on nodes whose ID starts with flozwer.paimbnails2/. Anything outside that prefix is rejected silently, so the agent never accidentally interacts with unrelated UI.
- ActionGraph: a small registry of pre-defined click chains for the most common intents. The DSL parser routes prompts like "go to discord" to the matching chain.
- WaitForNode step polls every 0.15 s with a configurable timeout (default 2 s) until a node with the given ID appears in the scene, so the agent waits for the popup to render before flying to the next button.
- AgentDSL parser turns natural-language prompts into a small action plan with five step kinds: Open, WaitForNode, ClickButton, SetSetting and Say. Works in English/Spanish (e.g. "open discord", "go to forum", "enable discord", "set language to spanish").
- SettingsRegistry: a whitelist of mod settings the agent is allowed to modify (about 16 entries). Anything outside the whitelist is refused.
- The Agent Mode state is persisted across sessions.

**Paimon Guide (Paigorit V1)**
- Matching algorithm Paigorit V1 powering the Paimon guide chat. Paimon learns from the real titles of the popups and layers in the mod instead of hand-written keyword lists.
- PopupRegistry: every Paimbnails popup/layer is registered with its real display name, aliases, weight and an open() lambda. Each entry has a weight (1-200) so when several popups match, the most specific one wins.
- Compound keyword matching is bag-of-words: a multi-word display name matches as long as all words appear, regardless of order. Match scoring is weight + bonuses (compound match, exact token, high fuzzy similarity).
- LightLemmatizer module: English/Spanish stopwords are stripped, light suffix-based stemming, and a synonym table of about 50 entries. Reuses the existing rapidfuzz (header-only, MIT) for fuzzy similarity. No new external libraries were added.

**Compatibility**
- Removed incompatibility with thesillydoggo.blur-api. Paimbnails now coexists with mods that depend on Blur API (e.g. QOLMod). Our internal blur stays untouched and continues to work.

**Stability**
- Each phase of on game exit is now wrapped with try/catch via a safeShutdownStep helper. A failure in one shutdown step (audio, video, cache cleanup) no longer aborts the whole exit sequence; every other step still runs, saved data is persisted, and the failure is logged with the step name.
- ThumbnailLoader now releases CCImage instances via release() instead of delete, respecting Cocos2d-x reference counting.
- Build now uses C++23 (Geode 5.6.1 requires it). The previous C++20 setting could cause subtle ABI mismatches.

**Build / Toolchain**
- Project version bumped in CMakeLists.

## Espanol

**Mod Previews**
- Nueva funcion Mod Previews: al abrir un mod de Geode, si su repositorio tiene una carpeta previews/ con preview-1.png ... preview-10.png, Paimbnails muestra una tira de thumbnails en la pestana de Details. Toca un thumbnail para abrir una galeria a pantalla completa con navegacion anterior/siguiente. Se activa desde el ajuste Mod Previews.
- Inspired by Alphalaneous (Mod-Previews).

**Paimon Agent Mode**
- Nuevo boton de Agent Mode bajo Paimon en el chat de la guia. Rosa (agent:off) significa que Paimon responde preguntas y te muestra el camino; azul (agent:on) significa que Paimon ejecuta acciones por vos.
- Ejecucion visual del agente con clicks reales: en modo agente, al presionar Ask, el chat se cierra y una Paimon AgentPilot aparece sobre la escena actual (z=99999, encima de cualquier popup). Vuela en una curva bezier suave por una cadena de objetivos, haciendo click de verdad en los botones por el camino, no solo abriendo el popup final.
- Whitelist por prefijo: el ClickInvoker solo dispara CCMenuItem::activate() en nodos cuyo ID empieza con flozwer.paimbnails2/. Todo lo de fuera de ese prefijo se rechaza en silencio, asi el agente nunca interactua por accidente con UI ajena.
- ActionGraph: un pequeno registro de cadenas de click predefinidas para los intents mas comunes. El parser del DSL enruta frases como "andate a discord" a la cadena correspondiente.
- El paso WaitForNode consulta cada 0.15 s con un timeout configurable (2 s por defecto) hasta que un nodo con el ID dado aparece en la escena, asi el agente espera a que el popup se renderice antes de volar al siguiente boton.
- Parser AgentDSL convierte frases en lenguaje natural en un pequeno plan de accion con cinco tipos de paso: Open, WaitForNode, ClickButton, SetSetting y Say. Funciona en ingles/espanol (ej. "abre discord", "andate al foro", "activa discord", "pon el idioma en espanol").
- SettingsRegistry: una whitelist de ajustes del mod que el agente puede modificar (unas 16 entradas). Todo lo de fuera de la whitelist se rechaza.
- El estado de Agent Mode se guarda entre sesiones.

**Guia Paimon (Paigorit V1)**
- Algoritmo de matching Paigorit V1 que mueve el chat de la guia Paimon. Paimon aprende de los titulos reales de los popups y capas del mod en vez de listas de keywords escritas a mano.
- PopupRegistry: cada popup/capa de Paimbnails se registra con su display name real, alias, peso y un lambda open(). Cada entrada tiene un peso (1-200), asi cuando varios popups coinciden, gana el mas especifico.
- El matching compuesto es bag-of-words: un display name de varias palabras coincide mientras aparezcan todas las palabras, sin importar el orden. El scoring es peso + bonuses (match compuesto, token exacto, alta similitud difusa).
- Modulo LightLemmatizer: se quitan las stopwords ingles/espanol, stemming ligero por sufijo, y una tabla de sinonimos de unas 50 entradas. Reutiliza el rapidfuzz existente (header-only, MIT) para la similitud difusa. No se agregaron nuevas librerias externas.

**Compatibilidad**
- Eliminada la incompatibilidad con thesillydoggo.blur-api. Paimbnails ahora coexiste con mods que dependen de Blur API (ej. QOLMod). Nuestro blur interno queda intacto y sigue funcionando.

**Estabilidad**
- Cada fase del cierre del juego ahora se envuelve con try/catch via un helper safeShutdownStep. Un fallo en un paso del apagado (audio, video, limpieza de cache) ya no aborta toda la secuencia de salida; los demas pasos siguen ejecutandose, los datos guardados se persisten y el fallo queda logueado con el nombre del paso.
- ThumbnailLoader ahora libera las instancias de CCImage via release() en vez de delete, respetando el conteo de referencias de Cocos2d-x.
- El build ahora usa C++23 (Geode 5.6.1 lo requiere). El ajuste anterior de C++20 podia causar incompatibilidades sutiles de ABI.

**Build / Toolchain**
- Version del proyecto actualizada en CMakeLists.

# <cy>v1.0.4</c>

## English

**Shaders**
- New dynamic shader system with runtime parameter updates.
- Reworked shader pipeline for improved performance and flexibility.
- Popup blur effect on opened popups.
- Additional shaders bundled and selectable per layer.

**UI / Controls**
- Custom sliders with new visuals and smoother behavior.
- New dynamic input system for more responsive interactions.
- Fast circular menu for quick access to common actions.

**Audio**
- Custom menu music with per-layer overrides.

**Integrations**
- Discord Rich Presence support showing current layer and activity.

**Experimental**
- Paidraw beta: in-app drawing tool (early preview, opt-in).

**Technical**
- Removed ImagePlus dependency (no longer required to install).
- General optimization pass across rendering, audio and asset pipelines.
- Multiple bug fixes.

## Espanol

**Shaders**
- Nuevo sistema de shaders dinamico con actualizacion de parametros en runtime.
- Pipeline de shaders reescrito para mejor rendimiento y flexibilidad.
- Efecto de blur en los popups abiertos.
- Shaders adicionales incluidos y seleccionables por capa.

**UI / Controles**
- Sliders personalizados con nuevos visuales y comportamiento mas suave.
- Nuevo sistema de input dinamico para interacciones mas responsivas.
- Menu circular rapido para acceso rapido a acciones comunes.

**Audio**
- Musica de menu personalizada con overrides por capa.

**Integraciones**
- Soporte de Discord Rich Presence mostrando la capa y actividad actual.

**Experimental**
- Paidraw beta: herramienta de dibujo dentro del juego (preview temprano, opcional).

**Tecnico**
- Eliminada la dependencia de ImagePlus (ya no es necesario instalarla).
- Pase general de optimizacion en los pipelines de render, audio y assets.
- Multiples bugs arreglados.

# <cy>v1.0.1</c>

## English

**Thumbnails**
- Thumbnail previews in level cells (LevelBrowserLayer, LevelSearchLayer).
- Realtime search preview with configurable debounce.
- Local thumbnail viewer popup with gallery mode and configurable transitions.
- Concurrent download limit setting to control network usage.

**Custom Backgrounds**
- Per-layer background configuration (menu, search, gauntlet, level select, profile).
- Support for static images, GIF animations and video files (MP4).
- Video backgrounds with GPU-accelerated YUV decoding via Media Foundation (Windows), AVFoundation (macOS/iOS) and software fallback (Android/other).
- Background blur shaders (Kawase, Paimon blur) with configurable intensity.
- Shared video player cache to avoid redundant decoder instances across layers.
- Dark mode overlay and adaptive color modes for backgrounds.

**Dynamic Song System**
- Per-layer music configuration with custom paths, song IDs, speed and filter.
- Smooth audio handoff between video background audio and dynamic songs.

**Pet Companion**
- Animated pet sprite that follows the cursor on supported layers.
- Idle, sleep and reaction states tied to game events (level complete, death, practice exit).

**Badges**
- Custom badge icons alongside player names in comments and profiles.
- Adaptive font scaling for long comments.

**Emotes**
- Emote system for comments with CDN-backed asset delivery.

**Accessibility / UI**
- Transparent list mode for level browsers.
- Compact list mode.
- Button scale animation on hover for registered Paimbnails UI elements.

**Technical**
- Geode node-ids integration for cross-mod compatibility.
- Runtime lifecycle manager for ordered shutdown (audio, video, texture caches).
- Cloudflare Worker backend with CDN fallback for read-only API endpoints.
- API key authentication with optional mod-code for moderator features.

## Espanol

**Thumbnails**
- Previews de thumbnail en las celdas de nivel (LevelBrowserLayer, LevelSearchLayer).
- Preview de busqueda en tiempo real con debounce configurable.
- Popup visor de thumbnails local con modo galeria y transiciones configurables.
- Ajuste de limite de descargas concurrentes para controlar el uso de red.

**Fondos Personalizados**
- Configuracion de fondo por capa (menu, busqueda, gauntlet, seleccion de nivel, perfil).
- Soporte para imagenes estaticas, animaciones GIF y archivos de video (MP4).
- Fondos de video con decodificacion YUV acelerada por GPU via Media Foundation (Windows), AVFoundation (macOS/iOS) y fallback por software (Android/otros).
- Shaders de blur de fondo (Kawase, Paimon blur) con intensidad configurable.
- Cache compartido del reproductor de video para evitar instancias de decoder redundantes entre capas.
- Overlay de modo oscuro y modos de color adaptativos para fondos.

**Sistema de Cancion Dinamica**
- Configuracion de musica por capa con rutas propias, IDs de cancion, velocidad y filtro.
- Transicion de audio suave entre el audio del video de fondo y las canciones dinamicas.

**Pet Companion**
- Sprite de mascota animado que sigue el cursor en las capas soportadas.
- Estados de idle, sleep y reaccion ligados a eventos del juego (nivel completado, muerte, salida de practica).

**Badges**
- Iconos de badge personalizados junto a los nombres de jugador en comentarios y perfiles.
- Escalado de fuente adaptativo para comentarios largos.

**Emotes**
- Sistema de emotes para comentarios con entrega de assets via CDN.

**Accesibilidad / UI**
- Modo de lista transparente para los navegadores de niveles.
- Modo de lista compacta.
- Animacion de escala de boton al pasar el cursor sobre elementos de UI registrados de Paimbnails.

**Tecnico**
- Integracion con geode node-ids para compatibilidad entre mods.
- Manager de ciclo de vida en runtime para un apagado ordenado (audio, video, caches de textura).
- Backend en Cloudflare Worker con fallback de CDN para endpoints de API de solo lectura.
- Autenticacion por API key con mod-code opcional para funciones de moderador.
