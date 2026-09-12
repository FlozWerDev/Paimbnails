# Auditoria de crash logs y compatibilidad (2026-09-12)

Fuente revisada: Bunny CDN, `system/data/crashlogs/index.json` y los 103 cuerpos
referenciados por el indice. Los 103 JSON se descargaron y validaron. Para cada
reporte se extrajo la version desde `Installed Mods`: el campo `modVersion` del
indice describe la version del cliente que subio el archivo y no siempre la que
se habia caido.

## Resultado

- 3 crashes atribuidos directamente a `flozwer.paimbnails2`.
- 20 con Paimbnails en los primeros tres frames: probable responsabilidad o
  memoria danada que aflora en Paimbnails.
- 21 interacciones de hook/lifetime con Paimbnails entre los primeros nueve frames.
- 27 atribuidos por Geode a otro mod y sin frame de Paimbnails.
- 25 externos o incidentales: Paimbnails aparece tarde o no aparece.
- 7 sin stack suficiente para atribuirlos.

Los tres directos son:

- `1786633225667-u9vb0rr`, v1.1.0: callback viejo de GIF Import
  (`refreshPreview`/`applyProcessed`). Es historico y el flujo actual ya usa
  tokens de vida y descarte de resultados obsoletos.
- `1788579910441-9ufn34j`, v1.0.9: destruccion de metadata/nodos al salir del
  proceso. Es historico; el cierre actual ya limpia subscriptores y recursos
  antes de Cocos, y esta revision agrega cancelacion centralizada de tareas.
- `1789009781224-mp2drfy`, v1.1.2: reemplazo reentrante de una captura con
  Globed, CDC Level Thumbnails e ImagePlus activos. Se corrige difiriendo el
  callback desplazado y usando captura del back-buffer con renderers externos.

## Cambios derivados

- Cancelacion de todos los delays, selectores y callbacks antes de desmontar
  `CCScheduler`/`WeakRefPool`.
- Cancelacion temprana de file pickers y tareas del actualizador; los
  `TaskHolder` globales ya no se destruyen despues de `geode::async::runtime`.
- Cache de miniaturas y Collab usan vida de proceso con limpieza explicita;
  no ejecutan destructores que toquen Cocos, red o logger despues de Geode.
- `EventBus` rechaza subscripciones tardias y su flag de cierre es atomico.
- Captura conservadora con Globed, CDC, Eclipse, Tinker o Mega Hack; evita una
  segunda visita a `PlayLayer` dentro de un FBO ajeno.
- El nodo solicitado para captura queda retenido hasta el pre-swap.
- Los callbacks reemplazados/fallidos se entregan despues del frame, sin
  reentrar la maquina de estados.
- Collab deja de hookear `undo`, `redo`, `select` y `deselect`; la presencia de
  seleccion se sondea cada 100 ms. Fuera de una sala conectada, sus hooks de
  mutacion son no-op.

## Revision individual

La categoria se decide por `Faulty Mod`, posicion del primer frame de
Paimbnails y contexto del stack. Un frame tardio no prueba causalidad; por eso
se conserva separado como incidental.

### Paimbnails directo (3)

- `1786633225667-u9vb0rr` — v1.1.0
- `1788579910441-9ufn34j` — v1.0.9
- `1789009781224-mp2drfy` — v1.1.2

### Probable Paimbnails (20)

- `1786629681254-669bbu3` — v1.1.0
- `1786930323781-b6p1k98` — v1.0.9
- `1786966446942-m0l55m2` — v1.0.9
- `1787084599264-6cjew7p` — v1.1.0
- `1787319646186-iz7lj1q` — v1.1.1; Geode metadata
- `1787321631214-j4argsp` — v1.1.1; Geode metadata
- `1787321688682-7zqaadb` — v1.1.1; Geode metadata
- `1787321859930-rjzxhpe` — v1.1.1; Geode metadata
- `1787609404152-s64xlpa` — v1.1.1
- `1787623206156-5g7xjct` — v1.1.1
- `1787623345718-ebm0hc5` — v1.1.1
- `1787692417372-w4wq5el` — v1.1.1
- `1787692551756-b27aywd` — v1.1.1
- `1787692713037-q0xj7w8` — v1.1.1
- `1788225102065-zj1800j` — v1.1.1
- `1788553690534-fh0bklu` — v1.1.0
- `1788674582869-9pcwgc6` — v1.1.1
- `1788703654947-xrc2zk2` — v1.1.0; Geode metadata
- `1788818894820-8k42qjg` — v1.1.0
- `1789089319092-14nkqyv` — v1.1.0

### Interaccion de hooks o lifetime (21)

