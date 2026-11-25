# Verificación de baneos

La autoridad es la respuesta HTTPS del servidor. La caché local sirve para
aplicar un ban conocido inmediatamente; un registro local de «no baneado»
nunca concede acceso online, aunque tenga un HMAC válido.

## Comportamiento del cliente

- El monitor arranca una vez y solo consulta la red después del consentimiento.
  La identidad exige que `GJAccountManager` y `GameManager` estén disponibles,
  que ambos nombres coincidan sin distinguir mayúsculas y que el ID de cuenta sea
  positivo. `GameManager::m_playerUserID` se incluye en la identidad cuando está
  disponible, de modo que un cambio de user ID invalida la sesión. Una identidad
  incompleta o incoherente deja las funciones online bloqueadas.
- La consulta usa `https://api.flozwer.org/api/banned`, sin redirecciones,
  con parámetros de cuenta, un nonce nuevo por intento (reloj de alta
  resolución, `random_device` y contador) y cabeceras contra caché. También
  envía `X-Viewer-Token` cuando existe, como prueba adicional de propiedad.
  Cambiar la dirección de otros servicios no cambia esta autoridad.
- Se exige un objeto JSON con `banned` booleano. Un error HTTP, JSON incompleto,
  tipos incorrectos o `success: false` no conceden acceso ni borran la caché.
  Si el servidor devuelve `accountID`, `check`, `nonce` o `_check`, el valor debe
  coincidir con la cuenta y el nonce de esa consulta; una respuesta dirigida a
  otra cuenta se descarta y se reintenta.
- La autorización vive en memoria, pertenece al ID, nombre y user ID de la
  cuenta, se renueva cada 60 segundos y caduca a los 120 segundos usando un reloj
  monotónico. Un fallo definitivo de comprobación revoca el acceso antes de esa
  caducidad. Los errores transitorios tienen un máximo de tres intentos, con
  esperas de uno y dos segundos; un HTTP 429 no se reintenta dentro de la misma
  consulta.
- Las solicitudes HTTP se comprueban al enviarse y al entregar su resultado.
  WebSockets y eventos de Versus también exigen autorización. Al revocarla se
  cierran las conexiones persistentes y la sesión de edición colaborativa.
  Recuperar la conexión permite volver a usar funciones online; las sesiones
  interrumpidas de colaboración o chat pueden requerir conexión manual.
- Entre las dos copias de caché prevalece cualquier ban autenticado, incluso
  frente a un permiso con fecha posterior. Cuando gana un ban, ambas copias se
  reparan con el mismo registro sellado, de modo que borrar `.paimon` o el valor
  de `save.json` por separado no revela un permiso antiguo. Retrasar el reloj no
  descarta un ban. Borrar, reemplazar o corromper ambas copias nunca concede
  acceso online.
- Las razones se guardan y se muestran como texto de pantalla: se eliminan
  controles C0, se normalizan saltos de línea y se recorta sin partir caracteres
  UTF-8. Los nombres de cuenta se validan antes de usarse en una acción
  administrativa.
- Una respuesta se descarta si la cuenta cambió durante la consulta. Las
  respuestas pendientes tampoco sobreviven a la revocación de su autorización.
- Un ban conocido bloquea la inicialización y queda retenido durante esa sesión,
  incluso al cambiar de cuenta. Un desbaneo confirmado por el servidor actualiza
  ambas copias y surte efecto al reiniciar. La consulta de baneos sigue disponible
  con el cliente bloqueado para poder registrar ese desbaneo.
- Mientras el ban esté activo, el aviso no desaparece de forma permanente: si se
  cierra o una transición de escena lo elimina, el monitor lo vuelve a mostrar.
  Los atajos extendidos tampoco disparan acciones. El popup no se puede cerrar
  con el botón ni con la tecla de retroceso.
- Banear, desbanear y leer la lista exigen una sesión de moderador volátil y
  reciente (`modauth::isVerified()`). El actor y el ID se toman de esa sesión,
  no de los campos crudos de GD. El cliente rechaza autobaneos, nombres
  inválidos, razones vacías y acciones duplicadas mientras hay una petición en
  curso. La lista se limita y se sanea antes de dibujarla.
- Si el servidor no puede verificar una cuenta sin ban conocido, las funciones
  online quedan bloqueadas; esto no inventa un ban ni impide usar GD sin el mod.

## Límite de confianza y servidor pendiente

El código de producción del servidor no está en esta copia. Un cliente que el
usuario puede modificar o recompilar puede eliminar sus comprobaciones locales.
El HMAC incorporado en el mod detecta alteraciones accidentales y ediciones
simples; su clave distribuida con el cliente no es un secreto del servidor.
Del mismo modo, `X-Viewer-Token`, el nonce y las comprobaciones de identidad
locales mejoran la correlación y cierran ataques triviales, pero no convierten
al cliente en una autoridad.

Para rechazar también clientes modificados, el servidor debe:

1. Autenticar la propiedad de la cuenta con una sesión firmada y obtener de ella
   el ID de cuenta; no confiar en el nombre/ID enviados por el cliente.
2. Consultar el ban en cada operación protegida y revocar las sesiones activas
   cuando se aplica un ban. El ban debe cubrir HTTP, WebSockets y servicios
   relacionados, no solo el inicio de sesión.
3. Verificar los roles de moderador en el servidor para cada acción
   administrativa, con rate limiting y registro de auditoría. La clave API y los
   roles locales del cliente no son autorización.
4. Firmar o ecoar el nonce de la comprobación de ban y responder siempre para la
   cuenta autenticada, incluso cuando el cliente intenta cambiar los parámetros.
5. Tratar la caché y el popup como mecanismos de experiencia de usuario, no como
   controles de seguridad. La validación local no sustituye estas comprobaciones.

## Riesgos residuales

- Un usuario con control del binario puede saltarse el monitor, el popup y los
  atajos. Solo la aplicación del lado del servidor puede impedir el uso de los
  servicios.
- La clave HMAC y el `X-API-Key` están distribuidos con el cliente; sirven para
  integridad accidental y compatibilidad, no como secretos.
- La caché puede conservar un ban después de un desbaneo legítimo hasta que una
  respuesta live lo reemplace; es un fallo deliberado hacia la seguridad.
- Un cambio de nombre o de user ID durante la sesión invalida la autorización
  online hasta que una comprobación live confirme la nueva identidad.

## Pruebas

Las regresiones de caché, política de sesión, transporte y entrega de solicitudes
viven en [las pruebas de callbacks](../tests/review_fixtures/), se descubren
automáticamente y se ejecutan con AddressSanitizer y UndefinedBehaviorSanitizer:

```powershell
python tests/review_behavior_regression.py --build-dir build-clang --case ban_cache --case ban_policy --case ban_transport --case ban_web_dispatch --case websocket_nonblocking
python tests/review_guards_regression.py
```
