(function (root) {
  'use strict';
  const VERSION = '0.1.0';
  const LIMITS = Object.freeze({ inputBytes: 8 * 1024 * 1024, decodedBytes: 32 * 1024 * 1024, objects: 100000, pairs: 512, valueLength: 4096 });
  const IMAGE_IDS = new Set([211, 3637, 693, 694]);
  const TRIGGER_IDS = new Set([901, 1007, 1268]);
  const encoder = new TextEncoder();
  const numberPattern = /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/;

  function number(value, fallback = null) {
    if (typeof value !== 'string' || !numberPattern.test(value)) return fallback;
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : fallback;
  }

  function integer(value) {
    if (typeof value !== 'string' || !/^\d+$/.test(value)) return null;
    const parsed = Number(value);
    return Number.isSafeInteger(parsed) ? parsed : null;
  }

  function histogram(values) {
    const counts = new Map();
    for (const value of values) counts.set(value, (counts.get(value) || 0) + 1);
    return counts;
  }

  function entropy(counts) {
    const total = [...counts.values()].reduce((a, b) => a + b, 0);
    if (!total) return 0;
    let result = 0;
    for (const count of counts.values()) {
      const probability = count / total;
      result -= probability * Math.log2(probability);
    }
    return result;
  }

  async function digest(text) {
    const bytes = await root.crypto.subtle.digest('SHA-256', encoder.encode(text));
    return [...new Uint8Array(bytes)].map(value => value.toString(16).padStart(2, '0')).join('');
  }

  async function unpack(input) {
    if (typeof input !== 'string' || !input.trim()) throw new Error('La cadena está vacía.');
    if (encoder.encode(input).length > LIMITS.inputBytes) throw new Error('La entrada supera 8 MiB.');
    const trimmed = input.trim();
    if (trimmed.includes(';') || /^(?:k[A-Za-z0-9]+|1),/.test(trimmed)) return { text: input, format: 'raw' };
    const compact = trimmed.replace(/\s/g, '');
    if (!/^[A-Za-z0-9+/_-]+={0,2}$/.test(compact) || compact.length % 4 === 1) throw new Error('Formato no reconocido: usa una cadena de nivel, no un contenedor .bro, XML o JSON.');
    if (!root.DecompressionStream) throw new Error('Este navegador no admite descompresión local. Usa la cadena descomprimida.');
    let binary;
    try { binary = root.atob(compact.replace(/-/g, '+').replace(/_/g, '/')); }
    catch (_) { throw new Error('Base64 no válido.'); }
    const bytes = Uint8Array.from(binary, value => value.charCodeAt(0));
    const gzip = bytes[0] === 0x1f && bytes[1] === 0x8b;
    const zlib = bytes.length >= 2 && (bytes[0] & 15) === 8 && (bytes[0] >> 4) <= 7 && ((bytes[0] << 8) + bytes[1]) % 31 === 0;
    if (!gzip && !zlib) throw new Error('La entrada Base64 no contiene un flujo gzip o zlib reconocido.');
    const reader = new Blob([bytes]).stream().pipeThrough(new root.DecompressionStream(gzip ? 'gzip' : 'deflate')).getReader();
    const chunks = [];
    let total = 0;
    try {
      while (true) {
        const next = await reader.read();
        if (next.done) break;
        total += next.value.length;
        if (total > LIMITS.decodedBytes) {
          await reader.cancel();
          throw new Error('La cadena descomprimida supera 32 MiB.');
        }
        chunks.push(next.value);
      }
    } finally { reader.releaseLock(); }
    const decoded = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) { decoded.set(chunk, offset); offset += chunk.length; }
    let text;
    try { text = new TextDecoder('utf-8', { fatal: true }).decode(decoded); }
    catch (_) { throw new Error('El flujo no contiene texto UTF-8 válido.'); }
    return { text, format: gzip ? 'base64-gzip' : 'base64-zlib' };
  }

  function parse(text) {
    if (encoder.encode(text).length > LIMITS.decodedBytes) throw new Error('La cadena supera 32 MiB.');
    text = text.trim();
    const objects = [];
    let header = null;
    let cursor = 0;
    let recordIndex = 0;
    while (cursor < text.length) {
      const delimiter = text.indexOf(';', cursor);
      const end = delimiter < 0 ? text.length : delimiter;
      const record = text.slice(cursor, end);
      cursor = end + 1;
      if (!record) throw new Error('Hay un registro vacío dentro de la cadena.');
      const tokens = record.split(',');
      if (tokens.length % 2 || tokens.length > LIMITS.pairs * 2) throw new Error('Registro ' + recordIndex + ': pares incompletos o demasiadas propiedades.');
      const isHeader = recordIndex === 0 && /^k[A-Za-z0-9]+$/.test(tokens[0]);
      const props = Object.create(null);
      const keys = [];
      for (let i = 0; i < tokens.length; i += 2) {
        const key = tokens[i];
        const value = tokens[i + 1];
        if (!(isHeader ? /^k[A-Za-z0-9]+$/.test(key) : /^(?:0|[1-9]\d*)$/.test(key))) throw new Error('Registro ' + recordIndex + ': clave no válida.');
        if (Object.hasOwn(props, key)) throw new Error('Registro ' + recordIndex + ': clave duplicada ' + key + '.');
        if (!value.length || value.length > LIMITS.valueLength) throw new Error('Registro ' + recordIndex + ': valor vacío o demasiado largo.');
        props[key] = value;
        keys.push(key);
      }
      if (isHeader) header = { props, keys };
      else {
        if (objects.length >= LIMITS.objects) throw new Error('El laboratorio admite hasta 100.000 objetos.');
        const id = integer(props['1']);
        if (id === null || id <= 0) throw new Error('Registro ' + recordIndex + ': falta un ID de objeto positivo.');
        const x = number(props['2']), y = number(props['3']);
        for (const key of ['2', '3', '6', '128', '129']) {
          if (Object.hasOwn(props, key) && number(props[key]) === null) throw new Error('Registro ' + recordIndex + ': número no finito o no válido en ' + key + '.');
        }
        const groups = props['57'] === undefined ? [] : props['57'].split('.').map(integer);
        if (groups.some(value => value === null || value <= 0)) throw new Error('Registro ' + recordIndex + ': lista de grupos no válida.');
        objects.push({ index: objects.length, id, x, y, rotation: number(props['6'], 0), scaleX: number(props['128'], 1), scaleY: number(props['129'], 1), color: integer(props['21']), groups: [...new Set(groups)], props, keys });
      }
      recordIndex++;
    }
    if (!objects.length) throw new Error('No se encontraron objetos.');
    return { objects, header };
  }

  function close(a, b) { return Math.abs(a - b) <= 0.002 + Math.max(Math.abs(a), Math.abs(b)) * 0.001; }
  function ratioMark(a, b) {
    const total = Math.abs(a) + Math.abs(b);
    if (total <= 0.0001) return 0;
    const ratio = Math.min(Math.abs(a), Math.abs(b)) / total;
    if (Math.abs(ratio - 0.381966) < 0.003) return 1;
    if (Math.abs(ratio - 0.438447) < 0.003) return 2;
    return 0;
  }

  function pairMark(a, b) {
    if (a.color !== b.color) return 0;
    const difference = Math.abs(a.rotation - b.rotation) % 360;
    if (Math.min(difference, 360 - difference) >= 0.01) return 0;
    const width = close(Math.abs(a.scaleY), Math.abs(b.scaleY)) ? ratioMark(a.scaleX, b.scaleX) : 0;
    const height = close(Math.abs(a.scaleX), Math.abs(b.scaleX)) ? ratioMark(a.scaleY, b.scaleY) : 0;
    if (!width && !height) return 0;
    const angle = a.rotation * Math.PI / 180;
    const dx = b.x - a.x, dy = b.y - a.y;
    const localX = dx * Math.cos(angle) - dy * Math.sin(angle);
    const localY = dx * Math.sin(angle) + dy * Math.cos(angle);
    if (width) {
      const distance = 15 * (Math.abs(a.scaleX) + Math.abs(b.scaleX));
      const tolerance = 0.012 + distance * 0.001;
      if (Math.abs(Math.abs(localX) - distance) <= tolerance && Math.abs(localY) <= tolerance) return width;
    }
    if (height) {
      const distance = 15 * (Math.abs(a.scaleY) + Math.abs(b.scaleY));
      const tolerance = 0.012 + distance * 0.001;
      if (Math.abs(Math.abs(localY) - distance) <= tolerance && Math.abs(localX) <= tolerance) return height;
    }
    return 0;
  }

  function watermark(objects) {
    let previous = null, pairs = 0, variants = 0, rotationMarks = 0, flaggedRotationMarks = 0;
    const matches = [];
    for (const object of objects) {
      if (!IMAGE_IDS.has(object.id) || !object.color || object.color <= 0) { previous = null; continue; }
      const flags = ['64', '67', '121', '134'].filter(key => object.props[key] === '1').length;
      const turns = Math.floor(object.rotation / 360 + 0.0001);
      if (object.rotation >= 0 && [6, 7, 11, 12].includes(turns)) {
        rotationMarks++;
        if (flags >= 3) flaggedRotationMarks++;
        if (matches.length < 32) matches.push({ kind: 'rotation', object: object.index, rotation: object.rotation, flags });
      }
      const candidate = object.id === 211 && object.x !== null && object.y !== null && object.props['128'] !== undefined && object.props['129'] !== undefined;
      if (!candidate) { previous = null; continue; }
      const variant = previous ? pairMark(previous, object) : 0;
      if (variant) {
        pairs++;
        variants |= variant;
        if (matches.length < 32) matches.push({ kind: 'pair', objects: [previous.index, object.index], ratioVariant: variant });
        previous = null;
      } else previous = object;
    }
    const geometryPairs = variants === 3 ? pairs : 0;
    return { compatible: flaggedRotationMarks > 0 || rotationMarks >= 3 || geometryPairs >= 3, rotationMarks, flaggedRotationMarks, geometryPairs, ratioVariants: variants, evidence: matches, authenticated: false, provesAI: false, note: 'Compatibilidad con reglas del importador Paimbnails; flags y marcas geométricas no son firmas criptográficas. El conteo completo puede superar el retorno temprano del detector C++.' };
  }

  function summaries(objects) {
    const positions = objects.filter(object => object.x !== null && object.y !== null);
    const grid = [0.25, 0.5, 1, 3, 5, 10, 15, 30].map(step => {
      let aligned = 0;
      for (const object of positions) {
        const residualX = Math.abs(object.x / step - Math.round(object.x / step));
        const residualY = Math.abs(object.y / step - Math.round(object.y / step));
        if (residualX <= 0.0001 && residualY <= 0.0001) aligned++;
      }
      return { step, aligned, fraction: positions.length ? aligned / positions.length : null };
    });
    const idCounts = histogram(objects.map(object => object.id));
    const keyOrders = histogram(objects.map(object => object.keys.join(',')));
    const shapeCounts = histogram(objects.map(object => [object.id, object.props['6'] || '0', object.props['128'] || '1', object.props['129'] || '1', object.props['21'] || '', object.props['24'] || '', object.props['25'] || ''].join('|')));
    const membership = new Map();
    const targets = new Set();
    let supportedTriggerCount = 0;
    for (const object of objects) {
      for (const group of object.groups) membership.set(group, (membership.get(group) || 0) + 1);
      if (TRIGGER_IDS.has(object.id)) {
        supportedTriggerCount++;
        const target = integer(object.props['51']);
        if (target !== null && target > 0) targets.add(target);
      }
    }
    const top = counts => [...counts].sort((a, b) => b[1] - a[1] || (String(a[0]) < String(b[0]) ? -1 : String(a[0]) > String(b[0]) ? 1 : 0)).slice(0, 12).map(([value, count]) => ({ value, count }));
    const decimalPrecision = key => top(histogram(objects.filter(object => Object.hasOwn(object.props, key)).map(object => {
      const literal = object.props[key];
      return /[eE]/.test(literal) ? 'scientific' : ((literal.split('.')[1] || '').length);
    })));
    return {
      objectCount: objects.length, positionedObjects: positions.length, imagePrimitiveCount: objects.filter(object => IMAGE_IDS.has(object.id)).length,
      objectIdEntropyBits: entropy(idCounts), uniqueObjectIds: idCounts.size, topObjectIds: top(idCounts),
      keyOrderEntropyBits: entropy(keyOrders), distinctKeyOrders: keyOrders.size, dominantKeyOrderFraction: Math.max(...top(keyOrders).map(entry => entry.count)) / objects.length,
      gridAlignment: grid, repeatedShapeFraction: objects.length ? (objects.length - shapeCounts.size) / objects.length : 0,
      repetitionDefinition: 'Misma tupla literal ID/rotación/escalas/color/capas, omitiendo posición y grupos; no reconoce diseños completos.',
      lexicalDecimalPrecision: { x: decimalPrecision('2'), y: decimalPrecision('3'), scaleX: decimalPrecision('128'), scaleY: decimalPrecision('129') },
      groups: { distinctMembershipGroups: membership.size, supportedTriggerCount, distinctSupportedTargets: targets.size, targetsWithoutDirectMembership: [...targets].filter(group => !membership.has(group)).slice(0, 32), semantics: 'Solo IDs 901, 1007 y 1268 y campo 51. Una referencia sin miembros no demuestra error ni IA.' }
    };
  }

  function canonicalRecord(record) {
    return record.keys.slice().sort((a, b) => a < b ? -1 : a > b ? 1 : 0).map(key => [key, record.props[key]]);
  }

  async function analyze(input) {
    const unpacked = await unpack(input);
    const parsed = parse(unpacked.text);
    const metrics = summaries(parsed.objects);
    const mark = watermark(parsed.objects);
    const canonical = JSON.stringify({ header: parsed.header ? canonicalRecord(parsed.header) : null, objects: parsed.objects.map(object => JSON.stringify(canonicalRecord(object))).sort() });
    return {
      schema: 'paimbnails.level-analysis/0.1', analyzerVersion: VERSION,
      status: 'indeterminate', aiProbability: null, attributedModel: null, policyAction: 'none',
      explanation: 'La cadena permite describir patrones y compatibilidad con herramientas. No existe aquí un corpus calibrado ni una prueba de procedencia de IA.',
      input: { format: unpacked.format, bytes: encoder.encode(input).length, decodedBytes: encoder.encode(unpacked.text).length, headerPresent: !!parsed.header, rawSha256: await digest(input), decodedSha256: await digest(unpacked.text), propertyMultisetSha256: await digest(canonical), canonicalization: 'property-multiset-v1: claves ordenadas por comparación UTF-16; orden de objetos ignorado, valores literales y duplicados de objetos preservados. Solo comparación sintáctica; no es hash semántico de GD.' },
      metrics, importerWatermark: mark,
      preview: parsed.objects.filter(object => object.x !== null && object.y !== null).filter((_, index) => index % Math.max(1, Math.ceil(metrics.positionedObjects / 2500)) === 0).map(object => ({ index: object.index, id: object.id, x: object.x, y: object.y })),
      limitations: ['Sin calibración de autoría IA.', 'Sin inspección de prompts o pensamiento interno.', 'El exportador puede reescribir las señales léxicas.', 'Las marcas del importador se pueden copiar o perder.', 'No se reconstruye la ejecución, el render ni todo el grafo de triggers.', 'Registros desconocidos se conservan, pero no se interpreta su semántica.'],
      limits: LIMITS
    };
  }

  root.PaimbnailsLevelAudit = Object.freeze({ VERSION, LIMITS, analyze, parse, watermark });
})(typeof self !== 'undefined' ? self : globalThis);
