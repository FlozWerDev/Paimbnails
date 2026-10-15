(function () {
  'use strict';
  const byId = id => document.getElementById(id);
  const input = byId('level-input');
  const status = byId('analysis-status');
  const numberFormat = new Intl.NumberFormat('es');
  let worker = null, deadline = null, report = null, generation = 0;

  function format(value, digits = 1) { return new Intl.NumberFormat('es', { maximumFractionDigits: digits }).format(value); }
  function clearWorker() {
    if (worker) worker.terminate();
    worker = null;
    if (deadline) clearTimeout(deadline);
    deadline = null;
    byId('analyze').disabled = false;
    byId('cancel-analysis').disabled = true;
    byId('analysis-panel').setAttribute('aria-busy', 'false');
  }
  function invalidate(message) {
    generation++;
    clearWorker();
    report = null;
    byId('download-report').disabled = true;
    byId('analysis-results').hidden = true;
    for (const id of ['report-json', 'hash-summary', 'metrics-summary', 'mark-explanation', 'object-count', 'id-count', 'key-order', 'importer-state']) byId(id).textContent = '';
    for (const id of ['id-histogram', 'grid-results', 'evidence-results']) byId(id).replaceChildren();
    const canvas = byId('object-map');
    canvas.width = canvas.width;
    status.textContent = message || 'Entrada modificada. Analiza de nuevo para obtener un informe.';
  }
  function canvasContext(canvas) {
    const width = canvas.clientWidth || 600;
    const height = 260;
    const ratio = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = width * ratio;
    canvas.height = height * ratio;
    const context = canvas.getContext('2d');
    context.scale(ratio, ratio);
    context.fillStyle = '#091619';
    context.fillRect(0, 0, width, height);
    return { context, width, height };
  }
  function drawPositions() {
    if (!report) return;
    const { context, width, height } = canvasContext(byId('object-map'));
    const positions = report.preview;
    if (!positions.length) return;
    let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
    for (const object of positions) {
      minX = Math.min(minX, object.x); maxX = Math.max(maxX, object.x);
      minY = Math.min(minY, object.y); maxY = Math.max(maxY, object.y);
    }
    const spanX = maxX - minX || 1, spanY = maxY - minY || 1;
    const scale = Math.min((width - 56) / spanX, (height - 56) / spanY);
    for (const object of positions) {
      const x = 28 + (object.x - minX) * scale;
      const y = height - 28 - (object.y - minY) * scale;
      context.fillStyle = [211, 3637, 693, 694].includes(object.id) ? '#79e6db' : '#d9a877';
      context.beginPath(); context.arc(x, y, 2.6, 0, 2 * Math.PI); context.fill();
    }
    context.fillStyle = '#b7c7c8'; context.font = '11px monospace';
    context.fillText('Centros de objetos · muestra de ' + numberFormat.format(positions.length), 14, 18);
  }
  function tableBody(id, rows) {
    const body = byId(id);
    body.replaceChildren();
    for (const values of rows) {
      const row = document.createElement('tr');
      for (const value of values) { const cell = document.createElement('td'); cell.textContent = String(value); row.append(cell); }
      body.append(row);
    }
  }
  function showReport(value) {
    report = value;
    byId('analysis-results').hidden = false;
    byId('download-report').disabled = false;
    byId('object-count').textContent = numberFormat.format(value.metrics.objectCount);
    byId('id-count').textContent = numberFormat.format(value.metrics.uniqueObjectIds);
    byId('key-order').textContent = format(value.metrics.dominantKeyOrderFraction * 100) + ' %';
    byId('importer-state').textContent = value.importerWatermark.compatible ? 'Marca compatible' : 'Sin coincidencia';
    byId('mark-explanation').textContent = value.importerWatermark.compatible ? 'Se cumplen reglas de la marca del importador. No autentifican el origen ni demuestran IA.' : 'No se observó la marca con estas reglas. Esto no demuestra ausencia de importación ni de IA.';
    tableBody('id-histogram', value.metrics.topObjectIds.map(entry => [entry.value, entry.count, format(100 * entry.count / value.metrics.objectCount) + ' %']));
    tableBody('grid-results', value.metrics.gridAlignment.map(entry => [entry.step, entry.aligned, entry.fraction === null ? 'Sin posiciones' : format(entry.fraction * 100) + ' %']));
    tableBody('evidence-results', value.importerWatermark.evidence.map(entry => [entry.kind === 'rotation' ? 'Rotación' : 'Pareja', entry.kind === 'rotation' ? entry.object : entry.objects.join(', '), entry.kind === 'rotation' ? entry.rotation + '° · flags ' + entry.flags : 'Variante ' + entry.ratioVariant]));
    byId('metrics-summary').textContent = 'Entropía de IDs: ' + format(value.metrics.objectIdEntropyBits, 3) + ' bits. Repetición de tuplas: ' + format(100 * value.metrics.repeatedShapeFraction) + ' %. Grupos de pertenencia: ' + value.metrics.groups.distinctMembershipGroups + '. Triggers admitidos: ' + value.metrics.groups.supportedTriggerCount + '.';
    byId('hash-summary').textContent = 'SHA-256 de entrada: ' + value.input.rawSha256 + '\nSHA-256 descomprimido: ' + value.input.decodedSha256 + '\nComparación sintáctica: ' + value.input.propertyMultisetSha256;
    byId('report-json').textContent = JSON.stringify(value, null, 2);
    status.textContent = 'Análisis completado en este navegador. Participación de IA: indeterminada.';
    drawPositions();
  }

  byId('analyze').addEventListener('click', function () {
    invalidate('Analizando localmente…');
    const current = generation;
    const bytes = new TextEncoder().encode(input.value).length;
    if (bytes > 8 * 1024 * 1024) { status.textContent = 'La entrada supera 8 MiB.'; return; }
    try { worker = new Worker('/request-assets/papers/ai-level-transparency/analysis-worker.js?v=20261012'); }
    catch (_) { status.textContent = 'No se pudo iniciar el Worker local. Puedes descargar el núcleo para inspeccionarlo.'; return; }
    byId('analyze').disabled = true;
    byId('cancel-analysis').disabled = false;
    byId('analysis-panel').setAttribute('aria-busy', 'true');
    worker.onmessage = function (event) {
      if (current !== generation) return;
      clearWorker();
      if (event.data.ok) showReport(event.data.report);
      else status.textContent = 'No analizable: ' + event.data.error;
    };
    worker.onerror = function () { if (current === generation) invalidate('No se pudo completar el análisis local.'); };
    deadline = setTimeout(() => { if (current === generation) invalidate('Análisis interrumpido al superar 30 segundos. Usa una cadena menor.'); }, 30000);
    worker.postMessage({ input: input.value });
  });
  byId('cancel-analysis').addEventListener('click', () => invalidate('Análisis cancelado.'));
  input.addEventListener('input', () => invalidate());
  byId('clear-input').addEventListener('click', function () { input.value = ''; byId('level-file').value = ''; invalidate('Entrada e informe eliminados de esta sesión.'); });
  byId('level-file').addEventListener('change', async function (event) {
    invalidate('Leyendo archivo local…');
    const current = generation;
    const file = event.target.files[0];
    if (!file) return;
    if (file.size > 8 * 1024 * 1024) { status.textContent = 'El archivo supera 8 MiB.'; return; }
    try {
      const text = await file.text();
      if (current !== generation) return;
      input.value = text;
      status.textContent = 'Archivo cargado localmente. Pulsa Analizar para inspeccionarlo.';
    } catch (_) { if (current === generation) status.textContent = 'No se pudo leer el archivo.'; }
  });
  byId('download-report').addEventListener('click', function () {
    if (!report) return;
    const url = URL.createObjectURL(new Blob([JSON.stringify(report, null, 2)], { type: 'application/json' }));
    const link = document.createElement('a'); link.href = url; link.download = 'paimbnails-level-analysis.json'; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
  const fixtures = {
    grid: () => 'kS38,1;' + Array.from({ length: 96 }, (_, i) => '1,1,2,' + (i % 16) * 30 + ',3,' + Math.floor(i / 16) * 30 + ';').join(''),
    importer: () => 'kS38,1;' + Array.from({ length: 96 }, (_, i) => '1,211,2,' + ((i % 16) * 6).toFixed(3) + ',3,' + (Math.floor(i / 16) * 6).toFixed(3) + ',21,100,64,1,67,1,121,1,134,1,128,0.2000,129,0.2000' + (i % 24 === 0 ? ',6,' + (i % 48 === 0 ? '2160.000' : '3960.000') : '') + ';').join(''),
    triggers: () => 'kS38,1;' + Array.from({ length: 12 }, (_, i) => '1,211,2,' + i * 30 + ',3,0,21,100,57,' + (100 + i) + ';1,1007,2,' + i * 30 + ',3,60,10,0,35,1,51,' + (100 + i) + ',87,1;').join('')
  };
  document.querySelectorAll('[data-fixture]').forEach(button => button.addEventListener('click', function () {
    input.value = fixtures[button.dataset.fixture]();
    byId('level-file').value = '';
    invalidate('Fixture sintético cargado. No es un ejemplo etiquetado de IA. Pulsa Analizar.');
  }));

  function updateBaseRate() {
    const prior = Number(byId('prior').value) / 100;
    const tpr = Number(byId('sensitivity').value) / 100;
    const fpr = Number(byId('false-positive').value) / 100;
    byId('prior-value').textContent = format(prior * 100, 2) + ' %';
    byId('sensitivity-value').textContent = format(tpr * 100, 2) + ' %';
    byId('false-positive-value').textContent = format(fpr * 100, 2) + ' %';
    const truePositive = 10000 * prior * tpr;
    const falsePositive = 10000 * (1 - prior) * fpr;
    const total = truePositive + falsePositive;
    const ppv = total ? truePositive / total : null;
    byId('base-rate-result').textContent = 'Población ficticia de 10.000 niveles: ' + format(truePositive) + ' verdaderos positivos y ' + format(falsePositive) + ' falsos positivos esperados. Proporción de aciertos entre positivos: ' + (ppv === null ? 'indefinida: no hay positivos.' : format(ppv * 100) + ' %.');
    byId('true-positive-bar').style.width = (total ? 100 * truePositive / total : 0) + '%';
    byId('false-positive-bar').style.width = (total ? 100 * falsePositive / total : 0) + '%';
  }
  ['prior', 'sensitivity', 'false-positive'].forEach(id => byId(id).addEventListener('input', updateBaseRate));
  updateBaseRate();
  byId('print-paper').addEventListener('click', () => window.print());
  window.addEventListener('resize', drawPositions);
  window.addEventListener('pagehide', clearWorker);
})();
