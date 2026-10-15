'use strict';
importScripts('./analyzer.js?v=20261012');
self.onmessage = async function (event) {
  try { self.postMessage({ ok: true, report: await self.PaimbnailsLevelAudit.analyze(event.data.input) }); }
  catch (error) { self.postMessage({ ok: false, error: error.message || 'No se pudo analizar la entrada.' }); }
};