- `1786675405723-j7bir3q` — v1.0.9
- `1786753921269-v0ousnr` — v1.0.9
- `1786753921426-zi6vdvz` — v1.0.9
- `1787012881245-f9674km` — v1.0.9
- `1787012881521-v3ztfzj` — v1.0.9
- `1787084599025-x0d041o` — v1.0.9
- `1787257414398-d5tzls4` — v1.1.1
- `1787320934056-5lj27f0` — v1.1.1
- `1787360371206-0e8jct6` — v1.0.9
- `1787872797273-nq87g3f` — v1.1.1
- `1788148445973-vcn2aoj` — v1.1.1; Tinker
- `1788383385836-utrc86v` — v1.1.2; `EditorUI::deselectAll`
- `1788567785402-o8hw3it` — v1.1.1
- `1788620527441-feksh53` — v1.1.0; Geode JSON
- `1788653217030-51mrpwz` — v1.1.2; async/waker
- `1788657936145-3o4cutk` — v1.1.2; duplicado del anterior
- `1788703654821-wqtngtj` — v1.1.0
- `1788706029742-frxr9q9` — v1.1.2; `EditorUI::undoLastAction`
- `1788706635619-oxwiigs` — v1.1.2; runtime async durante salida
- `1788774246026-vd2ao7a` — v1.1.2; `WeakRefPool` en callback diferido
- `1788961948443-8lqfhvo` — v1.1.1; browser/BetterInfo

### Atribuido a otro mod, sin frame de Paimbnails (27)

- `1786677674178-idil25v` — Editortab API
- `1787242499269-miknohr` — Death Tracker
- `1787259920548-plt2vk0` — Attempt Playback
- `1787320506310-byty41o` — Geode Loader
- `1787530239133-cz8v19j` — Geode Loader
- `1787848156096-vwevndr` — Tinker
- `1787855982190-96vgx0q` — Tinker
- `1787976463263-wfe9rw2` — Options API
- `1788562974465-hgqkn39` — Tinker
- `1788562974476-t21y8mu` — Tinker
- `1788563907227-1di8f0e` — Geode Loader
- `1788563907671-pb77mbk` — Tinker
- `1788574335061-5eouhu1` — Music Integrations
- `1788574946781-wp1kjx2` — Editortab API
- `1788662763636-o869lr5` — Death Tracker
- `1788673337982-hu4r1lp` — Geode Loader
- `1788735281661-g7gis0a` — Geode Loader
- `1788751367667-em31yc2` — Geode Loader
- `1788804155982-wrvsfqd` — Geode Loader
- `1788920829745-uuw0uap` — Tinker
- `1788922797095-92hoxud` — Tinker; duplicado del anterior
- `1788922797210-f4qivoi` — Tinker
- `1788928316025-rxkgocs` — Tinker/editor-collab
- `1788949974593-b3yyts8` — Geode Loader async
- `1789049851774-fssukdb` — Geode Loader async
- `1789095566703-hwhpqpt` — Geode Loader
- `1789106138732-w3pu7id` — Geode Loader

### Externo o Paimbnails incidental (25)

- `1786675405230-sznorja` — v1.0.9
- `1786676020165-piih383` — v1.1.1
- `1786743919639-yleru7o` — v1.0.0-alpha.1, Android runtime
- `1786771139852-aycgfqd` — version desconocida, editor-collab-ui
- `1786930324024-agn7uif` — v1.1.0, driver Intel OpenGL
- `1787008917704-54feviv` — v1.0.9
- `1787174449224-ygdi32n` — v1.1.1, More Icons
- `1787242499320-egz9nj0` — version desconocida
- `1787603941174-johzfae` — v1.0.1
- `1787603941228-cb5rms3` — v1.0.1
- `1787623121856-9afg58m` — v1.1.1
- `1788574335129-e7mbxij` — v1.0.6, Mega Hack
- `1788579910560-m89fcsj` — v1.0.9, purga de CCDirector
- `1788620527396-790fuq5` — v1.1.0
- `1788657936140-3cbpsad` — v1.1.2, Windows Explorer
- `1788715605385-ajeoyfo` — version desconocida, DiceDash
- `1788715605460-5nvugpp` — duplicado del anterior
- `1788740441816-5es22ht` — v1.1.1, More Icons
- `1788774246048-ht4fojq` — v1.1.2, Windows Storage/threadpool
- `1788791110326-269t6a7` — v1.1.1
- `1788926521876-unpy62v` — v1.1.1
- `1788996549764-rdskq2a` — v1.1.1, Gauntlet
- `1789087563351-32fya31` — v1.1.2, Gauntlet/Better Gauntlets
- `1789088014523-0pvna7a` — duplicado de la familia anterior
- `1789176723035-x8jjqii` — v1.1.1, Mega Hack

### Sin stack suficiente (7)

- `1787848194097-0davf7x` — v1.1.1
- `1788381455944-yge8dgj` — version desconocida
- `1788553690382-ire85iv` — v1.1.0
- `1788574409567-fy4nrgv` — v1.1.2
- `1788703654826-inr35ux` — version desconocida
- `1789009780876-j66tt9x` — v1.1.2; log termina tras cierre limpio
- `1789074830845-rcvyf4b` — v1.1.2; log termina tras cierre limpio

## Limites de la atribucion

Los PDB locales corresponden a una compilacion v1.1.1 del 26 de agosto. Se
usaron solamente como apoyo para esa familia; no se aplicaron sus simbolos a
v1.1.2 ni a versiones anteriores. Los reportes sin stack no permiten afirmar
que Paimbnails sea la causa aunque hayan sido enviados desde su pagina.
