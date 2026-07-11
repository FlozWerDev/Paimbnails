var __defProp = Object.defineProperty;
var __name = (target, value) => __defProp(target, "name", { value, configurable: true });

// src/errors.js
var AppError = class extends Error {
  static {
    __name(this, "AppError");
  }
  constructor(message, { code = "INTERNAL_ERROR", status = 500, retryable = false, details = void 0 } = {}) {
    super(message);
    this.name = "AppError";
    this.code = code;
    this.status = status;
    this.retryable = retryable;
    this.details = details;
  }
  isRetryable() {
    return this.retryable;
  }
  toJSON() {
    const obj = { error: this.message, code: this.code };
    if (this.details) obj.details = this.details;
    return obj;
  }
};
var StorageError = class extends AppError {
  static {
    __name(this, "StorageError");
  }
  constructor(message, { code = "STORAGE_ERROR", retryable = true, details } = {}) {
    super(message, { code, status: 502, retryable, details });
    this.name = "StorageError";
  }
};
var ExternalServiceError = class extends AppError {
  static {
    __name(this, "ExternalServiceError");
  }
  constructor(message, { code = "EXTERNAL_SERVICE_ERROR", status = 502, retryable = true, details } = {}) {
    super(message, { code, status, retryable, details });
    this.name = "ExternalServiceError";
  }
};
function isRetryableStatus(status) {
  return [408, 429, 500, 502, 503, 504].includes(status);
}
__name(isRetryableStatus, "isRetryableStatus");

// src/utils/retry.js
async function retryWithBackoff(fn, opts = {}) {
  const {
    maxRetries = 2,
    initialDelayMs = 200,
    maxDelayMs = 2e3,
    label = "operation"
  } = opts;
  let lastError;
  for (let attempt = 0; attempt <= maxRetries; attempt++) {
    try {
      return await fn();
    } catch (err) {
      lastError = err;
      if (err instanceof AppError && !err.isRetryable()) throw err;
      if (attempt >= maxRetries) break;
      const delay = Math.min(initialDelayMs * Math.pow(2, attempt), maxDelayMs);
      console.warn(`[Retry] ${label} attempt ${attempt + 1} failed, retrying in ${delay}ms:`, err.message);
      await new Promise((r) => setTimeout(r, delay));
    }
  }
  throw lastError;
}
__name(retryWithBackoff, "retryWithBackoff");
async function fetchWithRetry(url2, init = {}, opts = {}) {
  const { maxRetries = 2, initialDelayMs = 200, maxDelayMs = 2e3, label = url2 } = opts;
  return retryWithBackoff(async () => {
    const res = await fetch(url2, init);
    if (!res.ok && isRetryableStatus(res.status)) {
      const err = new AppError(`HTTP ${res.status}`, {
        code: "HTTP_ERROR",
        status: res.status,
        retryable: true,
        details: { url: url2, status: res.status }
      });
      throw err;
    }
    return res;
  }, { maxRetries, initialDelayMs, maxDelayMs, label });
}
__name(fetchWithRetry, "fetchWithRetry");

// src/bunny-wrapper.js
var BUNNY_TIMEOUT_MS = 1e4;
async function timedFetch(url2, init = {}) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), BUNNY_TIMEOUT_MS);
  try {
    return await fetch(url2, { ...init, signal: ctrl.signal });
  } catch (e) {
    if (e.name === "AbortError") {
      throw new StorageError(
        `Bunny request timed out (${BUNNY_TIMEOUT_MS}ms): ${url2}`,
        { retryable: true, details: { url: url2, operation: "timeout" } }
      );
    }
    throw e;
  } finally {
    clearTimeout(timer);
  }
}
__name(timedFetch, "timedFetch");
var BunnyR2Object = class {
  static {
    __name(this, "BunnyR2Object");
  }
  constructor(data) {
    this.key = data.key;
    this.version = data.etag;
    this.size = data.size;
    this.etag = data.etag;
    this.httpEtag = data.etag;
    this.uploaded = new Date(data.lastModified);
    this.httpMetadata = data.httpMetadata || {};
    this.customMetadata = data.customMetadata || {};
  }
  writeHttpMetadata(headers) {
    if (this.httpMetadata.contentType) headers.set("Content-Type", this.httpMetadata.contentType);
    if (this.httpMetadata.contentLanguage) headers.set("Content-Language", this.httpMetadata.contentLanguage);
    if (this.httpMetadata.contentDisposition) headers.set("Content-Disposition", this.httpMetadata.contentDisposition);
    if (this.httpMetadata.contentEncoding) headers.set("Content-Encoding", this.httpMetadata.contentEncoding);
    if (this.httpMetadata.cacheControl) headers.set("Cache-Control", this.httpMetadata.cacheControl);
  }
};
var BunnyR2ObjectBody = class extends BunnyR2Object {
  static {
    __name(this, "BunnyR2ObjectBody");
  }
  constructor(data, response) {
    super(data);
    this.body = response.body;
    this.bodyUsed = false;
    this.response = response;
  }
  async arrayBuffer() {
    return this.response.arrayBuffer();
  }
  async text() {
    return this.response.text();
  }
  async json() {
    return this.response.json();
  }
  async blob() {
    return this.response.blob();
  }
};
var BunnyBucket = class {
  static {
    __name(this, "BunnyBucket");
  }
  constructor(accessKeyId, secretAccessKey, endpoint, zone, subfolder) {
    this.apiKey = secretAccessKey;
    this.zone = zone;
    this.subfolder = subfolder;
    let cleanEndpoint = endpoint.replace(/\/$/, "");
    if (cleanEndpoint.includes("s3.storage.bunnycdn.com")) {
      cleanEndpoint = cleanEndpoint.replace("s3.storage.bunnycdn.com", "storage.bunnycdn.com");
    }
    this.baseUrl = `${cleanEndpoint}/${this.zone}`;
  }
  _getFullKey(key) {
    const cleanKey = (key || "").replace(/^\/+/, "").replace(/\\/g, "/");
    if (!cleanKey || cleanKey.includes("..") || cleanKey.includes("//") || cleanKey.includes("\0")) {
      throw new StorageError("Invalid storage key", { retryable: false, details: { key } });
    }
    return this.subfolder ? `${this.subfolder}/${cleanKey}` : cleanKey;
  }
  _stripPrefix(fullKey) {
    if (this.subfolder && fullKey.startsWith(this.subfolder + "/")) {
      return fullKey.substring(this.subfolder.length + 1);
    }
    return fullKey;
  }
  /**
   * @param {string} key
   * @param {object} [opts]
   * @param {boolean} [opts.skipMeta] - skip .__meta__ fetch (default: true — saves 1 HTTP request per get)
   * @param {number}  [opts.cfCacheTtl] - seconds to let CF edge cache the subrequest (0 = no cache)
   */
  async get(key, opts = {}) {
    const fullKey = this._getFullKey(key);
    const url2 = `${this.baseUrl}/${fullKey}`;
    const cacheTtl = opts.cfCacheTtl ?? 0;
    const skipMeta = opts.skipMeta !== false;
    try {
      const res = await retryWithBackoff(async () => {
        const r = await timedFetch(url2, {
          method: "GET",
          headers: { "AccessKey": this.apiKey },
          cf: { cacheEverything: true, cacheTtl }
        });
        if (r.status === 404) return r;
        if (!r.ok && isRetryableStatus(r.status)) {
          throw new StorageError(`Bunny GET ${r.status}`, { details: { url: url2, status: r.status } });
        }
        if (!r.ok) throw new StorageError(`Bunny GET ${r.status}`, { retryable: false, details: { url: url2 } });
        return r;
      }, { label: `BunnyGET:${key}` });
      if (res.status === 404) return null;
      let customMeta = {};
      if (!skipMeta) {
        try {
          const metaUrl = `${this.baseUrl}/${fullKey}.__meta__`;
          const metaRes = await timedFetch(metaUrl, {
            method: "GET",
            headers: { "AccessKey": this.apiKey },
            cf: { cacheEverything: true, cacheTtl: 0 }
          });
          if (metaRes.ok) {
            customMeta = await metaRes.json();
          }
        } catch (_) {
        }
      }
      const obj = new BunnyR2ObjectBody({
        key,
        etag: res.headers.get("ETag"),
        size: parseInt(res.headers.get("Content-Length") || "0"),
        lastModified: res.headers.get("Last-Modified"),
        httpMetadata: {
          contentType: res.headers.get("Content-Type"),
          cacheControl: res.headers.get("Cache-Control"),
          contentDisposition: res.headers.get("Content-Disposition"),
          contentEncoding: res.headers.get("Content-Encoding"),
          contentLanguage: res.headers.get("Content-Language")
        },
        customMetadata: customMeta
      }, res);
      return obj;
    } catch (e) {
      if (e instanceof StorageError) {
        console.error(`[Bunny] Transient error getting ${key}:`, e.message);
        throw e;
      }
      console.error(`[Bunny] Unexpected error getting ${key}:`, e);
      throw new StorageError(`Bunny GET failed for '${key}': ${e.message}`, {
        retryable: true,
        details: { key, operation: "get" }
      });
    }
  }
  /**
   * @param {string} key
   * @param {object} [opts]
   * @param {boolean} [opts.skipMeta] - default true
   * @param {number}  [opts.cfCacheTtl]
   */
  async head(key, opts = {}) {
    const fullKey = this._getFullKey(key);
    const url2 = `${this.baseUrl}/${fullKey}`;
    const cacheTtl = opts.cfCacheTtl ?? 0;
    const skipMeta = opts.skipMeta !== false;
    try {
      const res = await retryWithBackoff(async () => {
        const r = await timedFetch(url2, {
          method: "GET",
          headers: {
            "AccessKey": this.apiKey,
            "Range": "bytes=0-0"
          },
          cf: { cacheEverything: true, cacheTtl }
        });
        await r.arrayBuffer().catch(() => {
        });
        if (r.status === 404) return r;
        if (r.status === 206) return r;
        if (!r.ok && isRetryableStatus(r.status)) {
          throw new StorageError(`Bunny HEAD-as-GET ${r.status}`, { details: { url: url2, status: r.status } });
        }
        if (!r.ok) throw new StorageError(`Bunny HEAD-as-GET ${r.status}`, { retryable: false, details: { url: url2 } });
        return r;
      }, { label: `BunnyHEAD:${key}` });
      if (res.status === 404) return null;
      let customMeta = {};
      if (!skipMeta) {
        try {
          const metaUrl = `${this.baseUrl}/${fullKey}.__meta__`;
          const metaRes = await timedFetch(metaUrl, {
            method: "GET",
            headers: { "AccessKey": this.apiKey },
            cf: { cacheEverything: true, cacheTtl: 0 }
          });
          if (metaRes.ok) {
            customMeta = await metaRes.json();
          }
        } catch (_) {
        }
      }
      const contentRange = res.headers.get("Content-Range");
      let fileSize = 0;
      if (contentRange) {
        const match = contentRange.match(/\/(\d+)/);
        if (match) fileSize = parseInt(match[1], 10);
      }
      if (!fileSize) {
        fileSize = parseInt(res.headers.get("Content-Length") || "0", 10);
      }
      return new BunnyR2Object({
        key,
        etag: res.headers.get("ETag"),
        size: fileSize,
        lastModified: res.headers.get("Last-Modified"),
        httpMetadata: {
          contentType: res.headers.get("Content-Type"),
          cacheControl: res.headers.get("Cache-Control")
        },
        customMetadata: customMeta
      });
    } catch (e) {
      if (e instanceof StorageError) {
        console.error(`[Bunny] Transient error heading ${key}:`, e.message);
        throw e;
      }
      console.error(`[Bunny] Unexpected error heading ${key}:`, e);
      throw new StorageError(`Bunny HEAD failed for '${key}': ${e.message}`, {
        retryable: true,
        details: { key, operation: "head" }
      });
    }
  }
  async put(key, body, options = {}) {
    const fullKey = this._getFullKey(key);
    const url2 = `${this.baseUrl}/${fullKey}`;
    const uploadedAt = (/* @__PURE__ */ new Date()).toISOString();
    const headers = {
      "AccessKey": this.apiKey
    };
    if (options.httpMetadata) {
      if (options.httpMetadata.contentType) headers["Content-Type"] = options.httpMetadata.contentType;
    }
    const res = await retryWithBackoff(async () => {
      const r = await timedFetch(url2, {
        method: "PUT",
        body,
        headers
      });
      if (!r.ok && isRetryableStatus(r.status)) {
        throw new StorageError(`Bunny PUT ${r.status}`, { details: { url: url2, status: r.status } });
      }
      if (!r.ok) throw new StorageError(`Bunny PUT failed: ${r.status}`, { retryable: false, details: { url: url2 } });
      return r;
    }, { label: `BunnyPUT:${key}` });
    if (!options.skipMeta) {
      const metaData = {
        uploadedAt,
        ...options.customMetadata || {}
      };
      const metaUrl = `${this.baseUrl}/${fullKey}.__meta__`;
      try {
        await timedFetch(metaUrl, {
          method: "PUT",
          body: JSON.stringify(metaData),
          headers: {
            "AccessKey": this.apiKey,
            "Content-Type": "application/json"
          }
        });
      } catch (e) {
        console.warn(`[Bunny] Failed to write meta for ${key}:`, e);
      }
    }
    return {
      key,
      etag: "uploaded",
      uploadedAt
    };
  }
  async delete(key) {
    const fullKey = this._getFullKey(key);
    const url2 = `${this.baseUrl}/${fullKey}`;
    await retryWithBackoff(async () => {
      const r = await timedFetch(url2, {
        method: "DELETE",
        headers: { "AccessKey": this.apiKey }
      });
      if (!r.ok && r.status !== 404 && isRetryableStatus(r.status)) {
        throw new StorageError(`Bunny DELETE ${r.status}`, { details: { url: url2, status: r.status } });
      }
    }, { label: `BunnyDELETE:${key}` });
    try {
      await timedFetch(`${url2}.__meta__`, {
        method: "DELETE",
        headers: { "AccessKey": this.apiKey }
      });
    } catch (_) {
    }
  }
  async list(options = {}) {
    const prefix = options.prefix || "";
    const limit2 = options.limit || 1e3;
    const fullPrefix = this.subfolder ? `${this.subfolder}/${prefix}` : prefix;
    let listPath = fullPrefix;
    if (listPath && !listPath.endsWith("/")) {
      const lastSlash = listPath.lastIndexOf("/");
      if (lastSlash !== -1) {
        listPath = listPath.substring(0, lastSlash + 1);
      } else {
        listPath = "";
      }
    }
    const allObjects = [];
    let page2 = 0;
    const pageSize = Math.min(limit2, 500);
    let filterPrefix = "";
    if (fullPrefix && fullPrefix.startsWith(listPath)) {
      filterPrefix = fullPrefix.substring(listPath.length);
    }
    const zonePrefix = `/${this.zone}/`;
    while (allObjects.length < limit2) {
      const url2 = `${this.baseUrl}/${listPath}?page=${page2}&perPage=${pageSize}`;
      const res = await retryWithBackoff(async () => {
        const r = await timedFetch(url2, {
          method: "GET",
          headers: { "AccessKey": this.apiKey },
          cf: { cacheEverything: true, cacheTtl: 0 }
        });
        if (r.status === 404) return r;
        if (!r.ok && isRetryableStatus(r.status)) {
          throw new StorageError(`Bunny LIST ${r.status}`, { details: { url: url2, status: r.status } });
        }
        if (!r.ok) throw new StorageError(`Bunny LIST failed: ${r.status}`, { retryable: false, details: { url: url2 } });
        return r;
      }, { label: `BunnyLIST:${prefix}` });
      if (res.status === 404) {
        break;
      }
      const items = await res.json();
      if (!Array.isArray(items) || items.length === 0) break;
      for (const item of items) {
        if (item.IsDirectory) continue;
        if (item.ObjectName.endsWith(".__meta__")) continue;
        if (filterPrefix && !item.ObjectName.startsWith(filterPrefix)) continue;
        const itemFullPath = `${item.Path}${item.ObjectName}`;
        let relativePath = itemFullPath;
        if (relativePath.startsWith(zonePrefix)) {
          relativePath = relativePath.substring(zonePrefix.length);
        } else if (relativePath.startsWith("/")) {
          relativePath = relativePath.substring(1);
        }
        if (fullPrefix && !relativePath.startsWith(fullPrefix)) continue;
        allObjects.push(new BunnyR2Object({
          key: this._stripPrefix(relativePath),
          etag: "",
          size: item.Length,
          lastModified: item.LastChanged,
          httpMetadata: {}
        }));
        if (allObjects.length >= limit2) break;
      }
      if (items.length < pageSize) break;
      page2++;
    }
    return {
      objects: allObjects,
      truncated: allObjects.length >= limit2,
      cursor: null,
      delimitedPrefixes: []
    };
  }
};

// src/middleware/cors.js
var NO_STORE_CACHE_CONTROL = "no-store, no-cache, must-revalidate, max-age=0";
function noStoreHeaders(extra = {}) {
  return {
    "Cache-Control": NO_STORE_CACHE_CONTROL,
    Pragma: "no-cache",
    Expires: "0",
    ...extra
  };
}
__name(noStoreHeaders, "noStoreHeaders");
function allowedOrigin(requestOrigin, allowedOrigins) {
  if (!allowedOrigins || allowedOrigins === "*") return "*";
  if (!requestOrigin) return null;
  const allowed = allowedOrigins.split(",").map((o) => o.trim().toLowerCase());
  if (allowed.includes(requestOrigin.toLowerCase())) {
    return requestOrigin;
  }
  return null;
}
__name(allowedOrigin, "allowedOrigin");
function corsHeaders(origin, env) {
  const policy = env?.ALLOWED_ORIGINS;
  const allowed = allowedOrigin(origin, policy);
  // Strict mode: if the deployment configured a specific allowlist (not "*")
  // and the request's origin is not on it, do NOT echo a permissive
  // Access-Control-Allow-Origin. Returning "null" makes browsers block the
  // response while keeping the request itself observable for debugging.
  const isStrictMode = policy && policy !== "*";
  const aco = allowed || (isStrictMode ? "null" : "*");
  return {
    "Access-Control-Allow-Origin": aco,
    "Access-Control-Allow-Methods": "GET, POST, PUT, DELETE, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, X-API-Key, X-Mod-Code, Authorization",
    "Access-Control-Max-Age": "86400",
    ...isStrictMode ? { Vary: "Origin" } : {}
  };
}
__name(corsHeaders, "corsHeaders");
function corsNoStore(origin, env) {
  return {
    ...corsHeaders(origin, env),
    ...noStoreHeaders()
  };
}
__name(corsNoStore, "corsNoStore");
function enforceCorsPolicy(response, request, env) {
  const origin = request.headers.get("Origin");
  const headers = new Headers(response.headers);
  const policyHeaders = corsHeaders(origin, env);
  headers.set("Access-Control-Allow-Origin", policyHeaders["Access-Control-Allow-Origin"]);
  headers.set("Access-Control-Allow-Methods", policyHeaders["Access-Control-Allow-Methods"]);
  headers.set("Access-Control-Allow-Headers", policyHeaders["Access-Control-Allow-Headers"]);
  headers.set("Access-Control-Max-Age", policyHeaders["Access-Control-Max-Age"]);
  if (policyHeaders.Vary) headers.set("Vary", policyHeaders.Vary);
  else headers.delete("Vary");
  return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
}
__name(enforceCorsPolicy, "enforceCorsPolicy");
function htmlSecurityHeaders() {
  return {
    "Content-Security-Policy": "default-src 'self'; img-src 'self' https: data:; media-src 'self' https:; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; font-src 'self' https: data:; connect-src 'self' https:; frame-ancestors 'none'; base-uri 'self'; form-action 'self'",
    "X-Content-Type-Options": "nosniff",
    "X-Frame-Options": "DENY",
    "Referrer-Policy": "strict-origin-when-cross-origin",
    "Strict-Transport-Security": "max-age=31536000; includeSubDomains",
    "Permissions-Policy": "geolocation=(), microphone=(), camera=()"
  };
}
__name(htmlSecurityHeaders, "htmlSecurityHeaders");
function handleOptions(request, env) {
  const origin = request.headers.get("Origin");
  const allowed = allowedOrigin(origin, env?.ALLOWED_ORIGINS);
  if (!allowed) {
    return new Response(null, { status: 403 });
  }
  return new Response(null, {
    status: 204,
    headers: { ...corsHeaders(origin, env), ...noStoreHeaders() }
  });
}
__name(handleOptions, "handleOptions");

// src/middleware/rate-limit.js
var WINDOW_MS = 6e4;
var LIMITS = {
  anonymous: 240,
  // unauthenticated requests (x2 - cache handles most)
  authenticated: 480,
  // valid API key (x2)
  moderator: 1200
  // moderator / admin (x2)
};
var _buckets = /* @__PURE__ */ new Map();
var PRUNE_INTERVAL = 5 * 6e4;
var _lastPrune = Date.now();
function pruneStale() {
  const now = Date.now();
  if (now - _lastPrune < PRUNE_INTERVAL) return;
  _lastPrune = now;
  const cutoff = now - WINDOW_MS * 2;
  for (const [key, bucket] of _buckets) {
    if (bucket.tokens.length === 0 || bucket.tokens[bucket.tokens.length - 1] < cutoff) {
      _buckets.delete(key);
    }
  }
}
__name(pruneStale, "pruneStale");
function getClientIP(request) {
  return request.headers.get("CF-Connecting-IP") || request.headers.get("X-Real-IP") || "0.0.0.0";
}
__name(getClientIP, "getClientIP");
function checkRateLimit(request, tier = "anonymous") {
  pruneStale();
  const ip = getClientIP(request);
  const key = `${tier}:${ip}`;
  const now = Date.now();
  const limit2 = LIMITS[tier] || LIMITS.anonymous;
  let bucket = _buckets.get(key);
  if (!bucket) {
    bucket = { tokens: [] };
    _buckets.set(key, bucket);
  }
  const windowStart = now - WINDOW_MS;
  bucket.tokens = bucket.tokens.filter((t) => t > windowStart);
  if (bucket.tokens.length >= limit2) {
    const oldest = bucket.tokens[0];
    const retryAfter = Math.ceil((oldest + WINDOW_MS - now) / 1e3);
    return { allowed: false, remaining: 0, retryAfter };
  }
  bucket.tokens.push(now);
  return { allowed: true, remaining: limit2 - bucket.tokens.length };
}
__name(checkRateLimit, "checkRateLimit");
function rateLimitGuard(request, tier = "anonymous") {
  const { allowed, remaining, retryAfter } = checkRateLimit(request, tier);
  if (allowed) return null;
  return new Response(JSON.stringify({
    error: "Rate limit exceeded",
    code: "RATE_LIMITED",
    retryAfter
  }), {
    status: 429,
    headers: {
      "Content-Type": "application/json",
      "Retry-After": String(retryAfter),
      "X-RateLimit-Limit": String(LIMITS[tier] || LIMITS.anonymous),
      "X-RateLimit-Remaining": "0",
      ...corsHeaders()
    }
  });
}
__name(rateLimitGuard, "rateLimitGuard");
function oversizedRequestGuard(request, env) {
  if (!["POST", "PUT", "PATCH"].includes(request.method)) return null;
  const rawLength = request.headers.get("Content-Length");
  if (!rawLength) {
    return new Response(JSON.stringify({ error: "Content-Length required", code: "LENGTH_REQUIRED" }), {
      status: 411,
      headers: { "Content-Type": "application/json", ...corsNoStore(null, env) }
    });
  }
  const contentLength = Number(rawLength);
  const configured = Number.parseInt(env?.MAX_UPLOAD_SIZE || "52428800", 10);
  const maxUpload = Number.isSafeInteger(configured) && configured > 0 ? Math.min(configured, 64 * 1024 * 1024) : 50 * 1024 * 1024;
  const maxRequest = maxUpload + 1024 * 1024;
  if (!Number.isSafeInteger(contentLength) || contentLength < 0 || contentLength > maxRequest) {
    return new Response(JSON.stringify({ error: "Request body too large", code: "BODY_TOO_LARGE" }), {
      status: 413,
      headers: { "Content-Type": "application/json", ...corsNoStore(null, env) }
    });
  }
  return null;
}
__name(oversizedRequestGuard, "oversizedRequestGuard");

// src/services/storage.js
async function getR2Json(bucket, key) {
  const object = await bucket.get(key, { skipMeta: true });
  if (!object) return null;
  const text = await object.text();
  try {
    const clean = text.replace(/^\uFEFF/, "").trim();
    return JSON.parse(clean);
  } catch (e) {
    console.error(`Error parsing JSON for ${key}:`, e);
    return null;
  }
}
__name(getR2Json, "getR2Json");
async function putR2Json(bucket, key, data) {
  try {
    const json = JSON.stringify(data, null, 2);
    await bucket.put(key, json, {
      httpMetadata: { contentType: "application/json", cacheControl: NO_STORE_CACHE_CONTROL },
      skipMeta: true
      // system JSON files are always read with skipMeta: true — save 1 subrequest
    });
    return true;
  } catch (error) {
    console.error(`Error writing ${key}:`, error);
    throw new StorageError(`Failed to write '${key}': ${error.message}`, {
      retryable: true,
      details: { key, operation: "putJson" }
    });
  }
}
__name(putR2Json, "putR2Json");
function expandKeyVariants(baseKey) {
  const clean = baseKey.replace(/^\//, "");
  return [clean, "/" + clean];
}
__name(expandKeyVariants, "expandKeyVariants");
async function listR2Keys(bucket, prefix) {
  try {
    let keys = [];
    let cursor = void 0;
    do {
      const list = await bucket.list({ prefix, cursor });
      keys = keys.concat(list.objects.map((obj) => obj.key));
      cursor = list.truncated ? list.cursor : void 0;
    } while (cursor);
    return keys;
  } catch (error) {
    console.error(`Error listing ${prefix}:`, error);
    return [];
  }
}
__name(listR2Keys, "listR2Keys");

// src/services/cache.js
var _store = /* @__PURE__ */ new Map();
var memCache = {
  get(key) {
    const entry = _store.get(key);
    if (!entry) return void 0;
    if (Date.now() > entry.expiresAt) {
      _store.delete(key);
      return void 0;
    }
    return entry.data;
  },
  set(key, data, ttlMs) {
    _store.set(key, { data, expiresAt: Date.now() + ttlMs });
  },
  invalidate(key) {
    _store.delete(key);
  },
  invalidatePrefix(prefix) {
    for (const k of _store.keys()) {
      if (k.startsWith(prefix)) _store.delete(k);
    }
  },
  /**
   * Write-through helper: set cache + invalidate related keys in one call.
   * Use after a mutation to extend TTL instead of relying on short expiry.
   */
  setAndInvalidate(key, data, ttlMs, ...relatedKeys) {
    this.set(key, data, ttlMs);
    for (const rk of relatedKeys) this.invalidate(rk);
  },
  /** @returns {number} current entry count (useful for health/debug) */
  size() {
    return _store.size;
  }
};
var STRIP_PARAMS = ["_ts", "t", "_pv"];
var NORMALIZE_EXT_PREFIXES = ["/t/", "/profileimgs/", "/profiles/", "/backgrounds/", "/profilebackground/", "/api/download/"];
var STRIP_EXT_RE = /\.(webp|png|gif|jpg|jpeg|mp4)$/;
function cfCacheKey(request) {
  const url2 = new URL(request.url);
  for (const p of STRIP_PARAMS) url2.searchParams.delete(p);
  url2.searchParams.sort();
  const pathLower = url2.pathname.toLowerCase();
  if (NORMALIZE_EXT_PREFIXES.some((prefix) => pathLower.startsWith(prefix))) {
    url2.pathname = url2.pathname.replace(STRIP_EXT_RE, "");
  }
  return new Request(url2.toString(), { method: request.method });
}
__name(cfCacheKey, "cfCacheKey");
async function cfCacheMatch(request) {
  try {
    const cache = caches.default;
    const hit = await cache.match(request);
    return hit;
  } catch (e) {
    console.error("[CfCache] match error:", e);
    return null;
  }
}
__name(cfCacheMatch, "cfCacheMatch");
async function cfCachePut(request, response) {
  try {
    const cache = caches.default;
    await cache.put(request, response);
  } catch (e) {
    console.error("[CfCache] put failed:", e);
  }
}
__name(cfCachePut, "cfCachePut");
async function cfCacheDelete(url2) {
  try {
    const cache = caches.default;
    await cache.delete(url2);
  } catch {
  }
}
__name(cfCacheDelete, "cfCacheDelete");
function makeCacheable(original, maxAgeSec = 120, opts = {}) {
  const headers = new Headers(original.headers);
  const edgeMaxAge = Math.max(maxAgeSec, 7200);
  const swr = Math.min(edgeMaxAge, 300);
  headers.set("Cache-Control", `public, s-maxage=${edgeMaxAge}, max-age=${maxAgeSec}, stale-while-revalidate=${swr}`);
  headers.delete("Vary");
  headers.delete("Pragma");
  headers.delete("Expires");
  if (opts.cacheTag) {
    const tags = Array.isArray(opts.cacheTag) ? opts.cacheTag : [opts.cacheTag];
    headers.set("Cache-Tag", tags.join(","));
  }
  return new Response(original.body, {
    status: original.status,
    headers
  });
}
__name(makeCacheable, "makeCacheable");

// src/services/moderation.js
async function getModerators(bucket) {
  const cached = memCache.get("system_moderators");
  if (cached !== void 0) return cached;
  const data = await getR2Json(bucket, "data/moderators.json");
  const result = data?.moderators || [];
  memCache.set("system_moderators", result, 3e5);
  return result;
}
__name(getModerators, "getModerators");
async function getVips(bucket) {
  const cached = memCache.get("system_vips");
  if (cached !== void 0) return cached;
  const data = await getR2Json(bucket, "data/vips.json");
  const result = data?.vips || [];
  memCache.set("system_vips", result, 3e5);
  return result;
}
__name(getVips, "getVips");
async function getHelpers(bucket) {
  const cached = memCache.get("system_helpers");
  if (cached !== void 0) return cached;
  const data = await getR2Json(bucket, "data/helpers.json");
  const result = data?.helpers || [];
  memCache.set("system_helpers", result, 3e5);
  return result;
}
__name(getHelpers, "getHelpers");
async function getIdeas(bucket) {
  const cached = memCache.get("system_ideas");
  if (cached !== void 0) return cached;
  const data = await getR2Json(bucket, "data/ideas.json");
  const result = data?.ideas || [];
  memCache.set("system_ideas", result, 3e5);
  return result;
}
__name(getIdeas, "getIdeas");
// Maps a role id to its R2 store, list key and mem-cache key. "mod" mirrors to
// the public moderators list and invalidates the moderation caches.
var ROLE_STORE = {
  mod:    { file: "data/moderators.json", key: "moderators", mem: "system_moderators", mirror: "public/api/moderators.json", invalidate: true },
  vip:    { file: "data/vips.json",       key: "vips",       mem: "system_vips" },
  helper: { file: "data/helpers.json",    key: "helpers",    mem: "system_helpers" },
  idea:   { file: "data/ideas.json",      key: "ideas",      mem: "system_ideas" }
};
async function getRoleList(bucket, role) {
  const store = ROLE_STORE[role];
  if (!store) return [];
  const data = await getR2Json(bucket, store.file);
  return data?.[store.key] || [];
}
__name(getRoleList, "getRoleList");
async function writeRoleList(env, role, list) {
  const store = ROLE_STORE[role];
  if (!store) return;
  await putR2Json(env.SYSTEM_BUCKET, store.file, { [store.key]: list });
  memCache.set(store.mem, list, 3e5);
  if (store.mirror) {
    putR2Json(env.SYSTEM_BUCKET, store.mirror, { [store.key]: list }).catch(() => {});
  }
}
__name(writeRoleList, "writeRoleList");
async function getWhitelist(bucket, type = "profilebackground") {
  const data = await getR2Json(bucket, `data/whitelist/${type}.json`);
  return data?.users || [];
}
__name(getWhitelist, "getWhitelist");
async function addToWhitelist(bucket, username, addedBy, type = "profilebackground") {
  const data = await getR2Json(bucket, `data/whitelist/${type}.json`) || { users: [], log: [] };
  const users = data.users || [];
  const log = data.log || [];
  const lower = username.toLowerCase();
  if (!users.includes(lower)) {
    users.push(lower);
    log.push({ action: "add", username: lower, by: addedBy, at: (/* @__PURE__ */ new Date()).toISOString() });
    await putR2Json(bucket, `data/whitelist/${type}.json`, { users, log });
  }
  return users;
}
__name(addToWhitelist, "addToWhitelist");
async function removeFromWhitelist(bucket, username, removedBy, type = "profilebackground") {
  const data = await getR2Json(bucket, `data/whitelist/${type}.json`) || { users: [], log: [] };
  let users = data.users || [];
  const log = data.log || [];
  const lower = username.toLowerCase();
  const prev = users.length;
  users = users.filter((u) => u !== lower);
  if (users.length < prev) {
    log.push({ action: "remove", username: lower, by: removedBy, at: (/* @__PURE__ */ new Date()).toISOString() });
    await putR2Json(bucket, `data/whitelist/${type}.json`, { users, log });
  }
  return users;
}
__name(removeFromWhitelist, "removeFromWhitelist");
async function auditLog(bucket, event, ctx) {
  const ts = Date.now();
  const dateStr = new Date(ts).toISOString().split("T")[0];
  const randomId = Math.random().toString(36).slice(2, 8);
  const key = `data/audit/${event.action}/${dateStr}/${ts}-${randomId}.json`;
  const entry = {
    action: event.action,
    actor: event.actor || "unknown",
    actorAccountID: event.actorAccountID || null,
    target: event.target || null,
    outcome: event.outcome || "success",
    details: event.details || {},
    timestamp: ts,
    date: new Date(ts).toISOString()
  };
  if (ctx) ctx.waitUntil(putR2Json(bucket, key, entry));
  else await putR2Json(bucket, key, entry);
}
__name(auditLog, "auditLog");
async function logAudit(bucket, action, details, ctx) {
  await auditLog(bucket, {
    action,
    actor: details?.actor || details?.username || details?.adminUser || "unknown",
    target: details?.target || details?.levelId || null,
    outcome: "success",
    details
  }, ctx);
}
__name(logAudit, "logAudit");

// ── GDBrowser identity verification (cached) ────────────────────────
// Verifies that `username` actually owns `accountID` by hitting gdbrowser.com.
// Each verification is cached in SYSTEM_BUCKET for GDBROWSER_CACHE_TTL_MS so
// repeated checks within the TTL don't burn external subrequests.
//
// Returns: { ok: boolean, accountID?: number, source: "cache" | "live" | "error" }
//   ok=true means GDBrowser confirmed the identity at some point in the
//   cache window. ok=false means either GDBrowser denied it OR the upstream
//   call failed (we don't auto-fail-open on GDBrowser outages).
var GDBROWSER_CACHE_TTL_MS = 7 * 24 * 60 * 60 * 1e3; // 7 days
var GDBROWSER_NEG_CACHE_TTL_MS = 60 * 60 * 1e3;      // 1 hour for negatives
function isSafeUsername(username) {
  return typeof username === "string" && /^[^/\\\0\r\n]{1,32}$/.test(username.trim());
}
__name(isSafeUsername, "isSafeUsername");
async function verifyGdAccount(env, username, accountID) {
  const u = (username || "").toLowerCase().trim();
  const aid = parseInt(accountID || "0");
  if (!isSafeUsername(u) || aid <= 0) return { ok: false, source: "error" };

  // 1) memCache first — same isolate, near-zero cost
  const memKey = `gdverify_${u}_${aid}`;
  const memCached = memCache.get(memKey);
  if (memCached !== void 0) return { ...memCached, source: "cache" };

  // 2) SYSTEM_BUCKET cache — shared across isolates
  const persistKey = `data/gdverify/${u}_${aid}.json`;
  try {
    const persisted = await getR2Json(env.SYSTEM_BUCKET, persistKey);
    if (persisted && typeof persisted.expiresAt === "number" && Date.now() < persisted.expiresAt) {
      const result = { ok: !!persisted.ok, accountID: persisted.accountID || aid };
      memCache.set(memKey, result, 3 * 60 * 60 * 1e3); // 3h memCache
      return { ...result, source: "cache" };
    }
  } catch { /* ignore — fall through to live check */ }

  // 3) Live call to gdbrowser.com with a 5s timeout so a slow upstream
  //    can't block the whole worker.
  let result = { ok: false, accountID: aid };
  try {
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), 5000);
    try {
      const gdRes = await fetch(
        `https://gdbrowser.com/api/profile/${encodeURIComponent(username)}`,
        { signal: ctrl.signal }
      );
      if (gdRes.ok) {
        const gdData = await gdRes.json();
        if (parseInt(gdData.accountID) === aid) {
          result = { ok: true, accountID: aid };
        }
      }
    } finally {
      clearTimeout(timer);
    }
  } catch (e) {
    // network/abort: persist a negative-cache for the short TTL so we
    // don't hammer gdbrowser, but don't block longer than that.
    console.warn(`[verifyGdAccount] live error for ${u}/${aid}:`, e.message);
  }

  // 4) Persist (positive: long TTL; negative: short TTL).
  const ttl = result.ok ? GDBROWSER_CACHE_TTL_MS : GDBROWSER_NEG_CACHE_TTL_MS;
  const memTtl = result.ok ? 3 * 60 * 60 * 1e3 : 5 * 60 * 1e3;
  memCache.set(memKey, result, memTtl);
  // Best-effort persist; failure is non-fatal.
  putR2Json(env.SYSTEM_BUCKET, persistKey, {
    ok: result.ok,
    accountID: aid,
    cachedAt: Date.now(),
    expiresAt: Date.now() + ttl
  }).catch(() => {});

  return { ...result, source: "live" };
}
__name(verifyGdAccount, "verifyGdAccount");

// src/services/permissions.js
var DEFAULT_ADMIN_USERS = ["flozwer", "alvaroeter"];
var ADMIN_USERS = [...DEFAULT_ADMIN_USERS];
async function loadAdminUsers(sysBucket) {
  const cacheKey = "admin_users_list";
  const cached = memCache.get(cacheKey);
  if (cached) {
    ADMIN_USERS = cached;
    return;
  }
  try {
    const stored = await getR2Json(sysBucket, "data/system/admins.json");
    if (Array.isArray(stored) && stored.length > 0) {
      ADMIN_USERS = stored.map((u) => u.toLowerCase().trim());
    } else {
      ADMIN_USERS = [...DEFAULT_ADMIN_USERS];
    }
  } catch {
    ADMIN_USERS = [...DEFAULT_ADMIN_USERS];
  }
  memCache.set(cacheKey, ADMIN_USERS, 5 * 6e4);
}
__name(loadAdminUsers, "loadAdminUsers");
var Roles = {
  ADMIN: "admin",
  MODERATOR: "moderator",
  VIP: "vip",
  WHITELISTED: "whitelisted",
  // per-action whitelist (e.g. profilebackground)
  AUTHENTICATED: "authenticated",
  // has valid mod-code
  ANONYMOUS: "anonymous"
};
var Actions = {
  // Thumbnails
  UPLOAD_THUMBNAIL: "upload-thumbnail",
  UPLOAD_GIF: "upload-gif",
  UPLOAD_VIDEO: "upload-video",
  DELETE_THUMBNAIL: "delete-thumbnail",
  // Profiles
  UPLOAD_PROFILE_BG: "upload-profile-bg",
  UPLOAD_PROFILE_IMG: "upload-profile-img",
  // Music
  UPLOAD_MUSIC: "upload-music",
  // Queue
  VIEW_QUEUE: "view-queue",
  ACCEPT_QUEUE: "accept-queue",
  REJECT_QUEUE: "reject-queue",
  CLAIM_QUEUE: "claim-queue",
  // Admin
  MANAGE_MODERATORS: "manage-moderators",
  MANAGE_BANS: "manage-bans",
  MANAGE_VIPS: "manage-vips",
  MANAGE_WHITELIST: "manage-whitelist",
  VIEW_ADMIN: "view-admin",
  SET_FEATURED: "set-featured",
  // Misc
  UPLOAD_PET: "upload-pet",
  BYPASS_QUEUE: "bypass-queue",
  RUN_MIGRATION: "run-migration"
};
var POLICY = {
  // Admin-only
  [Actions.MANAGE_MODERATORS]: Roles.ADMIN,
  [Actions.MANAGE_BANS]: Roles.ADMIN,
  [Actions.MANAGE_VIPS]: Roles.ADMIN,
  [Actions.MANAGE_WHITELIST]: Roles.ADMIN,
  [Actions.VIEW_ADMIN]: Roles.ADMIN,
  [Actions.RUN_MIGRATION]: Roles.ADMIN,
  // Moderator+
  [Actions.VIEW_QUEUE]: Roles.MODERATOR,
  [Actions.ACCEPT_QUEUE]: Roles.MODERATOR,
  [Actions.REJECT_QUEUE]: Roles.MODERATOR,
  [Actions.CLAIM_QUEUE]: Roles.MODERATOR,
  [Actions.UPLOAD_THUMBNAIL]: Roles.MODERATOR,
  [Actions.UPLOAD_GIF]: Roles.MODERATOR,
  [Actions.UPLOAD_VIDEO]: Roles.MODERATOR,
  [Actions.DELETE_THUMBNAIL]: Roles.MODERATOR,
  [Actions.UPLOAD_PET]: Roles.MODERATOR,
  [Actions.SET_FEATURED]: Roles.MODERATOR,
  // Authenticated (valid mod-code holder)
  [Actions.UPLOAD_PROFILE_BG]: Roles.AUTHENTICATED,
  [Actions.UPLOAD_PROFILE_IMG]: Roles.AUTHENTICATED,
  [Actions.UPLOAD_MUSIC]: Roles.AUTHENTICATED
};
var ROLE_HIERARCHY = [
  Roles.ANONYMOUS,
  Roles.AUTHENTICATED,
  Roles.WHITELISTED,
  Roles.VIP,
  Roles.MODERATOR,
  Roles.ADMIN
];
async function getUserRole(username, sysBucket) {
  const user = (username || "").toLowerCase().trim();
  if (!user) return Roles.ANONYMOUS;
  if (ADMIN_USERS.includes(user)) return Roles.ADMIN;
  const [moderators, vips] = await Promise.all([
    getModerators(sysBucket),
    getVips(sysBucket)
  ]);
  if (moderators.includes(user)) return Roles.MODERATOR;
  if (vips.includes(user)) return Roles.VIP;
  return Roles.AUTHENTICATED;
}
__name(getUserRole, "getUserRole");

// src/middleware/auth.js
async function timingSafeEqual(a, b) {
  const enc = new TextEncoder();
  const aBuf = enc.encode(a);
  const bBuf = enc.encode(b);
  if (aBuf.byteLength !== bBuf.byteLength) {
    const maxLen = Math.max(aBuf.byteLength, bBuf.byteLength);
    const paddedA = new Uint8Array(maxLen);
    const paddedB = new Uint8Array(maxLen);
    paddedA.set(aBuf);
    paddedB.set(bBuf);
    const key = await crypto.subtle.importKey("raw", paddedA, { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
    await crypto.subtle.sign("HMAC", key, paddedB);
    return false;
  }
  return crypto.subtle.timingSafeEqual(aBuf, bBuf);
}
__name(timingSafeEqual, "timingSafeEqual");
async function verifyApiKey(request, env) {
  const apiKey = request.headers.get("X-API-Key");
  if (!apiKey || typeof env?.API_KEY !== "string" || !env.API_KEY) return false;
  return timingSafeEqual(apiKey, env.API_KEY);
}
__name(verifyApiKey, "verifyApiKey");
async function verifyModAuth(request, env, username, accountID) {
  const sysBucket = env.SYSTEM_BUCKET;
  if (!isSafeUsername(username)) return { authorized: false, invalidIdentity: true };
  username = username.trim();
  const rawModCode = request.headers.get("X-Mod-Code");
  const authKey = `data/auth/${username.toLowerCase()}.json`;
  if (rawModCode) {
    const modCode = rawModCode.trim();
    const storedData = await getR2Json(sysBucket, authKey);
    if (!storedData) {
      console.log(`[Auth] No stored auth data found for ${username} at ${authKey} (codeLen=${modCode.length})`);
      return { authorized: false, invalidCode: true };
    }
    const storedCode = (storedData.code || "").trim();
    const codeMatch = await timingSafeEqual(storedCode, modCode);
    if (!codeMatch) {
      console.log(`[Auth] Code mismatch for ${username} (sentLen=${modCode.length}, storedLen=${storedCode.length})`);
      return { authorized: false, invalidCode: true };
    }
    if (accountID > 0 && storedData.accountID && parseInt(storedData.accountID) !== parseInt(accountID)) {
      console.log(`[Auth] AccountID mismatch for ${username}: expected ${storedData.accountID}, got ${accountID}`);
      return { authorized: false };
    }
    if (storedData.expiresAt && Date.now() > storedData.expiresAt) {
      console.log(`[Auth] Mod code expired for ${username}. Expired at ${new Date(storedData.expiresAt).toISOString()}`);
      return { authorized: false, expired: true };
    }
    console.log(`[Auth] Authorized ${username} with mod-code (codeLen=${modCode.length})`);
    return { authorized: true };
  }
  // Membership and account IDs are public information, so neither proves that
  // the caller controls a moderator account. Privileged requests must always
  // present the previously provisioned per-moderator secret.
  console.log(`[Auth] No X-Mod-Code header for ${username}.`);
  return { authorized: false, needsModCode: true };
}
__name(verifyModAuth, "verifyModAuth");
async function isModeratorOrAdmin(env, username) {
  const user = (username || "").toLowerCase().trim();
  if (!user) return false;
  if (ADMIN_USERS.includes(user)) return true;
  const moderators = await getModerators(env.SYSTEM_BUCKET);
  return moderators.includes(user);
}
__name(isModeratorOrAdmin, "isModeratorOrAdmin");
async function verifyModAuthFromBody(request, env, body) {
  const username = (body.adminUser || body.moderator || body.actor || body.username || "").toString().trim();
  const accountID = parseInt(body.accountID || "0");
  if (!username) return { authorized: false };
  return await verifyModAuth(request, env, username, accountID);
}
__name(verifyModAuthFromBody, "verifyModAuthFromBody");
async function requireAdmin(request, env, body) {
  const auth = await verifyModAuthFromBody(request, env, body);
  if (!auth.authorized) {
    if (auth.needsModCode) return { authorized: false, error: "Provisioned moderator code required." };
    if (auth.invalidCode) return { authorized: false, error: "Invalid or expired moderator code." };
    return { authorized: false, error: "Auth verification failed" };
  }
  const username = (body.username || body.adminUser || "").toString().trim().toLowerCase();
  if (!ADMIN_USERS.includes(username)) return { authorized: false, error: "Admin privileges required" };
  return { authorized: true, newCode: auth.newCode };
}
__name(requireAdmin, "requireAdmin");
async function verifyAdminFromRequest(request, env) {
  const url2 = new URL(request.url);
  const username = (request.headers.get("X-Admin-User") || url2.searchParams.get("username") || "").toLowerCase();
  if (!username || !ADMIN_USERS.includes(username)) return false;
  const accountID = parseInt(url2.searchParams.get("accountID") || "0");
  const auth = await verifyModAuth(request, env, username, accountID);
  return auth.authorized;
}
__name(verifyAdminFromRequest, "verifyAdminFromRequest");
function parsePositiveAccountID(value) {
  const raw = String(value ?? "").trim();
  if (!/^\d{1,15}$/.test(raw)) return 0;
  const parsed = Number(raw);
  return Number.isSafeInteger(parsed) && parsed > 0 ? parsed : 0;
}
__name(parsePositiveAccountID, "parsePositiveAccountID");
async function authorizeAccountWrite(request, env, usernameValue, accountIDValue) {
  const username = String(usernameValue || "").trim();
  const accountID = parsePositiveAccountID(accountIDValue);
  if (!isSafeUsername(username) || accountID <= 0) {
    return { authorized: false, response: forbiddenResponse("Valid username and accountID required") };
  }
  const auth = await verifyModAuth(request, env, username, accountID);
  if (!auth.authorized) return { authorized: false, response: modAuthForbiddenResponse(auth) };
  const identity = await verifyAccountForWrite(env, accountID, username);
  if (!identity.valid) {
    return {
      authorized: false,
      response: new Response(JSON.stringify({ error: "Account verification failed", code: identity.reason }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      })
    };
  }
  return { authorized: true, username: username.toLowerCase(), accountID };
}
__name(authorizeAccountWrite, "authorizeAccountWrite");
function forbiddenResponse(message) {
  return new Response(JSON.stringify({ error: message || "Forbidden" }), {
    status: 403,
    headers: { "Content-Type": "application/json", ...corsHeaders() }
  });
}
__name(forbiddenResponse, "forbiddenResponse");
function modAuthForbiddenResponse(auth) {
  if (auth.needsModCode) {
    return new Response(JSON.stringify({
      error: "Mod code required",
      needsModCode: true,
      message: "A provisioned moderator code is required"
    }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  if (auth.invalidCode) {
    return new Response(JSON.stringify({
      error: "Invalid or expired mod code",
      invalidCode: true,
      message: "Ask an administrator to provision or rotate your moderator code."
    }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  return forbiddenResponse("Moderator auth required");
}
__name(modAuthForbiddenResponse, "modAuthForbiddenResponse");

// src/image-security.js
var MAGIC_SIGNATURES = {
  png: { bytes: [137, 80, 78, 71, 13, 10, 26, 10], offset: 0 },
  jpeg: { bytes: [255, 216, 255], offset: 0 },
  gif87a: { bytes: [71, 73, 70, 56, 55, 97], offset: 0 },
  // GIF87a
  gif89a: { bytes: [71, 73, 70, 56, 57, 97], offset: 0 },
  // GIF89a
  webp: { bytes: [82, 73, 70, 70], offset: 0, secondary: { bytes: [87, 69, 66, 80], offset: 8 } },
  // MP4: ISO base media file format — ftyp box at offset 4
  mp4ftyp: { bytes: [102, 116, 121, 112], offset: 4 }
  // "ftyp" at byte 4
};
var DANGEROUS_BINARY_SIGNATURES = [
  { name: "ELF executable", bytes: [127, 69, 76, 70] },
  { name: "PE/MZ executable", bytes: [77, 90, 144, 0] },
  // Full MZ+DOS stub, not just 0x4D 0x5A
  { name: "Java class", bytes: [202, 254, 186, 190] },
  { name: "ZIP archive", bytes: [80, 75, 3, 4] },
  { name: "RAR archive", bytes: [82, 97, 114, 33, 26, 7] },
  // Full RAR signature
  { name: "7z archive", bytes: [55, 122, 188, 175, 39, 28] },
  { name: "PDF document", bytes: [37, 80, 68, 70, 45] },
  // %PDF-
  { name: "WebAssembly", bytes: [0, 97, 115, 109] }
];
var DANGEROUS_TEXT_PATTERNS = [
  // Web scripting - high confidence patterns
  "<script",
  "<\/script>",
  "javascript:",
  "vbscript:",
  // PHP - high confidence
  "<?php",
  "<?=",
  // Server-side includes
  "<!--#exec",
  "<!--#include",
  // Shell shebangs
  "#!/bin/",
  "#!/usr/",
  // HTML injection - high confidence
  "<iframe",
  "<object data=",
  "<embed src=",
  "<applet",
  "<svg onload",
  "<svg/onload",
  "<body onload",
  "<meta http-equiv",
  // Data URI with executable content
  "data:text/html",
  "data:text/javascript",
  "data:application/x-javascript"
];
function detectRealFormat(data) {
  if (data.length < 12) return null;
  if (matchBytes(data, MAGIC_SIGNATURES.png.bytes, 0)) return "png";
  if (matchBytes(data, MAGIC_SIGNATURES.jpeg.bytes, 0)) return "jpeg";
  if (matchBytes(data, MAGIC_SIGNATURES.gif87a.bytes, 0) || matchBytes(data, MAGIC_SIGNATURES.gif89a.bytes, 0)) return "gif";
  if (matchBytes(data, MAGIC_SIGNATURES.webp.bytes, 0) && matchBytes(data, MAGIC_SIGNATURES.webp.secondary.bytes, 8)) return "webp";
  if (data.length >= 8 && matchBytes(data, MAGIC_SIGNATURES.mp4ftyp.bytes, 4)) return "mp4";
  return null;
}
__name(detectRealFormat, "detectRealFormat");
function matchBytes(data, expected, offset) {
  if (data.length < offset + expected.length) return false;
  for (let i = 0; i < expected.length; i++) {
    if (data[offset + i] !== expected[i]) return false;
  }
  return true;
}
__name(matchBytes, "matchBytes");
function scanTailForBinaries(data, tailOffset) {
  if (tailOffset >= data.length) return null;
  const tail = data.slice(tailOffset);
  for (const sig of DANGEROUS_BINARY_SIGNATURES) {
    for (let i = 0; i <= tail.length - sig.bytes.length; i++) {
      if (matchBytes(tail, sig.bytes, i)) {
        return sig.name;
      }
    }
  }
  return null;
}
__name(scanTailForBinaries, "scanTailForBinaries");
function extractPNGMetadata(data) {
  const metaChunks = [];
  let offset = 8;
  while (offset + 12 <= data.length) {
    const view = new DataView(data.buffer, data.byteOffset + offset, Math.min(8, data.length - offset));
    const chunkLength = view.getUint32(0, false);
    const chunkType = String.fromCharCode(data[offset + 4], data[offset + 5], data[offset + 6], data[offset + 7]);
    if (chunkLength > data.length - offset - 12) break;
    const metadataTypes = ["tEXt", "iTXt", "zTXt", "eXIf", "iCCP"];
    if (metadataTypes.includes(chunkType)) {
      const chunkData = data.slice(offset + 8, offset + 8 + chunkLength);
      metaChunks.push(chunkData);
    }
    if (chunkType === "IEND") break;
    offset += 12 + chunkLength;
  }
  return metaChunks;
}
__name(extractPNGMetadata, "extractPNGMetadata");
function extractJPEGMetadata(data) {
  const metaSegments = [];
  let offset = 2;
  while (offset < data.length - 3) {
    if (data[offset] !== 255) break;
    const marker = data[offset + 1];
    if (marker === 255) {
      offset++;
      continue;
    }
    if (marker === 216 || marker === 217 || marker >= 208 && marker <= 215 || marker === 1) {
      offset += 2;
      continue;
    }
    if (marker === 218) break;
    const segLength = data[offset + 2] << 8 | data[offset + 3];
    if (segLength < 2 || offset + 2 + segLength > data.length) break;
    if (marker >= 224 && marker <= 239 || marker === 254) {
      metaSegments.push(data.slice(offset + 4, offset + 2 + segLength));
    }
    offset += 2 + segLength;
  }
  return metaSegments;
}
__name(extractJPEGMetadata, "extractJPEGMetadata");
function extractGIFMetadata(data) {
  const metaBlocks = [];
  const packed = data[10];
  const hasGCT = (packed & 128) !== 0;
  const gctSize = hasGCT ? 3 * (1 << (packed & 7) + 1) : 0;
  let offset = 13 + gctSize;
  while (offset < data.length) {
    const introducer = data[offset];
    if (introducer === 59) break;
    if (introducer === 33 && offset + 2 < data.length) {
      const label = data[offset + 1];
      offset += 2;
      if (label === 254 || label === 255) {
        const blockParts = [];
        while (offset < data.length) {
          const blockSize = data[offset];
          offset++;
          if (blockSize === 0) break;
          if (offset + blockSize <= data.length) {
            blockParts.push(data.slice(offset, offset + blockSize));
          }
          offset += blockSize;
        }
        if (blockParts.length > 0) {
          const total = blockParts.reduce((s, b) => s + b.length, 0);
          const merged = new Uint8Array(total);
          let pos = 0;
          for (const part of blockParts) {
            merged.set(part, pos);
            pos += part.length;
          }
          metaBlocks.push(merged);
        }
      } else {
        while (offset < data.length) {
          const blockSize = data[offset];
          offset++;
          if (blockSize === 0) break;
          offset += blockSize;
        }
      }
    } else if (introducer === 44) {
      if (offset + 10 > data.length) break;
      const imgPacked = data[offset + 9];
      const hasLCT = (imgPacked & 128) !== 0;
      const lctSize = hasLCT ? 3 * (1 << (imgPacked & 7) + 1) : 0;
      offset += 10 + lctSize;
      if (offset >= data.length) break;
      offset++;
      while (offset < data.length) {
        const blockSize = data[offset];
        offset++;
        if (blockSize === 0) break;
        offset += blockSize;
      }
    } else {
      break;
    }
  }
  return metaBlocks;
}
__name(extractGIFMetadata, "extractGIFMetadata");
function extractWebPMetadata(data) {
  const metaChunks = [];
  if (data.length < 20) return metaChunks;
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const riffSize = view.getUint32(4, true);
  const expectedEnd = Math.min(riffSize + 8, data.length);
  let offset = 12;
  while (offset + 8 <= expectedEnd) {
    const chunkId = String.fromCharCode(data[offset], data[offset + 1], data[offset + 2], data[offset + 3]);
    const chunkSize = view.getUint32(offset + 4, true);
    if (offset + 8 + chunkSize > data.length) break;
    const metaTypes = ["EXIF", "XMP ", "ICCP"];
    if (metaTypes.includes(chunkId)) {
      metaChunks.push(data.slice(offset + 8, offset + 8 + chunkSize));
    }
    offset += 8 + chunkSize + chunkSize % 2;
  }
  return metaChunks;
}
__name(extractWebPMetadata, "extractWebPMetadata");
function scanMetadataForDangerousText(metadataRegions) {
  for (const region of metadataRegions) {
    let text;
    try {
      text = new TextDecoder("utf-8", { fatal: false }).decode(region).toLowerCase();
    } catch {
      continue;
    }
    for (const pattern of DANGEROUS_TEXT_PATTERNS) {
      if (text.includes(pattern.toLowerCase())) {
        return { found: true, pattern };
      }
    }
  }
  return null;
}
__name(scanMetadataForDangerousText, "scanMetadataForDangerousText");
function scanTailForDangerousText(data, tailOffset) {
  if (tailOffset >= data.length) return null;
  const tail = data.slice(tailOffset);
  return scanMetadataForDangerousText([tail]);
}
__name(scanTailForDangerousText, "scanTailForDangerousText");
function validatePNGStructure(data) {
  if (data.length < 33) return { valid: false, error: "PNG file too small" };
  if (!matchBytes(data, MAGIC_SIGNATURES.png.bytes, 0)) {
    return { valid: false, error: "Invalid PNG signature" };
  }
  let offset = 8;
  let foundIHDR = false;
  let foundIEND = false;
  let endOffset = data.length;
  let chunkCount = 0;
  const MAX_CHUNKS = 1e4;
  let totalMetadataSize = 0;
  const MAX_METADATA_SIZE = 1024 * 1024;
  while (offset + 8 <= data.length && chunkCount < MAX_CHUNKS) {
    const view = new DataView(data.buffer, data.byteOffset + offset, Math.min(8, data.length - offset));
    const chunkLength = view.getUint32(0, false);
    const chunkType = String.fromCharCode(data[offset + 4], data[offset + 5], data[offset + 6], data[offset + 7]);
    if (chunkLength > data.length - offset) {
      return { valid: false, error: `PNG chunk '${chunkType}' has invalid length: ${chunkLength}` };
    }
    if (chunkCount === 0 && chunkType !== "IHDR") {
      return { valid: false, error: "First PNG chunk must be IHDR" };
    }
    if (chunkType === "IHDR") {
      foundIHDR = true;
      if (chunkLength !== 13) {
        return { valid: false, error: "Invalid IHDR chunk length" };
      }
    }
    if (chunkType === "IEND") {
      foundIEND = true;
      endOffset = offset + 12 + chunkLength;
      const afterIEND = data.length - endOffset;
      if (afterIEND > 16) {
        return { valid: false, error: `Suspicious data after IEND chunk (${afterIEND} bytes appended)`, endOffset };
      }
      break;
    }
    const metadataChunks = ["tEXt", "iTXt", "zTXt", "eXIf", "iCCP", "sPLT", "hIST"];
    if (metadataChunks.includes(chunkType)) {
      totalMetadataSize += chunkLength;
      if (totalMetadataSize > MAX_METADATA_SIZE) {
        return { valid: false, error: "Excessive metadata in PNG (possible payload hiding)" };
      }
    }
    offset += 12 + chunkLength;
    chunkCount++;
  }
  if (!foundIHDR) return { valid: false, error: "Missing IHDR chunk" };
  if (!foundIEND) return { valid: false, error: "Missing IEND chunk (truncated or malformed PNG)" };
  if (chunkCount >= MAX_CHUNKS) return { valid: false, error: "Excessive number of PNG chunks (possible DoS)" };
  return { valid: true, endOffset };
}
__name(validatePNGStructure, "validatePNGStructure");
function validateJPEGStructure(data) {
  if (data.length < 20) return { valid: false, error: "JPEG file too small" };
  if (data[0] !== 255 || data[1] !== 216) {
    return { valid: false, error: "Invalid JPEG SOI marker" };
  }
  let offset = 2;
  let segmentCount = 0;
  const MAX_SEGMENTS = 5e3;
  let totalCommentSize = 0;
  let totalAPPSize = 0;
  const MAX_COMMENT_SIZE = 512 * 1024;
  const MAX_APP_SIZE = 2 * 1024 * 1024;
  let endOffset = data.length;
  while (offset < data.length - 1 && segmentCount < MAX_SEGMENTS) {
    if (data[offset] !== 255) break;
    const marker = data[offset + 1];
    if (marker === 255) {
      offset++;
      continue;
    }
    if (marker === 216 || marker >= 208 && marker <= 215 || marker === 1) {
      offset += 2;
      continue;
    }
    if (marker === 217) {
      endOffset = offset + 2;
      const remaining = data.length - endOffset;
      if (remaining > 16) {
        return { valid: false, error: `Suspicious data after JPEG EOI (${remaining} bytes appended)`, endOffset };
      }
      break;
    }
    if (marker === 218) break;
    if (offset + 4 > data.length) break;
    const segLength = data[offset + 2] << 8 | data[offset + 3];
    if (segLength < 2) return { valid: false, error: "Invalid JPEG segment length" };
    if (marker === 254) {
      totalCommentSize += segLength;
      if (totalCommentSize > MAX_COMMENT_SIZE) {
        return { valid: false, error: "Excessive JPEG comment data (possible payload hiding)" };
      }
    }
    if (marker >= 224 && marker <= 239) {
      totalAPPSize += segLength;
      if (totalAPPSize > MAX_APP_SIZE) {
        return { valid: false, error: "Excessive JPEG APP metadata (possible payload hiding)" };
      }
    }
    offset += 2 + segLength;
    segmentCount++;
  }
  if (segmentCount >= MAX_SEGMENTS) {
    return { valid: false, error: "Excessive number of JPEG segments (possible DoS)" };
  }
  return { valid: true, endOffset };
}
__name(validateJPEGStructure, "validateJPEGStructure");
function validateGIFStructure(data) {
  if (data.length < 13) return { valid: false, error: "GIF file too small" };
  const header = String.fromCharCode(...data.slice(0, 6));
  if (header !== "GIF87a" && header !== "GIF89a") {
    return { valid: false, error: "Invalid GIF header" };
  }
  const width = data[6] | data[7] << 8;
  const height = data[8] | data[9] << 8;
  const packed = data[10];
  const hasGCT = (packed & 128) !== 0;
  const gctSize = hasGCT ? 3 * (1 << (packed & 7) + 1) : 0;
  if (width === 0 || height === 0) {
    return { valid: false, error: "GIF has zero dimensions" };
  }
  if (width > 16384 || height > 16384) {
    return { valid: false, error: `GIF dimensions too large (${width}x${height})` };
  }
  let offset = 13 + gctSize;
  let blockCount = 0;
  const MAX_BLOCKS = 5e4;
  let totalCommentSize = 0;
  let totalAppExtSize = 0;
  const MAX_COMMENT_SIZE = 256 * 1024;
  const MAX_APP_EXT_SIZE = 512 * 1024;
  let endOffset = data.length;
  while (offset < data.length && blockCount < MAX_BLOCKS) {
    const introducer = data[offset];
    if (introducer === 59) {
      endOffset = offset + 1;
      const remaining = data.length - endOffset;
      if (remaining > 16) {
        return { valid: false, error: `Suspicious data after GIF trailer (${remaining} bytes appended)`, endOffset };
      }
      break;
    }
    if (introducer === 44) {
      if (offset + 10 > data.length) {
        return { valid: false, error: "Truncated GIF image descriptor" };
      }
      const imgPacked = data[offset + 9];
      const hasLCT = (imgPacked & 128) !== 0;
      const lctSize = hasLCT ? 3 * (1 << (imgPacked & 7) + 1) : 0;
      offset += 10 + lctSize;
      if (offset >= data.length) break;
      offset++;
      while (offset < data.length) {
        const blockSize = data[offset];
        offset++;
        if (blockSize === 0) break;
        offset += blockSize;
      }
    } else if (introducer === 33) {
      if (offset + 2 > data.length) break;
      const label = data[offset + 1];
      offset += 2;
      if (label === 254) {
        while (offset < data.length) {
          const blockSize = data[offset];
          offset++;
          if (blockSize === 0) break;
          totalCommentSize += blockSize;
          offset += blockSize;
        }
        if (totalCommentSize > MAX_COMMENT_SIZE) {
          return { valid: false, error: "Excessive GIF comment data (possible payload hiding)" };
        }
      } else if (label === 255) {
        while (offset < data.length) {
          const blockSize = data[offset];
          offset++;
          if (blockSize === 0) break;
          totalAppExtSize += blockSize;
          offset += blockSize;
        }
        if (totalAppExtSize > MAX_APP_EXT_SIZE) {
          return { valid: false, error: "Excessive GIF application extension data (possible payload hiding)" };
        }
      } else {
        while (offset < data.length) {
          const blockSize = data[offset];
          offset++;
          if (blockSize === 0) break;
          offset += blockSize;
        }
      }
    } else {
      break;
    }
    blockCount++;
  }
  if (blockCount >= MAX_BLOCKS) {
    return { valid: false, error: "Excessive number of GIF blocks (possible DoS/decompression bomb)" };
  }
  return { valid: true, endOffset };
}
__name(validateGIFStructure, "validateGIFStructure");
function validateWebPStructure(data) {
  if (data.length < 20) return { valid: false, error: "WebP file too small" };
  if (!matchBytes(data, [82, 73, 70, 70], 0)) {
    return { valid: false, error: "Invalid RIFF header" };
  }
  if (!matchBytes(data, [87, 69, 66, 80], 8)) {
    return { valid: false, error: "Invalid WebP identifier" };
  }
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const riffSize = view.getUint32(4, true);
  const expectedSize = riffSize + 8;
  const endOffset = expectedSize;
  if (data.length > expectedSize + 16) {
    return { valid: false, error: `Data appended after RIFF container (${data.length - expectedSize} extra bytes)`, endOffset };
  }
  let offset = 12;
  let chunkCount = 0;
  const MAX_CHUNKS = 1e3;
  let totalMetadataSize = 0;
  const MAX_METADATA = 2 * 1024 * 1024;
  while (offset + 8 <= data.length && offset < expectedSize && chunkCount < MAX_CHUNKS) {
    const chunkId = String.fromCharCode(data[offset], data[offset + 1], data[offset + 2], data[offset + 3]);
    const chunkSize = view.getUint32(offset + 4, true);
    const metaChunks = ["EXIF", "XMP ", "ICCP"];
    if (metaChunks.includes(chunkId)) {
      totalMetadataSize += chunkSize;
      if (totalMetadataSize > MAX_METADATA) {
        return { valid: false, error: "Excessive WebP metadata (possible payload hiding)" };
      }
    }
    offset += 8 + chunkSize + chunkSize % 2;
    chunkCount++;
  }
  if (chunkCount >= MAX_CHUNKS) {
    return { valid: false, error: "Excessive number of WebP chunks" };
  }
  return { valid: true, endOffset };
}
__name(validateWebPStructure, "validateWebPStructure");
function validateMP4Structure(data) {
  if (data.length < 8) return { valid: false, error: "MP4 file too small" };
  if (!matchBytes(data, [102, 116, 121, 112], 4)) {
    return { valid: false, error: "MP4 missing ftyp box \u2014 not a valid ISO BMFF file" };
  }
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const ALLOWED_BOXES = /* @__PURE__ */ new Set([
    "ftyp",
    "moov",
    "mdat",
    "free",
    "skip",
    "wide",
    "pdin",
    "moof",
    "mfra",
    "sidx",
    "styp",
    "ssix",
    "prft",
    "uuid",
    "meta"
  ]);
  let offset = 0;
  let boxCount = 0;
  const MAX_BOXES = 200;
  let foundMoov = false;
  while (offset + 8 <= data.length && boxCount < MAX_BOXES) {
    let boxSize = view.getUint32(offset, false);
    const boxType = String.fromCharCode(
      data[offset + 4],
      data[offset + 5],
      data[offset + 6],
      data[offset + 7]
    );
    if (boxSize === 1 && offset + 16 <= data.length) {
      const highBits = view.getUint32(offset + 8, false);
      if (highBits > 0) {
        return { valid: true, endOffset: data.length };
      }
      boxSize = view.getUint32(offset + 12, false);
    }
    if (boxSize === 0) {
      if (boxType === "mdat") {
        return { valid: true, endOffset: data.length };
      }
      break;
    }
    if (boxSize < 8 || offset + boxSize > data.length + 16) {
      if (boxCount > 0 && offset + 8 < data.length) {
        return { valid: true, endOffset: data.length };
      }
      return { valid: false, error: `Invalid MP4 box size ${boxSize} for '${boxType}'` };
    }
    if (!ALLOWED_BOXES.has(boxType)) {
    }
    if (boxType === "moov") foundMoov = true;
    offset += boxSize;
    boxCount++;
  }
  if (boxCount >= MAX_BOXES) {
    return { valid: false, error: "Excessive number of MP4 top-level boxes (possible DoS)" };
  }
  if (!foundMoov && data.length > 1024) {
    return { valid: false, error: "MP4 missing moov box (no movie metadata)" };
  }
  return { valid: true, endOffset: data.length };
}
__name(validateMP4Structure, "validateMP4Structure");
function validateMP4Codec(data) {
  if (data.length < 8) return { valid: false, error: "MP4 too small for codec check" };
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  function readBoxAt(offset, end) {
    if (offset + 8 > end) return null;
    let size = view.getUint32(offset, false);
    const type = String.fromCharCode(
      data[offset + 4],
      data[offset + 5],
      data[offset + 6],
      data[offset + 7]
    );
    let headerSize = 8;
    if (size === 1 && offset + 16 <= end) {
      const hi = view.getUint32(offset + 8, false);
      const lo = view.getUint32(offset + 12, false);
      size = hi > 0 ? end - offset : lo;
      headerSize = 16;
    } else if (size === 0) {
      size = end - offset;
    }
    if (size < headerSize || offset + size > end) return null;
    return { type, size, headerSize, offset };
  }
  __name(readBoxAt, "readBoxAt");
  function findBox(start, end, targetType) {
    let pos2 = start;
    while (pos2 < end) {
      const box = readBoxAt(pos2, end);
      if (!box) return null;
      if (box.type === targetType) return box;
      pos2 += box.size;
    }
    return null;
  }
  __name(findBox, "findBox");
  const moov = findBox(0, data.length, "moov");
  if (!moov) return { valid: false, error: "No moov box for codec validation" };
  const moovPayload = moov.offset + moov.headerSize;
  const moovEnd = moov.offset + moov.size;
  let videoCodec = null;
  let audioCodec = null;
  let pos = moovPayload;
  while (pos < moovEnd) {
    const box = readBoxAt(pos, moovEnd);
    if (!box) break;
    pos += box.size;
    if (box.type !== "trak") continue;
    const trakPayload = box.offset + box.headerSize;
    const trakEnd = box.offset + box.size;
    const mdia = findBox(trakPayload, trakEnd, "mdia");
    if (!mdia) continue;
    const mdiaPayload = mdia.offset + mdia.headerSize;
    const mdiaEnd = mdia.offset + mdia.size;
    const hdlr = findBox(mdiaPayload, mdiaEnd, "hdlr");
    if (!hdlr || hdlr.size < hdlr.headerSize + 12) continue;
    const handlerOffset = hdlr.offset + hdlr.headerSize + 8;
    if (handlerOffset + 4 > data.length) continue;
    const handlerType = String.fromCharCode(
      data[handlerOffset],
      data[handlerOffset + 1],
      data[handlerOffset + 2],
      data[handlerOffset + 3]
    );
    const isVideo = handlerType === "vide";
    const isAudio = handlerType === "soun";
    if (!isVideo && !isAudio) continue;
    const minf = findBox(mdiaPayload, mdiaEnd, "minf");
    if (!minf) continue;
    const stbl = findBox(minf.offset + minf.headerSize, minf.offset + minf.size, "stbl");
    if (!stbl) continue;
    const stsd = findBox(stbl.offset + stbl.headerSize, stbl.offset + stbl.size, "stsd");
    if (!stsd) continue;
    const stsdData = stsd.offset + stsd.headerSize;
    if (stsdData + 16 > data.length) continue;
    const entryBox = readBoxAt(stsdData + 8, stsd.offset + stsd.size);
    if (!entryBox) continue;
    if (isVideo) videoCodec = entryBox.type;
    else if (isAudio) audioCodec = entryBox.type;
  }
  if (!videoCodec) {
    return { valid: false, error: "No video track found in MP4" };
  }
  const allowedVideoCodecs = ["avc1", "avc3"];
  if (!allowedVideoCodecs.includes(videoCodec)) {
    return {
      valid: false,
      error: `Video codec '${videoCodec}' not allowed. Only H.264 (avc1) is accepted. Use the latest Paimbnails client to auto-convert.`,
      videoCodec,
      audioCodec
    };
  }
  if (audioCodec && audioCodec !== "mp4a") {
    return {
      valid: false,
      error: `Audio codec '${audioCodec}' not allowed. Only AAC (mp4a) is accepted. Use the latest Paimbnails client to auto-convert.`,
      videoCodec,
      audioCodec
    };
  }
  return { valid: true, videoCodec, audioCodec: audioCodec || null };
}
__name(validateMP4Codec, "validateMP4Codec");
function hasNullByteInjection(filename) {
  return filename.includes("\0") || filename.includes("%00");
}
__name(hasNullByteInjection, "hasNullByteInjection");
function hasPathTraversal(input) {
  const patterns = ["../", "..\\", "%2e%2e", "%252e"];
  const lower = input.toLowerCase();
  return patterns.some((p) => lower.includes(p));
}
__name(hasPathTraversal, "hasPathTraversal");
function hasDangerousExtension(filename) {
  const dangerous = [
    ".php",
    ".php3",
    ".php4",
    ".php5",
    ".phtml",
    ".pht",
    ".jsp",
    ".jspx",
    ".asp",
    ".aspx",
    ".ashx",
    ".exe",
    ".dll",
    ".bat",
    ".cmd",
    ".com",
    ".scr",
    ".ps1",
    ".sh",
    ".cgi",
    ".pl",
    ".py",
    ".rb",
    ".html",
    ".htm",
    ".xhtml",
    ".shtml",
    ".svg",
    ".swf",
    ".jar",
    ".war",
    ".htaccess",
    ".htpasswd"
  ];
  const lower = filename.toLowerCase();
  const parts = lower.split(".");
  if (parts.length > 2) {
    for (let i = 1; i < parts.length - 1; i++) {
      if (dangerous.some((ext) => ext === "." + parts[i])) {
        return true;
      }
    }
  }
  return dangerous.some((ext) => lower.endsWith(ext));
}
__name(hasDangerousExtension, "hasDangerousExtension");
function checkDecompressionBomb(data, width, height) {
  const pixelCount = width * height;
  const estimatedUncompressed = pixelCount * 4;
  const ratio = estimatedUncompressed / data.length;
  if (ratio > 1500 && pixelCount > 4e6) {
    return { safe: false, error: `Suspected decompression bomb (ratio ${Math.round(ratio)}:1, ${width}x${height})` };
  }
  if (width > 32768 || height > 32768) {
    return { safe: false, error: `Image dimensions too large (${width}x${height})` };
  }
  if (pixelCount > 1e8) {
    return { safe: false, error: `Too many pixels (${pixelCount})` };
  }
  return { safe: true };
}
__name(checkDecompressionBomb, "checkDecompressionBomb");
function getBasicDimensions(data, format) {
  try {
    if (format === "png" && data.length >= 24) {
      const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
      return { width: view.getUint32(16, false), height: view.getUint32(20, false) };
    }
    if (format === "gif" && data.length >= 10) {
      return { width: data[6] | data[7] << 8, height: data[8] | data[9] << 8 };
    }
    if (format === "webp" && data.length >= 30) {
      const chunkType = String.fromCharCode(data[12], data[13], data[14], data[15]);
      if (chunkType === "VP8X") {
        return {
          width: (data[24] | data[25] << 8 | data[26] << 16) + 1,
          height: (data[27] | data[28] << 8 | data[29] << 16) + 1
        };
      }
      if (chunkType === "VP8 " && data.length >= 30) {
        return {
          width: (data[26] | data[27] << 8) & 16383,
          height: (data[28] | data[29] << 8) & 16383
        };
      }
    }
    if (format === "jpeg") {
      let offset = 2;
      while (offset < data.length - 8) {
        if (data[offset] !== 255) break;
        const marker = data[offset + 1];
        const len = data[offset + 2] << 8 | data[offset + 3];
        if (marker === 192 || marker === 194) {
          return {
            width: data[offset + 7] << 8 | data[offset + 8],
            height: data[offset + 5] << 8 | data[offset + 6]
          };
        }
        offset += 2 + len;
      }
    }
  } catch {
  }
  return null;
}
__name(getBasicDimensions, "getBasicDimensions");
function validateImageSecurity(buffer, declaredType, filename = "") {
  const errors = [];
  const warnings = [];
  const data = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
  if (data.length < 12) {
    return { safe: false, realFormat: null, errors: ["File too small to be a valid image"], warnings };
  }
  if (data.length > 100 * 1024 * 1024) {
    return { safe: false, realFormat: null, errors: ["File exceeds maximum size limit"], warnings };
  }
  if (filename) {
    if (hasNullByteInjection(filename)) {
      errors.push("Null byte injection detected in filename");
    }
    if (hasPathTraversal(filename)) {
      errors.push("Path traversal detected in filename");
    }
    if (hasDangerousExtension(filename)) {
      errors.push("Dangerous file extension detected");
    }
  }
  const realFormat = detectRealFormat(data);
  if (!realFormat) {
    return { safe: false, realFormat: null, errors: ["Unable to determine media format from magic bytes - not a valid image or video"], warnings };
  }
  const isVideo = realFormat === "mp4";
  const mimeToFormat = {
    "image/png": "png",
    "image/jpeg": "jpeg",
    "image/jpg": "jpeg",
    "image/gif": "gif",
    "image/webp": "webp",
    "video/mp4": "mp4"
  };
  const expectedFormat = mimeToFormat[declaredType?.toLowerCase()];
  if (expectedFormat && expectedFormat !== realFormat) {
    errors.push(`MIME type mismatch: declared ${declaredType} but magic bytes indicate ${realFormat} (possible polyglot attack)`);
  }
  let structResult;
  switch (realFormat) {
    case "png":
      structResult = validatePNGStructure(data);
      break;
    case "jpeg":
      structResult = validateJPEGStructure(data);
      break;
    case "gif":
      structResult = validateGIFStructure(data);
      break;
    case "webp":
      structResult = validateWebPStructure(data);
      break;
    case "mp4":
      structResult = validateMP4Structure(data);
      break;
  }
  if (structResult && !structResult.valid) {
    errors.push(`${realFormat.toUpperCase()} structure error: ${structResult.error}`);
  }
  if (!isVideo) {
    const dims = getBasicDimensions(data, realFormat);
    if (dims) {
      const bombCheck = checkDecompressionBomb(data, dims.width, dims.height);
      if (!bombCheck.safe) {
        errors.push(bombCheck.error);
      }
    }
  }
  let metadataRegions = [];
  if (!isVideo) {
    try {
      switch (realFormat) {
        case "png":
          metadataRegions = extractPNGMetadata(data);
          break;
        case "jpeg":
          metadataRegions = extractJPEGMetadata(data);
          break;
        case "gif":
          metadataRegions = extractGIFMetadata(data);
          break;
        case "webp":
          metadataRegions = extractWebPMetadata(data);
          break;
      }
    } catch {
    }
  }
  if (metadataRegions.length > 0) {
    const metaTextResult = scanMetadataForDangerousText(metadataRegions);
    if (metaTextResult) {
      errors.push(`Dangerous code pattern in metadata: ${metaTextResult.pattern}`);
    }
  }
  const tailOffset = structResult?.endOffset || data.length;
  if (!isVideo && tailOffset < data.length) {
    const tailBinary = scanTailForBinaries(data, tailOffset);
    if (tailBinary) {
      errors.push(`Embedded binary after image end: ${tailBinary}`);
    }
    const tailText = scanTailForDangerousText(data, tailOffset);
    if (tailText) {
      errors.push(`Dangerous code appended after image end: ${tailText.pattern}`);
    }
  }
  return {
    safe: errors.length === 0,
    realFormat,
    errors,
    warnings
  };
}
__name(validateImageSecurity, "validateImageSecurity");
function detectMimeType(buffer) {
  const data = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
  const format = detectRealFormat(data);
  const map = { png: "image/png", jpeg: "image/jpeg", gif: "image/gif", webp: "image/webp", mp4: "video/mp4" };
  return map[format] || null;
}
__name(detectMimeType, "detectMimeType");

// src/middleware/security.js
var ALLOWED_UPLOAD_PATH_PREFIXES = [
  "thumbnails",
  "thumbnails/gif",
  "thumbnails/video",
  "profileimgs",
  "profiles",
  "backgrounds",
  "profilebackground",
  "profile-music",
  "pet-shop",
  "suggestions",
  "updates"
];
function isAllowedUploadPath(p) {
  if (typeof p !== "string") return false;
  if (p.includes("..") || p.includes("//") || p.includes("\\") || p.includes("\0")) return false;
  return ALLOWED_UPLOAD_PATH_PREFIXES.includes(p);
}
__name(isAllowedUploadPath, "isAllowedUploadPath");
function rejectIfBadUploadPath(p) {
  if (isAllowedUploadPath(p)) return null;
  console.warn(`[Security] Rejected upload path: ${p}`);
  return new Response(JSON.stringify({
    error: "Invalid upload path",
    code: "INVALID_PATH"
  }), {
    status: 400,
    headers: { "Content-Type": "application/json", ...corsHeaders() }
  });
}
__name(rejectIfBadUploadPath, "rejectIfBadUploadPath");
function rejectIfMalicious(buffer, declaredMimeType, filename = "") {
  const result = validateImageSecurity(buffer, declaredMimeType, filename);
  if (!result.safe) {
    console.error(`[Security] Image REJECTED - Errors: ${result.errors.join("; ")}`);
    if (result.warnings.length > 0) {
      console.warn(`[Security] Warnings: ${result.warnings.join("; ")}`);
    }
    return new Response(JSON.stringify({
      error: "Image rejected by security scan",
      details: result.errors[0],
      code: "SECURITY_VIOLATION"
    }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  if (result.warnings.length > 0) {
    console.warn(`[Security] Image accepted with warnings: ${result.warnings.join("; ")}`);
  }
  return null;
}
__name(rejectIfMalicious, "rejectIfMalicious");
function rejectIfNonCanonicalCodec(buffer) {
  const data = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
  const result = validateMP4Codec(data);
  if (!result.valid) {
    console.warn(`[Security] MP4 codec REJECTED: ${result.error}`);
    return new Response(JSON.stringify({
      error: "Video codec not allowed",
      details: result.error,
      code: "CODEC_NOT_CANONICAL"
    }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  return null;
}
__name(rejectIfNonCanonicalCodec, "rejectIfNonCanonicalCodec");

// src/middleware/boomlings-verify.js
var BOOMLINGS_BASE = "https://www.boomlings.com/database";
var GD_SECRET = "Wmfd2893gb7";
async function verifyAccountForWrite(env, accountID, username) {
  const id = parseInt(accountID);
  if (!id || id <= 0) {
    return { valid: false, reason: "ACCOUNT_REQUIRED" };
  }
  if (!username || !username.toString().trim()) {
    return { valid: false, reason: "ACCOUNT_REQUIRED" };
  }
  const normalizedUsername = username.toString().trim();
  try {
    // Never trust the client-provided `isOfficialServer` flag. Verify the
    // username/account pair server-side and fail closed when the verifier is
    // unavailable. verifyGdAccount caches results to keep this affordable.
    const proof = await verifyGdAccount(env, normalizedUsername, id);
    if (proof.ok && proof.accountID === id) {
      return { valid: true, accountID: id, username: normalizedUsername };
    }
    return { valid: false, reason: "ACCOUNT_MISMATCH" };
  } catch (err) {
    console.warn(`[Auth] Account verification failed closed for ${id}: ${err.message}`);
    return { valid: false, reason: "VERIFY_FAILED" };
  }
}
__name(verifyAccountForWrite, "verifyAccountForWrite");
async function fetchBoomlingsProof(accountID, username) {
  const url2 = `${BOOMLINGS_BASE}/getGJUserInfo20.php`;
  const body = new URLSearchParams({
    targetAccountID: String(accountID),
    secret: GD_SECRET
  });
  const res = await fetch(url2, {
    method: "POST",
    body: body.toString(),
    headers: { "Content-Type": "application/x-www-form-urlencoded" }
  });
  const text = await res.text();
  return verifyBoomlingsProof(accountID, username, text);
}
__name(fetchBoomlingsProof, "fetchBoomlingsProof");
function verifyBoomlingsProof(accountID, username, proofText) {
  if (!proofText || proofText.trim() === "-1" || proofText.trim() === "") {
    return { valid: false, reason: "ACCOUNT_NOT_FOUND" };
  }
  try {
    const fields = {};
    const pairs = proofText.split(":");
    for (let i = 0; i < pairs.length - 1; i += 2) {
      fields[pairs[i]] = pairs[i + 1];
    }
    const serverUsername = fields["1"] || fields["userName"] || "";
    const serverAccountID = parseInt(fields["16"] || fields["accountID"] || "0");
    if (!serverUsername || serverAccountID !== parseInt(accountID)) {
      return { valid: false, reason: "ACCOUNT_MISMATCH" };
    }
    if (serverUsername.toLowerCase() !== username.toLowerCase()) {
      return { valid: false, reason: "USERNAME_MISMATCH" };
    }
    return { valid: true, accountID: serverAccountID, username: serverUsername };
  } catch (err) {
    return { valid: false, reason: "INVALID_PROOF" };
  }
}
__name(verifyBoomlingsProof, "verifyBoomlingsProof");

// src/services/versions.js
var VERSIONS_TTL = 9e5;
var MEM_KEY = "versions.json";
var MAX_THUMBNAILS_PER_LEVEL = 10;
var MAX_WRITE_RETRIES = 3;
function normalizeVersionEntry(entry, index = 0) {
  if (!entry) return null;
  if (typeof entry === "string") {
    return {
      id: "legacy",
      position: 1,
      version: entry,
      format: "webp",
      path: "thumbnails",
      type: "static",
      isLegacy: true
    };
  }
  const ver = entry.version;
  return {
    id: entry.id || (ver === "legacy" ? "legacy" : `${index + 1}`),
    position: typeof entry.position === "number" ? entry.position : index + 1,
    version: ver,
    format: entry.format || "webp",
    path: (entry.path || "thumbnails").replace(/^\//, ""),
    type: entry.type || (entry.format === "gif" ? "gif" : "static"),
    uploadedBy: entry.uploadedBy,
    uploadedAt: entry.uploadedAt,
    ...ver === "legacy" || entry.isLegacy ? { isLegacy: true } : {}
  };
}
__name(normalizeVersionEntry, "normalizeVersionEntry");
function sortByPositionAsc(a, b) {
  return (a.position || 0) - (b.position || 0);
}
__name(sortByPositionAsc, "sortByPositionAsc");
function normalizeList(entry) {
  if (!entry) return [];
  if (Array.isArray(entry)) {
    return entry.map((v, i) => normalizeVersionEntry(v, i)).filter(Boolean).sort(sortByPositionAsc);
  }
  return [normalizeVersionEntry(entry, 0)].filter(Boolean);
}
__name(normalizeList, "normalizeList");
var VersionManager = class {
  static {
    __name(this, "VersionManager");
  }
  constructor(bucket) {
    this.bucket = bucket;
    this.cacheKey = "data/system/versions.json";
  }
  /** Read map — uses memCache for hot reads. */
  async getMap() {
    const cached = memCache.get(MEM_KEY);
    if (cached) return cached;
    const data = await getR2Json(this.bucket, this.cacheKey);
    const map = data || {};
    memCache.set(MEM_KEY, map, VERSIONS_TTL);
    return map;
  }
  /**
   * Read map fresh from R2, bypassing memCache.
   * MUST be used before any write to avoid overwriting concurrent changes.
   */
  async _getFreshMap() {
    memCache.invalidate(MEM_KEY);
    const data = await getR2Json(this.bucket, this.cacheKey);
    const map = data || {};
    return map;
  }
  /** Persist map to R2 and bust memCache. */
  async _putMap(map) {
    await putR2Json(this.bucket, this.cacheKey, map);
    memCache.invalidate(MEM_KEY);
  }
  async getVersion(id) {
    const versions = await this.getAllVersions(id);
    if (versions.length === 0) return void 0;
    return versions[0];
  }
  async getAllVersions(id) {
    const map = await this.getMap();
    return normalizeList(map[id]);
  }
  async update(id, version, format = "webp", path = "thumbnails", type = "static", metadata = {}) {
    const cleanMeta = {};
    if (metadata.uploadedBy) cleanMeta.uploadedBy = metadata.uploadedBy;
    if (metadata.uploadedAt) cleanMeta.uploadedAt = metadata.uploadedAt;
    for (let attempt = 0; attempt < MAX_WRITE_RETRIES; attempt++) {
      const map2 = await this._getFreshMap();
      const finalId2 = String(Date.now());
      const newVersion2 = {
        id: finalId2,
        version,
        format,
        path: path.replace(/^\//, ""),
        type,
        ...cleanMeta
      };
      map2[id] = [newVersion2];
      await this._putMap(map2);
      const verifyMap = await this._getFreshMap();
      const verifyList = normalizeList(verifyMap[id]);
      if (verifyList.some((v) => v.version === version)) {
        return newVersion2;
      }
      console.warn(`[VersionManager] update conflict for level ${id}, attempt ${attempt + 1}/${MAX_WRITE_RETRIES}`);
    }
    console.error(`[VersionManager] update exhausted retries for level ${id}, forcing write`);
    const map = await this._getFreshMap();
    const finalId = String(Date.now());
    const newVersion = {
      id: finalId,
      version,
      format,
      path: path.replace(/^\//, ""),
      type,
      ...cleanMeta
    };
    map[id] = [newVersion];
    await this._putMap(map);
    return newVersion;
  }
  async set(id, versions) {
    const normalizedInput = versions.map((v, i) => normalizeVersionEntry(v, i)).filter(Boolean).sort(sortByPositionAsc).map((v, i) => ({ ...v, position: i + 1 }));
    for (let attempt = 0; attempt < MAX_WRITE_RETRIES; attempt++) {
      const map2 = await this._getFreshMap();
      map2[id] = normalizedInput;
      await this._putMap(map2);
      const verifyMap = await this._getFreshMap();
      const verifyList = normalizeList(verifyMap[id]);
      const sameOrder = verifyList.length === normalizedInput.length && verifyList.every((entry, index) => {
        const expected = normalizedInput[index];
        return expected && String(entry.id) === String(expected.id) && entry.position === expected.position;
      });
      if (sameOrder) {
        return;
      }
      console.warn(`[VersionManager] set conflict for level ${id}, attempt ${attempt + 1}/${MAX_WRITE_RETRIES}`);
    }
    console.error(`[VersionManager] set exhausted retries for level ${id}, forcing write`);
    const map = await this._getFreshMap();
    map[id] = normalizedInput;
    await this._putMap(map);
  }
  /**
   * Append a new version for a level with retry-on-conflict.
   * Re-reads versions.json from R2 on each attempt so concurrent appends
   * by other Workers merge instead of overwriting each other.
   */
  async appendVersion(id, version, format = "webp", path = "thumbnails", type = "static", metadata = {}, maxPerLevel = MAX_THUMBNAILS_PER_LEVEL) {
    const cleanMeta = {};
    if (metadata.uploadedBy) cleanMeta.uploadedBy = metadata.uploadedBy;
    if (metadata.uploadedAt) cleanMeta.uploadedAt = metadata.uploadedAt;
    for (let attempt = 0; attempt < MAX_WRITE_RETRIES; attempt++) {
      const map2 = await this._getFreshMap();
      const current2 = normalizeList(map2[id]);
      const appended2 = {
        id: String(Date.now()),
        position: 1,
        version,
        format,
        path: path.replace(/^\//, ""),
        type,
        ...cleanMeta
      };
      let next2 = [appended2, ...current2];
      const removed2 = [];
      if (next2.length > maxPerLevel) {
        const toRemove = next2.length - maxPerLevel;
        removed2.push(...next2.slice(next2.length - toRemove));
        next2 = next2.slice(0, next2.length - toRemove);
      }
      next2 = next2.sort(sortByPositionAsc).map((v, i) => ({ ...v, position: i + 1 }));
      map2[id] = next2;
      await this._putMap(map2);
      const verifyMap = await this._getFreshMap();
      const verifyList = normalizeList(verifyMap[id]);
      const found = verifyList.some((v) => v.version === version);
      if (found) {
        const persisted = verifyList.find((v) => v.version === version) || next2[0];
        return { appended: persisted, removed: removed2, versions: verifyList };
      }
      console.warn(`[VersionManager] appendVersion conflict for level ${id}, attempt ${attempt + 1}/${MAX_WRITE_RETRIES}`);
    }
    console.error(`[VersionManager] appendVersion exhausted retries for level ${id}, forcing write`);
    const map = await this._getFreshMap();
    const current = normalizeList(map[id]);
    const appended = {
      id: String(Date.now()),
      position: 1,
      version,
      format,
      path: path.replace(/^\//, ""),
      type,
      ...cleanMeta
    };
    let next = [appended, ...current];
    const removed = [];
    if (next.length > maxPerLevel) {
      const toRemove = next.length - maxPerLevel;
      removed.push(...next.slice(next.length - toRemove));
      next = next.slice(0, next.length - toRemove);
    }
    next = next.sort(sortByPositionAsc).map((v, i) => ({ ...v, position: i + 1 }));
    map[id] = next;
    await this._putMap(map);
    return { appended: next[0], removed, versions: next };
  }
  async deleteVersion(id, thumbnailId) {
    for (let attempt = 0; attempt < MAX_WRITE_RETRIES; attempt++) {
      const map2 = await this._getFreshMap();
      const current2 = normalizeList(map2[id]);
      if (current2.length === 0) {
        return { removed: null, versions: [] };
      }
      const idx2 = current2.findIndex((v) => String(v.id) === String(thumbnailId));
      if (idx2 < 0) {
        return { removed: null, versions: current2 };
      }
      const removed2 = current2[idx2];
      const next2 = current2.filter((_, i) => i !== idx2).map((v, i) => ({ ...v, position: i + 1 }));
      if (next2.length === 0) {
        delete map2[id];
      } else {
        map2[id] = next2;
      }
      await this._putMap(map2);
      const verifyMap = await this._getFreshMap();
      const verifyList = normalizeList(verifyMap[id]);
      const stillExists = verifyList.some((v) => String(v.id) === String(thumbnailId));
      if (!stillExists) {
        return { removed: removed2, versions: next2 };
      }
      console.warn(`[VersionManager] deleteVersion conflict for level ${id}, attempt ${attempt + 1}/${MAX_WRITE_RETRIES}`);
    }
    console.error(`[VersionManager] deleteVersion exhausted retries for level ${id}, forcing write`);
    const map = await this._getFreshMap();
    const current = normalizeList(map[id]);
    const idx = current.findIndex((v) => String(v.id) === String(thumbnailId));
    if (idx < 0) return { removed: null, versions: current };
    const removed = current[idx];
    const next = current.filter((_, i) => i !== idx).map((v, i) => ({ ...v, position: i + 1 }));
    if (next.length === 0) delete map[id];
    else map[id] = next;
    await this._putMap(map);
    return { removed, versions: next };
  }
  async delete(id) {
    for (let attempt = 0; attempt < MAX_WRITE_RETRIES; attempt++) {
      const map2 = await this._getFreshMap();
      if (!map2[id]) return;
      delete map2[id];
      await this._putMap(map2);
      const verifyMap = await this._getFreshMap();
      if (!verifyMap[id]) return;
      console.warn(`[VersionManager] delete conflict for level ${id}, attempt ${attempt + 1}/${MAX_WRITE_RETRIES}`);
    }
    console.error(`[VersionManager] delete exhausted retries for level ${id}, forcing write`);
    const map = await this._getFreshMap();
    if (map[id]) {
      delete map[id];
      await this._putMap(map);
    }
  }
};

// src/services/leaderboard.js
function getWeekNumber(d) {
  d = new Date(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()));
  d.setUTCDate(d.getUTCDate() + 4 - (d.getUTCDay() || 7));
  var yearStart = new Date(Date.UTC(d.getUTCFullYear(), 0, 1));
  var weekNo = Math.ceil(((d - yearStart) / 864e5 + 1) / 7);
  return `${d.getUTCFullYear()}-W${weekNo}`;
}
__name(getWeekNumber, "getWeekNumber");
async function updateLeaderboard(env, type, levelId, stats, uploadedBy, accountID) {
  const date = /* @__PURE__ */ new Date();
  let key;
  if (type === "daily") key = `data/leaderboards/daily/${date.toISOString().split("T")[0]}.json`;
  else if (type === "weekly") key = `data/leaderboards/weekly/${getWeekNumber(date)}.json`;
  else return;
  try {
    const lbMemKey = `lb_${type}_${key}`;
    let leaderboard = memCache.get(lbMemKey);
    if (leaderboard === void 0) {
      leaderboard = await getR2Json(env.SYSTEM_BUCKET, key) || [];
    }
    leaderboard = leaderboard.filter((item) => item.levelId !== parseInt(levelId));
    const average = stats.count > 0 ? stats.total / stats.count : 0;
    if (stats.count > 0) {
      const entry = {
        levelId: parseInt(levelId),
        rating: average,
        count: stats.count,
        uploadedBy: uploadedBy || "Unknown",
        updatedAt: (/* @__PURE__ */ new Date()).toISOString()
      };
      if (accountID) entry.accountID = parseInt(accountID);
      leaderboard.push(entry);
    }
    leaderboard.sort((a, b) => {
      if (b.rating !== a.rating) return b.rating - a.rating;
      return b.count - a.count;
    });
    if (leaderboard.length > 100) leaderboard = leaderboard.slice(0, 100);
    await putR2Json(env.SYSTEM_BUCKET, key, leaderboard);
    const publicKey = type === "daily" ? "public/api/leaderboard/daily.json" : "public/api/leaderboard/weekly.json";
    putR2Json(env.SYSTEM_BUCKET, publicKey, leaderboard).catch(() => {
    });
    memCache.set(lbMemKey, leaderboard, 6e4);
    memCache.invalidate(`leaderboard:${key}`);
  } catch (e) {
    console.error(`Failed to update ${type} leaderboard:`, e);
  }
}
__name(updateLeaderboard, "updateLeaderboard");
async function updateTopThumbnailsCache(env, levelId, stats, uploadedBy, accountID) {
  try {
    const ttMemKey = "lb_top_thumbnails";
    let cache = memCache.get(ttMemKey);
    if (cache === void 0) {
      cache = await getR2Json(env.SYSTEM_BUCKET, "data/system/top_thumbnails.json") || [];
    }
    const lid = parseInt(levelId);
    const average = stats.count > 0 ? stats.total / stats.count : 0;
    cache = cache.filter((item) => item.levelId !== lid);
    if (stats.count >= 3) {
      cache.push({
        levelId: lid,
        rating: Math.round(average * 100) / 100,
        count: stats.count,
        uploadedBy: uploadedBy || "Unknown",
        accountID: accountID ? parseInt(accountID) : 0
      });
    }
    cache.sort((a, b) => {
      if (b.rating !== a.rating) return b.rating - a.rating;
      return b.count - a.count;
    });
    if (cache.length > 100) cache = cache.slice(0, 100);
    await putR2Json(env.SYSTEM_BUCKET, "data/system/top_thumbnails.json", cache);
    putR2Json(env.SYSTEM_BUCKET, "public/api/top-thumbnails.json", cache).catch(() => {
    });
    memCache.set(ttMemKey, cache, 6e4);
    memCache.invalidate("top_thumbnails");
  } catch (e) {
    console.error("Failed to update top thumbnails cache:", e);
  }
}
__name(updateTopThumbnailsCache, "updateTopThumbnailsCache");
async function rebuildCreatorLeaderboard(env) {
  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const fullMap = await vm.getMap();
    const userMap = {};
    for (const [id, entry] of Object.entries(fullMap)) {
      const versions = Array.isArray(entry) ? entry : [entry];
      for (const ver of versions) {
        const name = ver.uploadedBy || ver.updated_by;
        if (!name || name === "Unknown" || name === "System") continue;
        const key = name.toLowerCase();
        if (!userMap[key]) {
          userMap[key] = { username: name, accountID: ver.accountID || 0, uploadCount: 0, totalRating: 0, totalVotes: 0 };
        }
        userMap[key].uploadCount++;
        if (ver.accountID && !userMap[key].accountID) userMap[key].accountID = parseInt(ver.accountID);
      }
    }
    const existing = await getR2Json(env.SYSTEM_BUCKET, "data/system/creator_leaderboard.json") || [];
    for (const entry of existing) {
      const key = (entry.username || "").toLowerCase();
      if (userMap[key]) {
        userMap[key].totalRating = entry.totalRating || 0;
        userMap[key].totalVotes = entry.totalVotes || 0;
      }
    }
    const creators = Object.values(userMap).map((u) => ({
      ...u,
      avgRating: u.totalVotes > 0 ? Math.round(u.totalRating / u.totalVotes * 100) / 100 : 0
    }));
    creators.sort((a, b) => {
      if (b.uploadCount !== a.uploadCount) return b.uploadCount - a.uploadCount;
      return (b.avgRating || 0) - (a.avgRating || 0);
    });
    const top100 = creators.slice(0, 100);
    await putR2Json(env.SYSTEM_BUCKET, "data/system/creator_leaderboard.json", top100);
    putR2Json(env.SYSTEM_BUCKET, "public/api/top-creators.json", top100).catch(() => {
    });
    memCache.invalidate("top_creators");
    memCache.invalidate("lb_creator_leaderboard");
    console.log(`[Leaderboard] Rebuilt creator leaderboard: ${top100.length} creators from ${Object.keys(fullMap).length} levels`);
    return top100;
  } catch (e) {
    console.error("Failed to rebuild creator leaderboard:", e);
    return [];
  }
}
__name(rebuildCreatorLeaderboard, "rebuildCreatorLeaderboard");
async function updateCreatorLeaderboardCache(env, username, opts = {}) {
  if (!username || username === "Unknown" || username === "System") return;
  try {
    const clMemKey = "lb_creator_leaderboard";
    let cache = memCache.get(clMemKey);
    if (cache === void 0) {
      cache = await getR2Json(env.SYSTEM_BUCKET, "data/system/creator_leaderboard.json") || [];
    }
    const userLower = username.toLowerCase();
    let entry = cache.find((c) => c.username.toLowerCase() === userLower);
    if (!entry) {
      entry = { username, accountID: opts.accountID || 0, uploadCount: 0, totalRating: 0, totalVotes: 0 };
      cache.push(entry);
    }
    if (opts.incrementUpload) {
      entry.uploadCount = (entry.uploadCount || 0) + 1;
    }
    if (opts.accountID && !entry.accountID) {
      entry.accountID = parseInt(opts.accountID);
    }
    if (opts.addRating) {
      entry.totalRating = (entry.totalRating || 0) + opts.addRating;
      entry.totalVotes = (entry.totalVotes || 0) + 1;
    }
    entry.avgRating = entry.totalVotes > 0 ? Math.round(entry.totalRating / entry.totalVotes * 100) / 100 : 0;
    cache.sort((a, b) => {
      if (b.uploadCount !== a.uploadCount) return b.uploadCount - a.uploadCount;
      return (b.avgRating || 0) - (a.avgRating || 0);
    });
    if (cache.length > 100) cache = cache.slice(0, 100);
    await putR2Json(env.SYSTEM_BUCKET, "data/system/creator_leaderboard.json", cache);
    putR2Json(env.SYSTEM_BUCKET, "public/api/top-creators.json", cache).catch(() => {
    });
    memCache.set(clMemKey, cache, 6e4);
    memCache.invalidate("top_creators");
  } catch (e) {
    console.error("Failed to update creator leaderboard cache:", e);
  }
}
__name(updateCreatorLeaderboardCache, "updateCreatorLeaderboardCache");

// src/services/webhook.js
async function hmacSign(payload, secret) {
  const enc = new TextEncoder();
  const key = await crypto.subtle.importKey(
    "raw",
    enc.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"]
  );
  const sig = await crypto.subtle.sign("HMAC", key, enc.encode(payload));
  return [...new Uint8Array(sig)].map((b) => b.toString(16).padStart(2, "0")).join("");
}
__name(hmacSign, "hmacSign");
async function persistFailedEvent(bucket, eventType, payload, error) {
  try {
    const ts = Date.now();
    const id = Math.random().toString(36).slice(2, 8);
    const key = `data/webhook-retry/${ts}-${id}.json`;
    await putR2Json(bucket, key, {
      event: eventType,
      data: payload,
      failedAt: new Date(ts).toISOString(),
      error: error?.message || "Unknown error",
      retryCount: 0
    });
    console.log(`[Webhook] Persisted failed '${eventType}' event for retry: ${key}`);
  } catch (persistErr) {
    console.error(`[Webhook] CRITICAL: Failed to persist '${eventType}' event:`, persistErr.message);
  }
}
__name(persistFailedEvent, "persistFailedEvent");
async function dispatchWebhook(env, eventType, payload) {
  const url2 = env.DISCORD_BOT_WEBHOOK_URL;
  const secret = env.DISCORD_BOT_WEBHOOK_SECRET;
  if (!url2 || !secret) return;
  const body = JSON.stringify({ event: eventType, data: payload });
  const timestamp = Math.floor(Date.now() / 1e3).toString();
  const hmacSecret = env.WEBHOOK_HMAC_SECRET;
  const signaturePayload = `${timestamp}.${body}`;
  const signature = hmacSecret ? await hmacSign(signaturePayload, hmacSecret) : null;
  try {
    await retryWithBackoff(async () => {
      const headers = {
        "Content-Type": "application/json",
        "Authorization": `Bearer ${secret}`,
        "X-Webhook-Timestamp": timestamp
      };
      if (signature) {
        headers["X-Webhook-Signature"] = `sha256=${signature}`;
      }
      const resp = await fetch(url2, { method: "POST", headers, body });
      if (resp.ok) {
        console.log(`[Webhook] Dispatched '${eventType}' event successfully`);
        return;
      }
      if (isRetryableStatus(resp.status)) {
        throw new ExternalServiceError(`Webhook ${resp.status}`, {
          status: resp.status,
          details: { eventType, status: resp.status }
        });
      }
      console.warn(`[Webhook] '${eventType}' got non-retryable status ${resp.status}`);
    }, { maxRetries: 2, initialDelayMs: 2e3, maxDelayMs: 4e3, label: `Webhook:${eventType}` });
  } catch (err) {
    console.error(`[Webhook] '${eventType}' failed after retries:`, err.message);
    if (env.SYSTEM_BUCKET) {
      await persistFailedEvent(env.SYSTEM_BUCKET, eventType, payload, err);
    }
  }
}
__name(dispatchWebhook, "dispatchWebhook");

// src/image-utils.js
function getImageDimensions(buffer) {
  const data = new Uint8Array(buffer);
  if (data.length < 30) return null;
  if (data[0] === 137 && data[1] === 80 && data[2] === 78 && data[3] === 71) {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const width = view.getUint32(16, false);
    const height = view.getUint32(20, false);
    return { width, height, type: "png" };
  }
  if (data[0] === 82 && data[1] === 73 && data[2] === 70 && data[3] === 70 && data[8] === 87 && data[9] === 69 && data[10] === 66 && data[11] === 80) {
    const chunkType = String.fromCharCode(data[12], data[13], data[14], data[15]);
    if (chunkType === "VP8X") {
      const width = (data[24] | data[25] << 8 | data[26] << 16) + 1;
      const height = (data[27] | data[28] << 8 | data[29] << 16) + 1;
      return { width, height, type: "webp" };
    }
    if (chunkType === "VP8 ") {
      if (data[23] !== 157 || data[24] !== 1 || data[25] !== 42) {
      }
      const width = (data[26] | data[27] << 8) & 16383;
      const height = (data[28] | data[29] << 8) & 16383;
      return { width, height, type: "webp" };
    }
    if (chunkType === "VP8L") {
      if (data[20] !== 47) return null;
      const b0 = data[21];
      const b1 = data[22];
      const b2 = data[23];
      const b3 = data[24];
      const width = (b0 | (b1 & 63) << 8) + 1;
      const height = (b1 >> 6 | b2 << 2 | (b3 & 15) << 10) + 1;
      return { width, height, type: "webp" };
    }
  }
  if (data[0] === 71 && data[1] === 73 && data[2] === 70 && data[3] === 56 && (data[4] === 55 || data[4] === 57) && data[5] === 97) {
    const width = data[6] | data[7] << 8;
    const height = data[8] | data[9] << 8;
    return { width, height, type: "gif" };
  }
  if (data[0] === 255 && data[1] === 216) {
    let offset = 2;
    while (offset < data.length - 8) {
      if (data[offset] !== 255) break;
      const marker = data[offset + 1];
      const len = data[offset + 2] << 8 | data[offset + 3];
      if (marker === 192 || marker === 194) {
        const height = data[offset + 5] << 8 | data[offset + 6];
        const width = data[offset + 7] << 8 | data[offset + 8];
        return { width, height, type: "jpeg" };
      }
      offset += 2 + len;
    }
  }
  return null;
}
__name(getImageDimensions, "getImageDimensions");

// src/services/cache-invalidation.js
var DEFAULT_ZONE_ID = "f85cbf7e018cfe1315da7c21d3b88a95";
function getZoneId(env) {
  return env?.CF_ZONE_ID || DEFAULT_ZONE_ID;
}
__name(getZoneId, "getZoneId");
async function purgeCdnTags(env, tags) {
  if (!env?.CF_API_TOKEN || tags.length === 0) return;
  try {
    await fetch(
      `https://api.cloudflare.com/client/v4/zones/${getZoneId(env)}/purge_cache`,
      {
        method: "POST",
        headers: {
          Authorization: `Bearer ${env.CF_API_TOKEN}`,
          "Content-Type": "application/json"
        },
        body: JSON.stringify({ tags: tags.slice(0, 30) })
      }
    );
  } catch (e) {
    console.error("[CDN Tag Purge] failed:", e.message);
  }
}
__name(purgeCdnTags, "purgeCdnTags");
async function purgeCdnPrefix(env, prefixes) {
  if (!env?.CF_API_TOKEN || prefixes.length === 0) return;
  try {
    await fetch(
      `https://api.cloudflare.com/client/v4/zones/${getZoneId(env)}/purge_cache`,
      {
        method: "POST",
        headers: {
          Authorization: `Bearer ${env.CF_API_TOKEN}`,
          "Content-Type": "application/json"
        },
        body: JSON.stringify({ prefixes: prefixes.slice(0, 30) })
      }
    );
  } catch (e) {
    console.error("[CDN Prefix Purge] failed:", e.message);
  }
}
__name(purgeCdnPrefix, "purgeCdnPrefix");
function getOrigin(request) {
  const url2 = new URL(request.url);
  return url2.origin;
}
__name(getOrigin, "getOrigin");
async function invalidateThumbnail(request, levelId, env = null) {
  const origin = getOrigin(request);
  const promises = [];
  promises.push(cfCacheDelete(`${origin}/t/${levelId}`));
  promises.push(cfCacheDelete(`${origin}/api/download/thumbnails/${levelId}`));
  promises.push(
    cfCacheDelete(`${origin}/api/download/thumbnails/gif/${levelId}`)
  );
  promises.push(
    cfCacheDelete(`${origin}/api/download/thumbnails/video/${levelId}`)
  );
  promises.push(
    cfCacheDelete(`${origin}/api/thumbnails/list?levelId=${levelId}`)
  );
  promises.push(
    cfCacheDelete(`${origin}/api/thumbnails/info?levelId=${levelId}`)
  );
  promises.push(cfCacheDelete(`${origin}/api/exists?levelId=${levelId}`));
  promises.push(cfCacheDelete(`${origin}/api/manifest`));
  promises.push(cfCacheDelete(`${origin}/api/latest-uploads`));
  promises.push(cfCacheDelete(`${origin}/api/top-thumbnails`));
  promises.push(cfCacheDelete(`${origin}/api/leaderboard`));
  promises.push(cfCacheDelete(`${origin}/api/search`));
  promises.push(cfCacheDelete(`${origin}/api/gallery/list`));
  memCache.invalidate("latest_uploads");
  memCache.invalidate(`pimg_latest_${levelId}`);
  memCache.invalidate(`prof_latest_${levelId}`);
  memCache.invalidate(`thumbnails_list_${levelId}`);
  memCache.invalidate(`fallback_meta_${levelId}`);
  memCache.invalidate("top_thumbnails");
  memCache.invalidate("top_creators");
  bumpManifestEpoch(env?.SYSTEM_BUCKET);
  await Promise.allSettled(promises);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [
    `${host}/t/${levelId}`,
    `${host}/api/download/thumbnails/${levelId}`,
    `${host}/api/thumbnails/list`,
    `${host}/api/thumbnails/info`,
    `${host}/api/exists`,
    `${host}/api/manifest`,
    `${host}/api/latest-uploads`,
    `${host}/api/top-thumbnails`,
    `${host}/api/leaderboard`,
    `${host}/api/search`,
    `${host}/api/gallery/list`
  ]).catch(() => {
  });
  purgeCdnTags(env, [
    `thumbnail-${levelId}`,
    "manifest",
    "latest-uploads",
    "top-thumbnails",
    "leaderboard"
  ]).catch(() => {
  });
}
__name(invalidateThumbnail, "invalidateThumbnail");
async function invalidateProfileImage(request, accountId, env = null) {
  const origin = getOrigin(request);
  const promises = [];
  promises.push(cfCacheDelete(`${origin}/profileimgs/${accountId}`));
  promises.push(cfCacheDelete(`${origin}/api/profile/bundle/${accountId}`));
  promises.push(cfCacheDelete(`${origin}/api/top-creators`));
  memCache.invalidate(`pimg_latest_${accountId}`);
  memCache.invalidate("top_creators");
  await Promise.allSettled(promises);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [
    `${host}/profileimgs/${accountId}`,
    `${host}/api/profile/bundle/${accountId}`,
    `${host}/api/top-creators`
  ]).catch(() => {
  });
  purgeCdnTags(env, [`profile-${accountId}`, "top-creators"]).catch(() => {
  });
}
__name(invalidateProfileImage, "invalidateProfileImage");
async function invalidateProfileBackground(request, accountId, env = null) {
  const origin = getOrigin(request);
  const promises = [];
  promises.push(cfCacheDelete(`${origin}/profilebackground/${accountId}`));
  promises.push(cfCacheDelete(`${origin}/backgrounds/${accountId}`));
  promises.push(cfCacheDelete(`${origin}/api/profile/bundle/${accountId}`));
  promises.push(cfCacheDelete(`${origin}/api/profile/bgkind/${accountId}`));
  memCache.invalidate(`bg_latest_${accountId}`);
  memCache.invalidate(`bg_legacy_${accountId}`);
  memCache.invalidate(`profile_bgkind_${accountId}`);
  await Promise.allSettled(promises);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [
    `${host}/profilebackground/${accountId}`,
    `${host}/backgrounds/${accountId}`,
    `${host}/api/profile/bundle/${accountId}`,
    `${host}/api/profile/bgkind/${accountId}`
  ]).catch(() => {
  });
}
__name(invalidateProfileBackground, "invalidateProfileBackground");
async function invalidateProfileConfig(request, accountId, env = null) {
  const origin = getOrigin(request);
  const promises = [];
  promises.push(
    cfCacheDelete(`${origin}/api/profiles/config/${accountId}.json`)
  );
  promises.push(cfCacheDelete(`${origin}/api/profile/bundle/${accountId}`));
  promises.push(cfCacheDelete(`${origin}/api/profile/bgkind/${accountId}`));
  memCache.invalidate(`profile_bgkind_${accountId}`);
  await Promise.allSettled(promises);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [
    `${host}/api/profiles/config/${accountId}`,
    `${host}/api/profile/bundle/${accountId}`,
    `${host}/api/profile/bgkind/${accountId}`
  ]).catch(() => {
  });
}
__name(invalidateProfileConfig, "invalidateProfileConfig");
async function invalidateRating(request, levelId, env = null) {
  const origin = getOrigin(request);
  const promises = [];
  promises.push(cfCacheDelete(`${origin}/api/v2/ratings/${levelId}`));
  promises.push(cfCacheDelete(`${origin}/api/top-thumbnails`));
  promises.push(cfCacheDelete(`${origin}/api/leaderboard`));
  memCache.invalidate("top_thumbnails");
  await Promise.allSettled(promises);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [
    `${host}/api/v2/ratings/${levelId}`,
    `${host}/api/top-thumbnails`,
    `${host}/api/leaderboard`
  ]).catch(() => {
  });
}
__name(invalidateRating, "invalidateRating");
async function invalidateProfileRating(request, accountId, env = null) {
  const origin = getOrigin(request);
  const promises = [];
  promises.push(cfCacheDelete(`${origin}/api/profile-ratings/${accountId}`));
  promises.push(cfCacheDelete(`${origin}/api/top-creators`));
  memCache.invalidate(`profile_rating_${accountId}`);
  memCache.invalidate("top_creators");
  await Promise.allSettled(promises);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [
    `${host}/api/profile-ratings/${accountId}`,
    `${host}/api/top-creators`
  ]).catch(() => {
  });
}
__name(invalidateProfileRating, "invalidateProfileRating");
async function invalidateFeatured(request, env = null) {
  const origin = getOrigin(request);
  const promises = [];
  promises.push(cfCacheDelete(`${origin}/api/daily/current`));
  promises.push(cfCacheDelete(`${origin}/api/weekly/current`));
  promises.push(cfCacheDelete(`${origin}/api/featured/history`));
  promises.push(cfCacheDelete(`${origin}/api/leaderboard`));
  memCache.invalidate("featured_daily");
  memCache.invalidate("featured_weekly");
  await Promise.allSettled(promises);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [
    `${host}/api/daily/current`,
    `${host}/api/weekly/current`,
    `${host}/api/featured/history`,
    `${host}/api/leaderboard`
  ]).catch(() => {
  });
}
__name(invalidateFeatured, "invalidateFeatured");
async function invalidateModeration(request, env = null) {
  const origin = getOrigin(request);
  const promises = [];
  promises.push(cfCacheDelete(`${origin}/api/moderators`));
  promises.push(cfCacheDelete(`${origin}/api/admin/moderators`));
  memCache.invalidatePrefix("mod_");
  await Promise.allSettled(promises);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [
    `${host}/api/moderators`,
    `${host}/api/admin/moderators`
  ]).catch(() => {
  });
}
__name(invalidateModeration, "invalidateModeration");
async function invalidateBanList(request, env = null) {
  const origin = getOrigin(request);
  const promises = [];
  promises.push(cfCacheDelete(`${origin}/api/admin/banlist`));
  memCache.invalidate("system_banlist");
  await Promise.allSettled(promises);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [`${host}/api/admin/banlist`]).catch(() => {
  });
}
__name(invalidateBanList, "invalidateBanList");
async function invalidateProfileMusic(request, accountId, env = null) {
  const origin = getOrigin(request);
  const promises = [];
  promises.push(cfCacheDelete(`${origin}/profile-music/${accountId}.mp3`));
  promises.push(cfCacheDelete(`${origin}/profile-music/${accountId}.wav`));
  promises.push(cfCacheDelete(`${origin}/api/profile-music/${accountId}`));
  promises.push(cfCacheDelete(`${origin}/api/profile-music/${accountId}/audio`));
  promises.push(cfCacheDelete(`${origin}/api/profile/bundle/${accountId}`));
  // memCache del bundle: las llaves son `bundle_${accountId}_${username}` y
  // cambian con el username, por eso invalidamos por prefijo.
  memCache.invalidatePrefix(`bundle_${accountId}_`);
  await Promise.allSettled(promises);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [
    `${host}/profile-music/${accountId}`,
    `${host}/api/profile-music/${accountId}`,
    `${host}/api/profile/bundle/${accountId}`
  ]).catch(() => {
  });
}
__name(invalidateProfileMusic, "invalidateProfileMusic");
async function invalidateQueue(request, category, env = null) {
  const origin = getOrigin(request);
  await cfCacheDelete(`${origin}/api/queue/${category}`);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [`${host}/api/queue/${category}`]).catch(() => {
  });
}
__name(invalidateQueue, "invalidateQueue");
async function invalidateBotConfig(request, env = null) {
  const origin = getOrigin(request);
  await cfCacheDelete(`${origin}/api/bot/config`);
  memCache.invalidate("bot_config");
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [`${host}/api/bot/config`]).catch(() => {
  });
}
__name(invalidateBotConfig, "invalidateBotConfig");
async function invalidatePetShop(request, env = null) {
  const origin = getOrigin(request);
  await cfCacheDelete(`${origin}/api/pet-shop/list`);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [`${host}/api/pet-shop/list`]).catch(() => {
  });
}
__name(invalidatePetShop, "invalidatePetShop");
async function invalidateCustomBadge(request, accountId, env = null) {
  const origin = getOrigin(request);
  const promises = [];
  promises.push(cfCacheDelete(`${origin}/api/profile/badge/${accountId}`));
  promises.push(cfCacheDelete(`${origin}/api/profile/bundle/${accountId}`));
  memCache.invalidate(`badge_${accountId}`);
  await Promise.allSettled(promises);
  const host = new URL(request.url).host;
  purgeCdnPrefix(env, [
    `${host}/api/profile/badge/${accountId}`,
    `${host}/api/profile/bundle/${accountId}`
  ]).catch(() => {
  });
}
__name(invalidateCustomBadge, "invalidateCustomBadge");
var EPOCH_MEM_KEY = "manifest_epoch";
var EPOCH_R2_KEY = "data/system/manifest-epoch.json";
function getManifestEpoch() {
  const cached = memCache.get(EPOCH_MEM_KEY);
  return cached || Date.now();
}
__name(getManifestEpoch, "getManifestEpoch");
async function initManifestEpoch(bucket) {
  if (memCache.get(EPOCH_MEM_KEY) !== void 0) return;
  try {
    const data = await getR2Json(bucket, EPOCH_R2_KEY);
    const epoch = data?.epoch || Date.now();
    memCache.set(EPOCH_MEM_KEY, epoch, 864e5);
  } catch {
    memCache.set(EPOCH_MEM_KEY, Date.now(), 864e5);
  }
}
__name(initManifestEpoch, "initManifestEpoch");
async function bumpManifestEpoch(bucket) {
  const newEpoch = Date.now();
  memCache.set(EPOCH_MEM_KEY, newEpoch, 864e5);
  try {
    await putR2Json(bucket, EPOCH_R2_KEY, { epoch: newEpoch });
  } catch {
  }
}
__name(bumpManifestEpoch, "bumpManifestEpoch");

// src/controllers/thumbnails.js
function storageErrorResponse(error, cachedData, key, headers) {
  if (cachedData !== void 0) {
    console.warn(`[Degradation] Storage failed for '${key}', serving cached data`);
    return new Response(JSON.stringify({ ...cachedData, _degraded: true }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...headers }
    });
  }
  return new Response(JSON.stringify({
    error: "Storage service temporarily unavailable",
    code: "STORAGE_ERROR",
    retryable: true
  }), {
    status: 502,
    headers: { "Content-Type": "application/json", "Retry-After": "5", ...headers }
  });
}
__name(storageErrorResponse, "storageErrorResponse");
function buildThumbnailKey(levelId, versionEntry) {
  const path = (versionEntry.path || "thumbnails").replace(/^\//, "");
  const format = versionEntry.format || "webp";
  return `${path}/${levelId}_${versionEntry.version}.${format}`;
}
__name(buildThumbnailKey, "buildThumbnailKey");
function isPrimaryThumbnailIndex(index) {
  return index === 0;
}
__name(isPrimaryThumbnailIndex, "isPrimaryThumbnailIndex");
function normalizeVersionsForPrimary(versions) {
  return [...versions].sort((a, b) => {
    const posA = typeof a.position === "number" ? a.position : Number.MAX_SAFE_INTEGER;
    const posB = typeof b.position === "number" ? b.position : Number.MAX_SAFE_INTEGER;
    if (posA !== posB) return posA - posB;
    const idA = String(a.id || a.version || "");
    const idB = String(b.id || b.version || "");
    return idA.localeCompare(idB);
  });
}
__name(normalizeVersionsForPrimary, "normalizeVersionsForPrimary");
// Resolve a fallback uploader/date for a level when versions.json entries
// pre-date the introduction of `uploadedBy/uploadedAt`. Reads `latest_uploads.json`
// and `ratings/{levelId}.json` (cheap, cached per level via memCache).
async function resolveLevelFallbackMeta(env, levelId) {
  const memKey = `fallback_meta_${levelId}`;
  const cached = memCache.get(memKey);
  if (cached !== void 0) return cached;
  let fallback = { uploadedBy: null, uploadedAt: null };
  try {
    const latestData = await getR2Json(
      env.SYSTEM_BUCKET,
      "data/system/latest_uploads.json"
    );
    if (Array.isArray(latestData)) {
      const entry = latestData.find(
        (item) => String(item.levelId) === String(levelId)
      );
      if (entry && entry.username && entry.username !== "Unknown") {
        fallback.uploadedBy = entry.username;
        if (entry.timestamp) {
          try {
            fallback.uploadedAt = new Date(entry.timestamp).toISOString();
          } catch {
          }
        }
      }
    }
  } catch (e) {
    console.warn(`[FallbackMeta] latest_uploads read failed for ${levelId}:`, e.message);
  }
  if (!fallback.uploadedBy) {
    try {
      const ratingData = await getR2Json(
        env.SYSTEM_BUCKET,
        `ratings/${levelId}.json`
      );
      if (ratingData && ratingData.uploadedBy && ratingData.uploadedBy !== "Unknown") {
        fallback.uploadedBy = ratingData.uploadedBy;
      }
    } catch (e) {
      console.warn(`[FallbackMeta] ratings read failed for ${levelId}:`, e.message);
    }
  }
  // Cache for 5 minutes — same lifetime as the gallery list cache.
  memCache.set(memKey, fallback, 3e5);
  return fallback;
}
__name(resolveLevelFallbackMeta, "resolveLevelFallbackMeta");
// Enrich versions in-place with fallback uploader/date when missing. The first
// version (primary) gets the fallback; others fall back to the primary's
// uploader so the gallery has a consistent "Unknown → recovered" credit.
async function enrichVersionsWithFallback(env, levelId, versions) {
  if (!Array.isArray(versions) || versions.length === 0) return versions;
  const needsFallback = versions.some(
    (v) => !v.uploadedBy || v.uploadedBy === "unknown" || v.uploadedBy === "Unknown"
  );
  if (!needsFallback) return versions;
  const fallback = await resolveLevelFallbackMeta(env, levelId);
  if (!fallback.uploadedBy && !fallback.uploadedAt) return versions;
  return versions.map((v) => {
    const out = { ...v };
    if (!out.uploadedBy || out.uploadedBy === "unknown" || out.uploadedBy === "Unknown") {
      if (fallback.uploadedBy) out.uploadedBy = fallback.uploadedBy;
    }
    if (!out.uploadedAt && fallback.uploadedAt) out.uploadedAt = fallback.uploadedAt;
    return out;
  });
}
__name(enrichVersionsWithFallback, "enrichVersionsWithFallback");
function buildThumbnailsPayload(levelId, env, versions, origin) {
  const normalized = normalizeVersionsForPrimary(versions);
  return normalized.map(
    (v, i) => toThumbnailPayload(levelId, env, v, origin, isPrimaryThumbnailIndex(i))
  );
}
__name(buildThumbnailsPayload, "buildThumbnailsPayload");
function toThumbnailPayload(levelId, env, v, origin, isPrimary = false) {
  const path = (v.path || "thumbnails").replace(/^\//, "");
  const format = v.format || "webp";
  const version = v.version;
  const position = v.position || 1;
  const id = v.id || version;
  const uploadedBy = v.uploadedBy || "Unknown";
  const uploadedAt = v.uploadedAt || "";
  const baseUrl = origin || env.R2_PUBLIC_URL;
  const legacy = v.isLegacy || version === "legacy" || id === "legacy";
  const type = v.type || (format === "gif" ? "gif" : format === "mp4" ? "video" : "static");
  const isThumbnailPath = path === "thumbnails";
  let url2;
  if (isPrimary && isThumbnailPath && type === "static") {
    url2 = `${baseUrl}/t/${levelId}.webp`;
  } else if (isPrimary && isThumbnailPath && type === "gif") {
    url2 = `${baseUrl}/t/${levelId}.gif`;
  } else if (isPrimary && isThumbnailPath && type === "video") {
    url2 = `${baseUrl}/t/${levelId}.mp4`;
  } else if (legacy) {
    let filename = `${levelId}.${format}`;
    if (v.id && v.id !== "legacy_file" && v.id !== "legacy") {
      filename = `${levelId}_${v.id}.${format}`;
    }
    url2 = `${baseUrl}/api/download/${path}/${filename}`;
  } else {
    url2 = `${baseUrl}/api/download/${path}/${levelId}_${version}.${format}`;
  }
  return {
    id,
    thumbnailId: id,
    position,
    url: url2,
    type,
    format,
    creator: uploadedBy,
    author: uploadedBy,
    uploaded_by: uploadedBy,
    date: uploadedAt,
    uploaded_at: uploadedAt
  };
}
__name(toThumbnailPayload, "toThumbnailPayload");
async function handleReorderThumbnails(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const modCode = (request.headers.get("X-Mod-Code") || "").trim();
    const levelId = parseInt(body.levelId || "0");
    const username = String(body.username || "").trim();
    const usernameLower = username.toLowerCase();
    const accountID = parseInt(body.accountID || "0");
    const thumbnailIds = Array.isArray(body.thumbnailIds) ? body.thumbnailIds.map((value) => String(value).trim()).filter(Boolean) : [];
    if (levelId <= 0 || !username || accountID <= 0 || thumbnailIds.length < 2) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (!modCode) {
      return new Response(JSON.stringify({ error: "Mod code required" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const authResult = await verifyModAuth(request, env, usernameLower, accountID);
    if (!authResult.authorized) {
      return new Response(JSON.stringify({ error: "Not authorized - admin only" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (!ADMIN_USERS.includes(usernameLower)) {
      return new Response(JSON.stringify({ error: "Only admins can reorder thumbnails" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const versionManager = new VersionManager(env.SYSTEM_BUCKET);
    const currentVersions = await versionManager.getAllVersions(levelId);
    if (currentVersions.length < 2) {
      return new Response(JSON.stringify({ error: "At least 2 thumbnails are required to reorder" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (thumbnailIds.length !== currentVersions.length) {
      return new Response(JSON.stringify({ error: "Thumbnail list does not match current server state" }), {
        status: 409,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const currentById = new Map(currentVersions.map((version) => [String(version.id), version]));
    const uniqueIds = new Set(thumbnailIds);
    const sameSet = uniqueIds.size === currentVersions.length && thumbnailIds.every((thumbnailId) => currentById.has(thumbnailId));
    if (!sameSet) {
      return new Response(JSON.stringify({ error: "Thumbnail IDs are invalid or out of date" }), {
        status: 409,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const reordered = thumbnailIds.map((thumbnailId, index) => ({
      ...currentById.get(thumbnailId),
      position: index + 1
    }));
    await versionManager.set(levelId, reordered);
    invalidateThumbnail(request, levelId, env).catch(() => {
    });
    const origin = new URL(request.url).origin;
    const thumbnails = buildThumbnailsPayload(levelId, env, reordered, origin);
    const reorderTimestamp = Date.now();
    const logKey = `data/logs/reordered/${levelId}-${reorderTimestamp}.json`;
    await putR2Json(env.SYSTEM_BUCKET, logKey, {
      levelId,
      reorderedBy: username,
      accountID,
      thumbnailIds,
      reorderedAt: (/* @__PURE__ */ new Date()).toISOString(),
      timestamp: reorderTimestamp
    });
    const webhookPayload = {
      levelId,
      username,
      reorderedBy: username,
      thumbnailIds,
      timestamp: reorderTimestamp
    };
    if (ctx) ctx.waitUntil(dispatchWebhook(env, "thumbnail_reorder", webhookPayload));
    else await dispatchWebhook(env, "thumbnail_reorder", webhookPayload);
    return new Response(JSON.stringify({
      success: true,
      message: "Thumbnail order saved successfully",
      primaryThumbnailId: thumbnailIds[0],
      thumbnails
    }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Reorder thumbnails error:", error);
    return new Response(JSON.stringify({
      error: "Failed to reorder thumbnails",
      details: error.message
    }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleReorderThumbnails, "handleReorderThumbnails");
function parseLevelMeta(rawMeta) {
  // Safely parses the "levelMeta" form field (full GJGameLevel JSON the client
  // attaches). Returns a plain object or null — never throws, so it can never
  // block an upload. Parse this ONCE per request and reuse the result.
  if (!rawMeta || typeof rawMeta !== "string") return null;
  if (rawMeta.length > 32768) return null; // 32KB cap — guards against abuse
  let parsed;
  try {
    parsed = JSON.parse(rawMeta);
  } catch {
    return null;
  }
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) return null;
  return parsed;
}
__name(parseLevelMeta, "parseLevelMeta");
function metaString(parsed, key, maxLen = 128) {
  // Extracts a bounded, trimmed string field from parsed level metadata.
  // Returns null for missing/non-string/empty values so callers can omit the
  // field entirely instead of storing junk.
  if (!parsed) return null;
  const v = parsed[key];
  if (typeof v !== "string") return null;
  const trimmed = v.trim();
  if (!trimmed) return null;
  return trimmed.length > maxLen ? trimmed.slice(0, maxLen) : trimmed;
}
__name(metaString, "metaString");
async function storeLevelMeta(env, levelId, parsed, extra = {}) {
  // Persists the parsed level metadata into data/levelmeta/{levelId}.json so
  // the server reflects every level field (name, creator, difficulty, stars,
  // song, etc.). Expects an already-parsed object from parseLevelMeta.
  if (!parsed) return;
  const record = {
    ...parsed,
    levelId: parseInt(levelId),
    _server: {
      lastUploadedBy: extra.username || "unknown",
      lastAccountID: extra.accountID ?? null,
      lastFormat: extra.format || null,
      lastCategory: extra.category || null,
      updatedAt: (/* @__PURE__ */ new Date()).toISOString()
    }
  };
  await putR2Json(env.SYSTEM_BUCKET, `data/levelmeta/${levelId}.json`, record);
}
__name(storeLevelMeta, "storeLevelMeta");
async function handleUpload(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const formData = await request.formData();
    const file = formData.get("image");
    const levelId = formData.get("levelId");
    const username = formData.get("username");
    const accountID = parseInt(formData.get("accountID") || "0");
    const path = (formData.get("path") || "/thumbnails").replace(/^\//, "");
    if (!file || !isValidLevelID(levelId)) {
      return new Response(
        JSON.stringify({ error: "Missing required fields" }),
        {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    const __pathReject = rejectIfBadUploadPath(path);
    if (__pathReject) return __pathReject;
    const levelMetaParsed = parseLevelMeta(formData.get("levelMeta"));
    if (file.size > parseInt(env.MAX_UPLOAD_SIZE || "52428800")) {
      return new Response(JSON.stringify({ error: "File too large" }), {
        status: 413,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const usernameLower = username ? username.toLowerCase() : "";
    const accountVerification = await verifyAccountForWrite(env, accountID, username);
    if (!accountVerification.valid) {
      const errorMessages = {
        ACCOUNT_REQUIRED: "Valid accountID required",
        UNOFFICIAL_SERVER: "Upload requires official Boomlings server connection",
        ACCOUNT_NOT_FOUND: "Account not found on official servers",
        ACCOUNT_MISMATCH: "Account verification failed",
        USERNAME_MISMATCH: "Username does not match account",
        VERIFY_FAILED: "Account verification service unavailable"
      };
      return new Response(
        JSON.stringify({ error: errorMessages[accountVerification.reason] || "Account verification failed", code: accountVerification.reason }),
        { status: 403, headers: { "Content-Type": "application/json", ...corsNoStore() } }
      );
    }
    const authResult = await verifyModAuth(
      request,
      env,
      usernameLower,
      accountID
    );
    let isModerator = authResult.authorized;
    const isAdmin = authResult.authorized && ADMIN_USERS.includes(usernameLower);
    const isKnownModOrAdmin = await isModeratorOrAdmin(env, usernameLower);
    const canManageThumbnails = isModerator && isKnownModOrAdmin;
    const modCodeMismatch = isKnownModOrAdmin && !isModerator && !!authResult.invalidCode;
    const banData = await getR2Json(env.SYSTEM_BUCKET, "data/banlist.json");
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (banned.includes(usernameLower)) {
      return new Response(JSON.stringify({ error: "User is banned" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (username && username !== "Unknown") {
      const role = await getUserRole(usernameLower, env.SYSTEM_BUCKET);
      const quota = await checkUploadQuota(env.SYSTEM_BUCKET, username, role);
      if (!quota.allowed) {
        return new Response(JSON.stringify({
          error: "Daily upload quota exceeded",
          limit: quota.limit,
          used: quota.used,
          message: `You have reached your daily upload limit (${quota.limit}). Try again tomorrow.`
        }), { status: 429, headers: { "Content-Type": "application/json", ...corsNoStore() } });
      }
    }
    console.log(
      `[Upload] user="${username}" accountID=${accountID} isAdmin=${isAdmin} isMod=${isModerator} path=${path}`
    );
    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);
    const fileType = file.type || "image/webp";
    let extension = "webp";
    if (fileType === "image/png") extension = "png";
    else if (fileType === "image/jpeg") extension = "jpg";
    else if (fileType === "image/gif") extension = "gif";
    const securityReject = rejectIfMalicious(
      buffer,
      fileType,
      file.name || `${levelId}.${extension}`
    );
    if (securityReject) return securityReject;
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const vData = await vm.getVersion(levelId);
    let isUpdate = !!vData;
    if (!isUpdate) {
      const keys = [
        `${path}/${levelId}.webp`,
        `${path}/gif/${levelId}.gif`,
        `${path}/${levelId}.png`
      ];
      const checks = await Promise.all(
        keys.map((k) => env.THUMBNAILS_BUCKET.head(k, { skipMeta: true }))
      );
      if (checks.some((o) => o)) isUpdate = true;
    }
    let uploadKey;
    let uploadCategory;
    const version = Date.now().toString();
    const versionManager = new VersionManager(env.SYSTEM_BUCKET);
    if (path === "profileimgs") {
      const ts = Date.now().toString();
      const [whitelistProfileImg, vips] = await Promise.all([
        getWhitelist(env.SYSTEM_BUCKET, "profileimgs"),
        getVips(env.SYSTEM_BUCKET)
      ]);
      const isWhitelistedPI = whitelistProfileImg.includes(usernameLower);
      const isVip = vips.includes(usernameLower);
      if (!isModerator && !isAdmin && !isWhitelistedPI && !isVip) {
        uploadKey = `pending_profileimgs/${levelId}_${ts}.${extension}`;
        uploadCategory = "pending_profileimg";
        const pendingList = await env.THUMBNAILS_BUCKET.list({
          prefix: `pending_profileimgs/${levelId}_`
        });
        if (pendingList.objects.length > 0) {
          await Promise.all(
            pendingList.objects.map((o) => env.THUMBNAILS_BUCKET.delete(o.key))
          );
        }
      } else {
        uploadKey = `${path}/${levelId}_${ts}.${extension}`;
        uploadCategory = "profileimg";
        const prefixes = [`${path}/${levelId}.`, `${path}/${levelId}_`];
        const keysToDelete = [];
        for (const prefix of prefixes) {
          const list = await env.THUMBNAILS_BUCKET.list({ prefix });
          for (const obj of list.objects) keysToDelete.push(obj.key);
        }
        if (keysToDelete.length > 0) {
          await Promise.all(
            keysToDelete.map((k) => env.THUMBNAILS_BUCKET.delete(k))
          );
        }
        const pendingList = await env.THUMBNAILS_BUCKET.list({
          prefix: `pending_profileimgs/${levelId}_`
        });
        if (pendingList.objects.length > 0) {
          await Promise.all(
            pendingList.objects.map((o) => env.THUMBNAILS_BUCKET.delete(o.key))
          );
        }
        if (ctx)
          ctx.waitUntil(
            env.SYSTEM_BUCKET.delete(`data/queue/profileimgs/${levelId}.json`)
          );
        else
          await env.SYSTEM_BUCKET.delete(
            `data/queue/profileimgs/${levelId}.json`
          );
      }
    } else {
      if (isModerator && canManageThumbnails) {
        const type = extension === "gif" ? "gif" : "static";
        uploadKey = `${path}/${levelId}_${version}.${extension}`;
        uploadCategory = "live";
      } else {
        const ts = Date.now().toString();
        uploadKey = `pending_thumbnails/${levelId}_${ts}.${extension}`;
        uploadCategory = "pending_thumbnail";
        const pendingList = await env.THUMBNAILS_BUCKET.list({
          prefix: `pending_thumbnails/${levelId}_`
        });
        if (pendingList.objects.length > 0) {
          await Promise.all(
            pendingList.objects.map((o) => env.THUMBNAILS_BUCKET.delete(o.key))
          );
        }
      }
    }
    await env.THUMBNAILS_BUCKET.put(uploadKey, buffer, {
      httpMetadata: {
        contentType: fileType,
        cacheControl: "no-store, no-cache, must-revalidate, max-age=0"
      },
      customMetadata: {
        uploadedBy: username || "unknown",
        updated_by: username || "unknown",
        uploadedAt: (/* @__PURE__ */ new Date()).toISOString(),
        originalFormat: extension,
        isUpdate: isUpdate ? "true" : "false",
        version,
        accountID: accountID.toString(),
        moderatorUpload: isModerator ? "true" : "false",
        category: uploadCategory
      }
    });
    let appendedVersion = null;
    if (uploadCategory === "live") {
      const type = extension === "gif" ? "gif" : "static";
      const appendRes = await versionManager.appendVersion(
        levelId,
        version,
        extension,
        path,
        type,
        {
          uploadedBy: username,
          uploadedAt: (/* @__PURE__ */ new Date()).toISOString()
        },
        MAX_THUMBNAILS_PER_LEVEL
      );
      appendedVersion = appendRes.appended;
      for (const removed of appendRes.removed) {
        const removedKey = buildThumbnailKey(levelId, removed);
        if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(removedKey));
        else await env.THUMBNAILS_BUCKET.delete(removedKey);
      }
      if (ctx)
        ctx.waitUntil(env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`));
      else await env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`);
    }
    if (username && username !== "Unknown" && path !== "profileimgs") {
      const updateCreatorCache = /* @__PURE__ */ __name(() => updateCreatorLeaderboardCache(env, username, {
        incrementUpload: true,
        accountID
      }), "updateCreatorCache");
      if (ctx) ctx.waitUntil(updateCreatorCache());
      else await updateCreatorCache();
    }
    if (isModerator && path !== "profileimgs") {
      const updateLatest = /* @__PURE__ */ __name(async () => {
        const latestKey = "data/system/latest_uploads.json";
        let latest = await getR2Json(env.SYSTEM_BUCKET, latestKey) || [];
        latest = latest.filter((item) => item.levelId !== parseInt(levelId));
        const __lvlName = metaString(levelMetaParsed, "levelName", 100);
        const __lvlCreator = metaString(levelMetaParsed, "creatorName", 50);
        latest.unshift({
          levelId: parseInt(levelId),
          username: username || "unknown",
          timestamp: Date.now(),
          accountID,
          ...(__lvlName ? { levelName: __lvlName } : {}),
          ...(__lvlCreator ? { creator: __lvlCreator } : {})
        });
        if (latest.length > 20) latest = latest.slice(0, 20);
        await putR2Json(env.SYSTEM_BUCKET, latestKey, latest);
        memCache.invalidate("latest_uploads");
      }, "updateLatest");
      if (ctx) ctx.waitUntil(updateLatest());
      else await updateLatest();
      const webhookPayload = {
        levelId: parseInt(levelId),
        username: username || "unknown",
        timestamp: Date.now(),
        is_update: isUpdate || false
      };
      if (ctx) ctx.waitUntil(dispatchWebhook(env, "upload", webhookPayload));
      else await dispatchWebhook(env, "upload", webhookPayload);
    }
    const isPendingProfileImg = uploadCategory === "pending_profileimg";
    const isPendingThumbnail = uploadCategory === "pending_thumbnail";
    if (isPendingProfileImg) {
      const queueKey = `data/queue/profileimgs/${levelId}.json`;
      const queueItem = {
        levelId: parseInt(levelId),
        accountID,
        submittedBy: username || "unknown",
        timestamp: Date.now(),
        status: "pending",
        category: "profileimgs",
        filename: uploadKey,
        format: extension
      };
      if (ctx) ctx.waitUntil(putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem));
      else await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);
    }
    if (isPendingThumbnail) {
      const queueKey = `data/queue/thumbnails/${levelId}.json`;
      const queueItem = {
        levelId: parseInt(levelId),
        accountID,
        submittedBy: username || "unknown",
        timestamp: Date.now(),
        status: "pending",
        category: "thumbnails",
        filename: uploadKey,
        format: extension
      };
      if (ctx) ctx.waitUntil(putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem));
      else await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);
    }
    if (uploadCategory === "live" || uploadCategory === "profileimg") {
      if (uploadCategory === "profileimg") {
        invalidateProfileImage(request, levelId, env).catch(() => {
        });
      } else {
        invalidateThumbnail(request, levelId, env).catch(() => {
        });
      }
      const wtOrigin = new URL(request.url).origin;
      const writeThroughCacheReq = cfCacheKey(
        new Request(`${wtOrigin}/t/${levelId}`)
      );
      const writeThroughResp = makeCacheable(
        new Response(buffer, {
          headers: {
            "Content-Type": fileType,
            "Access-Control-Allow-Origin": "*"
          }
        }),
        604800
      );
      if (ctx)
        ctx.waitUntil(cfCachePut(writeThroughCacheReq, writeThroughResp));
      else cfCachePut(writeThroughCacheReq, writeThroughResp).catch(() => {
      });
    }
    if (username && username !== "Unknown" && !isModerator) {
      if (ctx) ctx.waitUntil(incrementUploadQuota(env.SYSTEM_BUCKET, username));
      else await incrementUploadQuota(env.SYSTEM_BUCKET, username).catch(() => {});
    }
    if (levelMetaParsed && path !== "profileimgs") {
      const metaTask = storeLevelMeta(env, levelId, levelMetaParsed, {
        username,
        accountID,
        format: extension,
        category: uploadCategory
      });
      if (ctx) ctx.waitUntil(metaTask);
      else await metaTask.catch(() => {});
    }
    let responseMessage = "Thumbnail published directly";
    if (isPendingProfileImg)
      responseMessage = "Profile image submitted for verification";
    else if (isPendingThumbnail)
      responseMessage = "Thumbnail submitted for verification";
    else if (uploadCategory === "live")
      responseMessage = "Thumbnail published directly to global";
    const responseData = {
      success: true,
      message: responseMessage,
      key: uploadKey,
      isUpdate,
      moderatorUpload: isModerator,
      inQueue: false,
      pendingVerification: isPendingProfileImg || isPendingThumbnail,
      cdnUrl: isPendingThumbnail ? null : `${new URL(request.url).origin}/t/${levelId}`
    };
    if (appendedVersion) {
      responseData.thumbnailId = appendedVersion.id;
      responseData.position = appendedVersion.position;
    }
    if (modCodeMismatch) {
      responseData.modCodeMismatch = true;
      responseData.alert = "Mod code no coincidente";
    }
    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Upload error:", error);
    return new Response(
      JSON.stringify({ error: "Upload failed", details: error.message }),
      {
        status: 500,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      }
    );
  }
}
__name(handleUpload, "handleUpload");
async function handleUploadGIF(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const formData = await request.formData();
    const file = formData.get("image");
    const levelId = formData.get("levelId");
    const username = formData.get("username");
    const accountID = parseInt(formData.get("accountID") || "0");
    const path = (formData.get("path") || "/thumbnails/gif").replace(/^\//, "");
    if (!file || !isValidLevelID(levelId)) {
      return new Response(
        JSON.stringify({ error: "Missing required fields" }),
        {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    {
      const __pathReject = rejectIfBadUploadPath(path);
      if (__pathReject) return __pathReject;
    }
    if (file.size > parseInt(env.MAX_UPLOAD_SIZE || "52428800")) {
      return new Response(JSON.stringify({ error: "File too large" }), {
        status: 413,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (file.type !== "image/gif") {
      return new Response(
        JSON.stringify({ error: "Invalid file type. Only GIF allowed." }),
        {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    const usernameLower = username ? username.toLowerCase() : "";
    const levelMetaParsed = parseLevelMeta(formData.get("levelMeta"));
    if (username && username !== "Unknown") {
      const role = await getUserRole(usernameLower, env.SYSTEM_BUCKET);
      const quota = await checkUploadQuota(env.SYSTEM_BUCKET, username, role);
      if (!quota.allowed) {
        return new Response(JSON.stringify({
          error: "Daily upload quota exceeded",
          limit: quota.limit,
          used: quota.used,
          message: `You have reached your daily upload limit (${quota.limit}). Try again tomorrow.`
        }), { status: 429, headers: { "Content-Type": "application/json", ...corsNoStore() } });
      }
    }
    const accountVerification = await verifyAccountForWrite(env, accountID, username);
    if (!accountVerification.valid) {
      const errorMessages = {
        ACCOUNT_REQUIRED: "Valid accountID required",
        UNOFFICIAL_SERVER: "Upload requires official Boomlings server connection",
        ACCOUNT_NOT_FOUND: "Account not found on official servers",
        ACCOUNT_MISMATCH: "Account verification failed",
        USERNAME_MISMATCH: "Username does not match account",
        VERIFY_FAILED: "Account verification service unavailable"
      };
      return new Response(
        JSON.stringify({ error: errorMessages[accountVerification.reason] || "Account verification failed", code: accountVerification.reason }),
        { status: 403, headers: { "Content-Type": "application/json", ...corsNoStore() } }
      );
    }
    const authResult = await verifyModAuth(
      request,
      env,
      usernameLower,
      accountID
    );
    const isModerator = authResult.authorized;
    const isKnownModOrAdmin = await isModeratorOrAdmin(env, usernameLower);
    const canManageThumbnails = isModerator && isKnownModOrAdmin;
    const modCodeMismatch = isKnownModOrAdmin && !isModerator && !!authResult.invalidCode;
    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);
    const securityReject = rejectIfMalicious(
      buffer,
      "image/gif",
      file.name || `${levelId}.gif`
    );
    if (securityReject) return securityReject;
    const versionManager = new VersionManager(env.SYSTEM_BUCKET);
    const version = Date.now().toString();
    const thisId = version;
    let key;
    let uploadCategory;
    let isProfile = path === "profiles";
    if (isModerator && canManageThumbnails) {
      if (isProfile) {
        const ts = Date.now().toString();
        key = `profiles/${levelId}_${ts}.gif`;
        const prefixes = [`profiles/${levelId}.`, `profiles/${levelId}_`];
        const keysToDelete = [];
        for (const prefix of prefixes) {
          const list = await env.THUMBNAILS_BUCKET.list({ prefix });
          for (const obj of list.objects) keysToDelete.push(obj.key);
        }
        if (keysToDelete.length > 0) {
          await Promise.all(
            keysToDelete.map((k) => env.THUMBNAILS_BUCKET.delete(k))
          );
        }
      } else {
        key = `${path}/${levelId}_${thisId}.gif`;
      }
      uploadCategory = isProfile ? "profile" : "live";
    } else {
      const ts = Date.now().toString();
      key = `pending_thumbnails/${levelId}_${ts}.gif`;
      uploadCategory = "pending_thumbnail";
      const pendingList = await env.THUMBNAILS_BUCKET.list({
        prefix: `pending_thumbnails/${levelId}_`
      });
      if (pendingList.objects.length > 0) {
        await Promise.all(
          pendingList.objects.map((o) => env.THUMBNAILS_BUCKET.delete(o.key))
        );
      }
    }
    const existingVersion = await versionManager.getVersion(levelId);
    const isUpdate = !!existingVersion;
    await env.THUMBNAILS_BUCKET.put(key, buffer, {
      httpMetadata: {
        contentType: "image/gif",
        cacheControl: "no-store, no-cache, must-revalidate, max-age=0"
      },
      customMetadata: {
        uploadedBy: username || "unknown",
        uploadedAt: (/* @__PURE__ */ new Date()).toISOString(),
        originalFormat: "gif",
        isUpdate: isUpdate ? "true" : "false",
        version: thisId,
        accountID: accountID.toString(),
        moderatorUpload: isModerator ? "true" : "false",
        category: uploadCategory
      }
    });
    let appendedVersion = null;
    if (uploadCategory === "live" && !isProfile) {
      const appendRes = await versionManager.appendVersion(
        levelId,
        thisId,
        "gif",
        path,
        "gif",
        {
          uploadedBy: username || "unknown",
          uploadedAt: (/* @__PURE__ */ new Date()).toISOString()
        },
        MAX_THUMBNAILS_PER_LEVEL
      );
      appendedVersion = appendRes.appended;
      for (const removed of appendRes.removed) {
        const removedKey = buildThumbnailKey(levelId, removed);
        if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(removedKey));
        else await env.THUMBNAILS_BUCKET.delete(removedKey);
      }
      const updateLatest = /* @__PURE__ */ __name(async () => {
        const latestKey = "data/system/latest_uploads.json";
        let latest = await getR2Json(env.SYSTEM_BUCKET, latestKey) || [];
        latest = latest.filter((item) => item.levelId !== parseInt(levelId));
        const __lvlName = metaString(levelMetaParsed, "levelName", 100);
        const __lvlCreator = metaString(levelMetaParsed, "creatorName", 50);
        latest.unshift({
          levelId: parseInt(levelId),
          username: username || "unknown",
          timestamp: Date.now(),
          accountID,
          isGif: true,
          ...(__lvlName ? { levelName: __lvlName } : {}),
          ...(__lvlCreator ? { creator: __lvlCreator } : {})
        });
        if (latest.length > 20) latest = latest.slice(0, 20);
        await putR2Json(env.SYSTEM_BUCKET, latestKey, latest);
        memCache.invalidate("latest_uploads");
      }, "updateLatest");
      if (ctx) ctx.waitUntil(updateLatest());
      else await updateLatest();
    }
    if (uploadCategory === "pending_thumbnail") {
      const queueKey = `data/queue/thumbnails/${levelId}.json`;
      const queueItem = {
        levelId: parseInt(levelId),
        accountID,
        submittedBy: username || "unknown",
        timestamp: Date.now(),
        status: "pending",
        category: "thumbnails",
        filename: key,
        format: "gif"
      };
      if (ctx) ctx.waitUntil(putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem));
      else await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);
    }
    const isPendingThumbnail = uploadCategory === "pending_thumbnail";
    if (username && username !== "Unknown" && !isModerator) {
      if (ctx) ctx.waitUntil(incrementUploadQuota(env.SYSTEM_BUCKET, username));
      else await incrementUploadQuota(env.SYSTEM_BUCKET, username).catch(() => {});
    }
    if (levelMetaParsed && !isProfile) {
      const metaTask = storeLevelMeta(env, levelId, levelMetaParsed, {
        username,
        accountID,
        format: "gif",
        category: uploadCategory
      });
      if (ctx) ctx.waitUntil(metaTask);
      else await metaTask.catch(() => {});
    }
    let responseMessage = "GIF published directly";
    if (isPendingThumbnail) responseMessage = "GIF thumbnail submitted for verification";
    else if (uploadCategory === "live") responseMessage = "GIF published directly to global";
    const responseData = {
      success: true,
      message: responseMessage,
      key,
      isUpdate,
      moderatorUpload: isModerator,
      pendingVerification: isPendingThumbnail,
      cdnUrl: isPendingThumbnail ? null : `${new URL(request.url).origin}/t/${levelId}`
    };
    if (appendedVersion) {
      responseData.thumbnailId = appendedVersion.id;
      responseData.position = appendedVersion.position;
    }
    if (modCodeMismatch) {
      responseData.modCodeMismatch = true;
      responseData.alert = "Mod code no coincidente";
    }
    if (uploadCategory === "live" || uploadCategory === "profile") {
      invalidateThumbnail(request, levelId, env).catch(() => {
      });
      if (isProfile) memCache.invalidate(`prof_latest_${levelId}`);
      const origin = new URL(request.url).origin;
      const writeThroughCacheReq = cfCacheKey(
        new Request(`${origin}/t/${levelId}`)
      );
      const writeThroughResp = makeCacheable(
        new Response(buffer, {
          headers: {
            "Content-Type": "image/gif",
            "Access-Control-Allow-Origin": "*"
          }
        }),
        604800
      );
      if (ctx) ctx.waitUntil(cfCachePut(writeThroughCacheReq, writeThroughResp));
      else cfCachePut(writeThroughCacheReq, writeThroughResp).catch(() => {
      });
    }
    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Upload GIF error:", error);
    return new Response(
      JSON.stringify({ error: "Upload failed", details: error.message }),
      {
        status: 500,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      }
    );
  }
}
__name(handleUploadGIF, "handleUploadGIF");
async function handleUploadVideo(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const formData = await request.formData();
    const file = formData.get("image");
    const levelId = formData.get("levelId");
    const username = formData.get("username");
    const accountID = parseInt(formData.get("accountID") || "0");
    const path = (formData.get("path") || "/thumbnails/video").replace(
      /^\//,
      ""
    );
    if (!file || !isValidLevelID(levelId)) {
      return new Response(
        JSON.stringify({ error: "Missing required fields" }),
        {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    {
      const __pathReject = rejectIfBadUploadPath(path);
      if (__pathReject) return __pathReject;
    }
    const maxVideoSize = parseInt(env.MAX_VIDEO_UPLOAD_SIZE || "26214400");
    if (file.size > maxVideoSize) {
      return new Response(
        JSON.stringify({ error: "Video file too large (max 25MB)" }),
        {
          status: 413,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    if (file.type !== "video/mp4") {
      return new Response(
        JSON.stringify({ error: "Invalid file type. Only MP4 video allowed." }),
        {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    const usernameLower = username ? username.toLowerCase() : "";
    const levelMetaParsed = parseLevelMeta(formData.get("levelMeta"));
    if (username && username !== "Unknown") {
      const role = await getUserRole(usernameLower, env.SYSTEM_BUCKET);
      const quota = await checkUploadQuota(env.SYSTEM_BUCKET, username, role);
      if (!quota.allowed) {
        return new Response(JSON.stringify({
          error: "Daily upload quota exceeded",
          limit: quota.limit,
          used: quota.used,
          message: `You have reached your daily upload limit (${quota.limit}). Try again tomorrow.`
        }), { status: 429, headers: { "Content-Type": "application/json", ...corsNoStore() } });
      }
    }
    const accountVerification = await verifyAccountForWrite(env, accountID, username);
    if (!accountVerification.valid) {
      const errorMessages = {
        ACCOUNT_REQUIRED: "Valid accountID required",
        UNOFFICIAL_SERVER: "Upload requires official Boomlings server connection",
        ACCOUNT_NOT_FOUND: "Account not found on official servers",
        ACCOUNT_MISMATCH: "Account verification failed",
        USERNAME_MISMATCH: "Username does not match account",
        VERIFY_FAILED: "Account verification service unavailable"
      };
      return new Response(
        JSON.stringify({ error: errorMessages[accountVerification.reason] || "Account verification failed", code: accountVerification.reason }),
        { status: 403, headers: { "Content-Type": "application/json", ...corsNoStore() } }
      );
    }
    const authResult = await verifyModAuth(
      request,
      env,
      usernameLower,
      accountID
    );
    const isModerator = authResult.authorized;
    const isKnownModOrAdmin = await isModeratorOrAdmin(env, usernameLower);
    const canManageThumbnails = isModerator && isKnownModOrAdmin;
    const modCodeMismatch = isKnownModOrAdmin && !isModerator && !!authResult.invalidCode;
    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);
    const securityReject = rejectIfMalicious(
      buffer,
      "video/mp4",
      file.name || `${levelId}.mp4`
    );
    if (securityReject) return securityReject;
    if (isModerator && canManageThumbnails) {
      const codecReject = rejectIfNonCanonicalCodec(buffer);
      if (codecReject) return codecReject;
    }
    const versionManager = new VersionManager(env.SYSTEM_BUCKET);
    const version = Date.now().toString();
    let key;
    let uploadCategory;
    let isProfile = path === "profiles";
    if (isModerator && canManageThumbnails) {
      if (isProfile) {
        key = `profiles/${levelId}_${version}.mp4`;
        const prefixes = [`profiles/${levelId}.`, `profiles/${levelId}_`];
        const keysToDelete = [];
        for (const prefix of prefixes) {
          const list = await env.THUMBNAILS_BUCKET.list({ prefix });
          for (const obj of list.objects) keysToDelete.push(obj.key);
        }
        if (keysToDelete.length > 0) {
          await Promise.all(
            keysToDelete.map((k) => env.THUMBNAILS_BUCKET.delete(k))
          );
        }
      } else {
        key = `${path}/${levelId}_${version}.mp4`;
      }
      uploadCategory = isProfile ? "profile" : "live";
    } else {
      const ts = Date.now().toString();
      key = `pending_thumbnails/${levelId}_${ts}.mp4`;
      uploadCategory = "pending_thumbnail";
      const pendingList = await env.THUMBNAILS_BUCKET.list({
        prefix: `pending_thumbnails/${levelId}_`
      });
      if (pendingList.objects.length > 0) {
        await Promise.all(
          pendingList.objects.map((o) => env.THUMBNAILS_BUCKET.delete(o.key))
        );
      }
    }
    const existingVersion = await versionManager.getVersion(levelId);
    const isUpdate = !!existingVersion;
    await env.THUMBNAILS_BUCKET.put(key, buffer, {
      httpMetadata: {
        contentType: "video/mp4",
        cacheControl: "no-store, no-cache, must-revalidate, max-age=0"
      },
      customMetadata: {
        uploadedBy: username || "unknown",
        uploadedAt: (/* @__PURE__ */ new Date()).toISOString(),
        originalFormat: "mp4",
        isUpdate: isUpdate ? "true" : "false",
        version,
        accountID: accountID.toString(),
        moderatorUpload: isModerator ? "true" : "false",
        category: uploadCategory
      }
    });
    let appendedVersion = null;
    if (uploadCategory === "live" && !isProfile) {
      const appendRes = await versionManager.appendVersion(
        levelId,
        version,
        "mp4",
        path,
        "video",
        {
          uploadedBy: username || "unknown",
          uploadedAt: (/* @__PURE__ */ new Date()).toISOString()
        },
        MAX_THUMBNAILS_PER_LEVEL
      );
      appendedVersion = appendRes.appended;
      for (const removed of appendRes.removed) {
        const removedKey = buildThumbnailKey(levelId, removed);
        if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(removedKey));
        else await env.THUMBNAILS_BUCKET.delete(removedKey);
      }
      const updateLatest = /* @__PURE__ */ __name(async () => {
        const latestKey = "data/system/latest_uploads.json";
        let latest = await getR2Json(env.SYSTEM_BUCKET, latestKey) || [];
        latest = latest.filter((item) => item.levelId !== parseInt(levelId));
        const __lvlName = metaString(levelMetaParsed, "levelName", 100);
        const __lvlCreator = metaString(levelMetaParsed, "creatorName", 50);
        latest.unshift({
          levelId: parseInt(levelId),
          username: username || "unknown",
          timestamp: Date.now(),
          accountID,
          isVideo: true,
          ...(__lvlName ? { levelName: __lvlName } : {}),
          ...(__lvlCreator ? { creator: __lvlCreator } : {})
        });
        if (latest.length > 20) latest = latest.slice(0, 20);
        await putR2Json(env.SYSTEM_BUCKET, latestKey, latest);
        memCache.invalidate("latest_uploads");
      }, "updateLatest");
      if (ctx) ctx.waitUntil(updateLatest());
      else await updateLatest();
    }
    if (uploadCategory === "pending_thumbnail") {
      const queueKey = `data/queue/thumbnails/${levelId}.json`;
      const queueItem = {
        levelId: parseInt(levelId),
        accountID,
        submittedBy: username || "unknown",
        timestamp: Date.now(),
        status: "pending",
        category: "thumbnails",
        filename: key,
        format: "mp4"
      };
      if (ctx) ctx.waitUntil(putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem));
      else await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);
    }
    const isPendingThumbnail = uploadCategory === "pending_thumbnail";
    if (username && username !== "Unknown" && !isModerator) {
      if (ctx) ctx.waitUntil(incrementUploadQuota(env.SYSTEM_BUCKET, username));
      else await incrementUploadQuota(env.SYSTEM_BUCKET, username).catch(() => {});
    }
    if (levelMetaParsed && !isProfile) {
      const metaTask = storeLevelMeta(env, levelId, levelMetaParsed, {
        username,
        accountID,
        format: "mp4",
        category: uploadCategory
      });
      if (ctx) ctx.waitUntil(metaTask);
      else await metaTask.catch(() => {});
    }
    let responseMessage = "Video published directly";
    if (isPendingThumbnail) responseMessage = "Video thumbnail submitted for verification";
    else if (uploadCategory === "live") responseMessage = "Video published directly to global";
    const responseData = {
      success: true,
      message: responseMessage,
      key,
      isUpdate,
      moderatorUpload: isModerator,
      pendingVerification: isPendingThumbnail,
      cdnUrl: isPendingThumbnail ? null : `${new URL(request.url).origin}/t/${levelId}`
    };
    if (appendedVersion) {
      responseData.thumbnailId = appendedVersion.id;
      responseData.position = appendedVersion.position;
    }
    if (modCodeMismatch) {
      responseData.modCodeMismatch = true;
      responseData.alert = "Mod code no coincidente";
    }
    if (uploadCategory === "live" || uploadCategory === "profile") {
      invalidateThumbnail(request, levelId, env).catch(() => {
      });
      if (isProfile) memCache.invalidate(`prof_latest_${levelId}`);
      const origin = new URL(request.url).origin;
      const writeThroughCacheReq = cfCacheKey(
        new Request(`${origin}/t/${levelId}`)
      );
      const writeThroughResp = makeCacheable(
        new Response(buffer, {
          headers: {
            "Content-Type": "video/mp4",
            "Access-Control-Allow-Origin": "*"
          }
        }),
        604800
      );
      if (ctx) ctx.waitUntil(cfCachePut(writeThroughCacheReq, writeThroughResp));
      else cfCachePut(writeThroughCacheReq, writeThroughResp).catch(() => {
      });
    }
    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Upload video error:", error);
    return new Response(
      JSON.stringify({ error: "Upload failed", details: error.message }),
      {
        status: 500,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      }
    );
  }
}
__name(handleUploadVideo, "handleUploadVideo");
async function handleDownload(request, env, ctx) {
  const url2 = new URL(request.url);
  const pathParts = url2.pathname.split("/");
  const filename = pathParts[pathParts.length - 1];
  const levelId = filename.replace(/\.(webp|png|gif|mp4)$/, "");
  const requestedFormat = filename.endsWith(".png") ? "png" : filename.endsWith(".gif") ? "gif" : filename.endsWith(".mp4") ? "mp4" : "webp";
  const rawPath = url2.searchParams.get("path") || "/thumbnails";
  const path = rawPath.replace(/^\//, "");
  let foundKey = null;
  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const versions = await vm.getAllVersions(levelId);
    const versionData = versions.length > 0 ? versions[0] : null;
    if (versionData) {
      const storedFormat = versionData.format || "webp";
      const storedPath = versionData.path || "thumbnails";
      const isFormatCompatible = requestedFormat === storedFormat || requestedFormat === "mp4" && storedFormat === "mp4" || requestedFormat !== "gif" && requestedFormat !== "mp4" && storedFormat !== "gif" && storedFormat !== "mp4";
      if (isFormatCompatible) {
        let vStr = versionData.version;
        foundKey = vStr === "legacy" ? `${storedPath}/${levelId}.${storedFormat}` : `${storedPath}/${levelId}_${vStr}.${storedFormat}`;
        console.log(
          `[Download] Found via VersionManager (Lookup): ${foundKey}`
        );
      }
    }
  } catch (e) {
    console.warn("VersionManager lookup failed:", e);
  }
  if (!foundKey && levelId.includes("_")) foundKey = `${path}/${filename}`;
  if (!foundKey) {
    return new Response("Thumbnail not found", {
      status: 404,
      headers: corsHeaders()
    });
  }
  const object = await env.THUMBNAILS_BUCKET.get(foundKey, { skipMeta: true });
  if (!object) {
    return new Response("Thumbnail not found in storage", {
      status: 404,
      headers: corsHeaders()
    });
  }
  const headers = new Headers();
  object.writeHttpMetadata(headers);
  headers.set("Access-Control-Allow-Origin", "*");
  headers.set(
    "Cache-Control",
    "public, s-maxage=604800, max-age=604800, stale-while-revalidate=300"
  );
  headers.delete("Pragma");
  headers.delete("Expires");
  headers.delete("Vary");
  const cors = corsHeaders();
  for (const [k, v] of Object.entries(cors)) headers.set(k, v);
  return new Response(object.body, { status: 200, headers });
}
__name(handleDownload, "handleDownload");
async function handleDirectThumbnail(request, env, ctx) {
  const url2 = new URL(request.url);
  const pathParts = url2.pathname.split("/");
  const filename = pathParts[2];
  const hasExplicitExtension = /\.(webp|gif|png|mp4)$/.test(filename);
  const levelId = filename.replace(/\.(webp|gif|png|mp4)$/, "");
  const requestedFormat = filename.endsWith(".png") ? "png" : filename.endsWith(".gif") ? "gif" : filename.endsWith(".mp4") ? "mp4" : "webp";
  let bestMatch = null;
  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const map = await vm.getMap();
    const entry = map[levelId];
    if (entry) {
      const versions = normalizeList(entry);
      const versionData = versions.length > 0 ? versions[0] : null;
      if (versionData) {
        const storedFormat = versionData.format || "webp";
        const storedPath = versionData.path || "thumbnails";
        const vStr = versionData.version;
        const isFormatCompatible = !hasExplicitExtension || requestedFormat === storedFormat || requestedFormat === "mp4" && storedFormat === "mp4" || requestedFormat !== "gif" && requestedFormat !== "mp4" && storedFormat !== "gif" && storedFormat !== "mp4";
        if (isFormatCompatible) {
          bestMatch = vStr === "legacy" ? `${storedPath}/${levelId}.${storedFormat}` : `${storedPath}/${levelId}_${vStr}.${storedFormat}`;
        }
      }
    }
  } catch (e) {
    console.warn("[Direct] VersionManager error:", e);
  }
  // Fallback: when the manifest has no entry (or a stale one), try the legacy
  // filename pattern. This rescues thumbnails whose versions.json entry was
  // wiped by the older queue-accept overwrite bug but whose file is still in
  // storage. Without this, DailyNode/LevelCell binary loads would 404 even
  // though the asset exists.
  if (!bestMatch) {
    const tryFormat = requestedFormat === "png" ? "webp" : requestedFormat;
    bestMatch = `thumbnails/${levelId}.${tryFormat}`;
  }
  if (bestMatch) {
    const bunnyUrl = `${env.R2_PUBLIC_URL}/${bestMatch.replace(/^\//, "")}`;
    if (requestedFormat === "png" && !bestMatch.endsWith(".png") && !bestMatch.endsWith(".gif")) {
      try {
        const imageRes = await fetch(bunnyUrl, {
          headers: { Accept: "image/png", "Cache-Control": "no-cache" },
          cf: { image: { format: "png" } }
        });
        if (imageRes.ok) {
          const newHeaders = new Headers(imageRes.headers);
          newHeaders.set("Content-Type", "image/png");
          newHeaders.set("Access-Control-Allow-Origin", "*");
          newHeaders.set(
            "Cache-Control",
            "public, s-maxage=604800, max-age=604800, stale-while-revalidate=300"
          );
          newHeaders.delete("Pragma");
          newHeaders.delete("Expires");
          return new Response(imageRes.body, {
            status: 200,
            headers: newHeaders
          });
        }
      } catch (e) {
        console.error("Direct conversion failed", e);
      }
    }
    const object = await env.THUMBNAILS_BUCKET.get(bestMatch, {
      skipMeta: true,
      cfCacheTtl: 86400
    });
    if (object) {
      const headers = new Headers();
      object.writeHttpMetadata(headers);
      headers.set("etag", object.httpEtag);
      headers.set("Access-Control-Allow-Origin", "*");
      headers.set(
        "Cache-Control",
        "public, s-maxage=604800, max-age=604800, stale-while-revalidate=300"
      );
      headers.delete("Pragma");
      headers.delete("Expires");
      headers.delete("Vary");
      return new Response(object.body, { headers });
    }
  }
  return new Response(JSON.stringify({ error: "Thumbnail not found" }), {
    status: 404,
    headers: {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*"
    }
  });
}
__name(handleDirectThumbnail, "handleDirectThumbnail");
async function handleExists(request, env, ctx) {
  const url2 = new URL(request.url);
  const levelId = url2.searchParams.get("levelId");
  if (!levelId) {
    return new Response(JSON.stringify({ error: "Missing levelId" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  try {
    const memKey = `exists_${levelId}`;
    const cached = memCache.get(memKey);
    if (cached !== void 0) {
      return new Response(JSON.stringify(cached), {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const versionManager = new VersionManager(env.SYSTEM_BUCKET);
    const map = await versionManager.getMap();
    const exists = !!map[levelId];
    const result = { exists };
    memCache.set(memKey, result, 6e5);
    return new Response(JSON.stringify(result), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    if (error instanceof StorageError) {
      const cached = memCache.get(`exists_${levelId}`);
      return storageErrorResponse(error, cached, `exists_${levelId}`, corsHeaders());
    }
    console.error("[Thumbnails] Exists error:", error);
    return new Response(JSON.stringify({ error: "Internal error", code: "INTERNAL_ERROR" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleExists, "handleExists");
async function handleDeleteThumbnail(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { levelId, username, thumbnailId } = body;
    const accountID = parseInt(body.accountID || "0");
    if (!levelId || !username || !thumbnailId) {
      return new Response(
        JSON.stringify({ error: "Missing required fields" }),
        {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    const usernameLower = username.toLowerCase();
    const authResult = await verifyModAuth(
      request,
      env,
      usernameLower,
      accountID
    );
    if (!authResult.authorized) {
      return new Response(
        JSON.stringify({ error: "Not authorized - moderator only" }),
        {
          status: 403,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    const canManageThumbnails = await isModeratorOrAdmin(env, usernameLower);
    if (!canManageThumbnails) {
      return new Response(
        JSON.stringify({
          error: "Only moderators/admin can delete thumbnails"
        }),
        {
          status: 403,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    const versionManager = new VersionManager(env.SYSTEM_BUCKET);
    const currentVersions = await versionManager.getAllVersions(levelId);
    const removedIndex = currentVersions.findIndex(
      (v) => String(v.id) === String(thumbnailId)
    );
    if (removedIndex < 0) {
      return new Response(
        JSON.stringify({ error: "Thumbnail ID not found for this level" }),
        {
          status: 404,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    const deleteResult = await versionManager.deleteVersion(
      levelId,
      thumbnailId
    );
    if (!deleteResult.removed) {
      return new Response(
        JSON.stringify({ error: "Thumbnail ID not found for this level" }),
        {
          status: 404,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    const removedKey = buildThumbnailKey(levelId, deleteResult.removed);
    await env.THUMBNAILS_BUCKET.delete(removedKey);
    const nextIndex = Math.max(
      0,
      Math.min(Math.max(removedIndex - 1, 0), deleteResult.versions.length - 1)
    );
    const suggestedNext = deleteResult.versions[nextIndex] || null;
    invalidateThumbnail(request, levelId, env).catch(() => {
    });
    const deleteTimestamp = Date.now();
    const logKey = `data/logs/deleted/${levelId}-${deleteTimestamp}.json`;
    await putR2Json(env.SYSTEM_BUCKET, logKey, {
      levelId: parseInt(levelId),
      deletedBy: username,
      deletedThumbnailId: String(thumbnailId),
      deletedAt: (/* @__PURE__ */ new Date()).toISOString(),
      timestamp: deleteTimestamp
    });
    const deleteWebhookPayload = {
      levelId: parseInt(levelId),
      username,
      deletedBy: username,
      thumbnailId: String(thumbnailId),
      timestamp: deleteTimestamp,
      remaining: deleteResult.versions.length
    };
    if (ctx)
      ctx.waitUntil(dispatchWebhook(env, "delete", deleteWebhookPayload));
    else await dispatchWebhook(env, "delete", deleteWebhookPayload);
    return new Response(
      JSON.stringify({
        success: true,
        message: "Thumbnail deleted successfully",
        deletedThumbnailId: String(thumbnailId),
        remaining: deleteResult.versions.length,
        suggestedNextThumbnailId: suggestedNext ? suggestedNext.id : null,
        suggestedNextPosition: suggestedNext ? suggestedNext.position : null
      }),
      {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      }
    );
  } catch (error) {
    console.error("Delete thumbnail error:", error);
    return new Response(
      JSON.stringify({
        error: "Failed to delete thumbnail",
        details: error.message
      }),
      {
        status: 500,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      }
    );
  }
}
__name(handleDeleteThumbnail, "handleDeleteThumbnail");
async function handleListThumbnails(request, env) {
  const url2 = new URL(request.url);
  const levelId = url2.searchParams.get("levelId");
  if (!levelId) {
    return new Response(JSON.stringify({ error: "Level ID required" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  try {
    const memKey = `thumbnails_list_${levelId}`;
    // Fix: previously the cache was read into `cached` but never used —
    // every call hit Bunny even though the result was already in memory.
    const cached = memCache.get(memKey);
    if (cached !== void 0) {
      return new Response(JSON.stringify(cached), {
        headers: { "Content-Type": "application/json", "X-Cache-Mem": "HIT", ...corsHeaders() }
      });
    }
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const versions = await vm.getAllVersions(levelId);
    const enriched = await enrichVersionsWithFallback(env, levelId, versions);
    const origin = new URL(request.url).origin;
    const raw = buildThumbnailsPayload(levelId, env, enriched, origin);
    const seen = /* @__PURE__ */ new Set();
    const results = raw.filter((t) => {
      if (seen.has(t.url)) return false;
      seen.add(t.url);
      return true;
    });
    const payload = { thumbnails: results };
    memCache.set(memKey, payload, 3e5);
    return new Response(JSON.stringify(payload), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    if (error instanceof StorageError) {
      const cached = memCache.get(`thumbnails_list_${levelId}`);
      return storageErrorResponse(error, cached, `thumbnails_list_${levelId}`, corsHeaders());
    }
    console.error("[Thumbnails] List error:", error);
    return new Response(JSON.stringify({ error: "Internal error", code: "INTERNAL_ERROR" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleListThumbnails, "handleListThumbnails");

// ── /api/thumbnails/list-batch — List thumbnails (gallery metadata) for many levels in one request ──
//
// POST { ids: number[] }
//   → { thumbnails: { [id]: ThumbnailInfo[] } }
//
// Equivalent to calling GET /api/thumbnails/list?levelId=ID once per id, but
// coalesced into a single request. Uses the same memCache entries as the
// single endpoint so cache stays warm across both call sites.
async function handleListThumbnailsBatch(request, env) {
  let body;
  try {
    body = await request.json();
  } catch (e) {
    return new Response(JSON.stringify({ error: "Invalid JSON body" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }

  const rawIds = Array.isArray(body && body.ids) ? body.ids : [];
  if (rawIds.length === 0) {
    return new Response(JSON.stringify({ thumbnails: {} }), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }

  // Normalize, dedupe, cap. Same shape as /api/thumbnails/batch.
  const seen = /* @__PURE__ */ new Set();
  const ids = [];
  for (const v of rawIds) {
    const n = parseInt(v);
    if (!Number.isFinite(n) || n <= 0) continue;
    if (seen.has(n)) continue;
    seen.add(n);
    ids.push(n);
    if (ids.length >= 40) break;
  }
  if (ids.length === 0) {
    return new Response(JSON.stringify({ thumbnails: {} }), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }

  const origin = new URL(request.url).origin;

  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const result = {};

    // Resolve each id with bounded concurrency to stay under the Workers
    // subrequest budget while still being much faster than serial.
    await batchMapWithConcurrency(ids, 8, async (id) => {
      const memKey = `thumbnails_list_${id}`;
      const cached = memCache.get(memKey);
      if (cached && Array.isArray(cached.thumbnails)) {
        result[id] = cached.thumbnails;
        return { id };
      }
      try {
        const versions = await vm.getAllVersions(id);
        const enriched = await enrichVersionsWithFallback(env, id, versions);
        const raw = buildThumbnailsPayload(id, env, enriched, origin);
        const dedup = /* @__PURE__ */ new Set();
        const filtered = raw.filter((t) => {
          if (dedup.has(t.url)) return false;
          dedup.add(t.url);
          return true;
        });
        memCache.set(memKey, { thumbnails: filtered }, 3e5);
        result[id] = filtered;
        return { id };
      } catch (e) {
        // Per-id failures fall back to an empty list rather than failing the
        // whole batch. Matches the shape the client expects (empty gallery).
        result[id] = [];
        return { id, __error: e && e.message };
      }
    });

    return new Response(JSON.stringify({ thumbnails: result }), {
      headers: {
        "Content-Type": "application/json",
        "Cache-Control": "private, max-age=60",
        ...corsHeaders()
      }
    });
  } catch (error) {
    console.error("[ListBatchThumbnails] error:", error);
    return new Response(JSON.stringify({ error: "Internal error", code: "INTERNAL_ERROR" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleListThumbnailsBatch, "handleListThumbnailsBatch");

async function handleGetThumbnailInfo(request, env) {
  const url2 = new URL(request.url);
  const levelId = url2.searchParams.get("levelId");
  if (!levelId) {
    return new Response(JSON.stringify({ error: "Level ID required" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const versions = await vm.getAllVersions(levelId);
    const versionData = versions.length > 0 ? versions[0] : null;
    if (!versionData) {
      return new Response(JSON.stringify({ error: "Thumbnail not found" }), {
        status: 404,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const storedFormat = versionData.format || "webp";
    const storedPath = versionData.path || "thumbnails";
    let key = versionData.version === "legacy" ? `${storedPath}/${levelId}.${storedFormat}` : `${storedPath}/${levelId}_${versionData.version}.${storedFormat}`;
    const head = await env.THUMBNAILS_BUCKET.head(key, { skipMeta: true });
    let metadata = head ? head.customMetadata || {} : {};
    if (versionData.uploadedBy) metadata.uploadedBy = versionData.uploadedBy;
    if (versionData.uploadedAt && !metadata.uploadedAt)
      metadata.uploadedAt = versionData.uploadedAt;
    if (!metadata.uploadedBy && metadata.originalSubmitter)
      metadata.uploadedBy = metadata.originalSubmitter;
    if (!metadata.uploadedBy || metadata.uploadedBy === "unknown" || metadata.uploadedBy === "Unknown") {
      try {
        const ratingData = await getR2Json(
          env.SYSTEM_BUCKET,
          `ratings/${levelId}.json`
        );
        if (ratingData && ratingData.uploadedBy && ratingData.uploadedBy !== "Unknown") {
          metadata.uploadedBy = ratingData.uploadedBy;
          metadata.source = "ratings_fallback";
        }
      } catch (e) {
        console.warn("Metadata fallback failed (ratings):", e);
      }
    }
    if (!metadata.uploadedBy || metadata.uploadedBy === "unknown" || metadata.uploadedBy === "Unknown") {
      try {
        const latestData = await getR2Json(
          env.SYSTEM_BUCKET,
          "data/system/latest_uploads.json"
        );
        if (latestData && Array.isArray(latestData)) {
          const entry = latestData.find(
            (item) => item.levelId === parseInt(levelId)
          );
          if (entry && entry.username) {
            metadata.uploadedBy = entry.username;
            metadata.source = "latest_uploads_fallback";
          }
        }
      } catch (e) {
        console.warn("Metadata fallback failed (latest):", e);
      }
    }
    if (!metadata.uploadedAt && head && head.uploaded) {
      try {
        metadata.uploadedAt = head.uploaded.toISOString();
      } catch (e) {
      }
    }
    if (!head) {
      return new Response(
        JSON.stringify({
          error: "File metadata not found",
          versionData,
          recoveredMetadata: metadata
        }),
        {
          status: 404,
          headers: { "Content-Type": "application/json", ...corsHeaders() }
        }
      );
    }
    const infoOrigin = new URL(request.url).origin;
    const enrichedInfoVersions = await enrichVersionsWithFallback(env, levelId, versions);
    const rawThumbs = buildThumbnailsPayload(levelId, env, enrichedInfoVersions, infoOrigin);
    const seenUrls = /* @__PURE__ */ new Set();
    const dedupedThumbs = rawThumbs.filter((t) => {
      if (seenUrls.has(t.url)) return false;
      seenUrls.add(t.url);
      return true;
    });
    return new Response(
      JSON.stringify({
        success: true,
        levelId,
        url: `${infoOrigin}/t/${levelId}`,
        version: versionData,
        metadata,
        thumbnails: dedupedThumbs,
        fileInfo: {
          size: head.size,
          uploadedAt: head.uploaded,
          contentType: head.httpMetadata?.contentType
        }
      }),
      {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      }
    );
  } catch (error) {
    console.error("Get info error:", error);
    return new Response(
      JSON.stringify({ error: "Failed to get info", details: error.message }),
      {
        status: 500,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      }
    );
  }
}
__name(handleGetThumbnailInfo, "handleGetThumbnailInfo");
async function handleManifest(request, env) {
  const url2 = new URL(request.url);
  const idsParam = url2.searchParams.get("ids") || "";
  const levelIds = idsParam.split(",").map((s) => s.trim()).filter(Boolean);
  if (levelIds.length === 0) {
    return new Response(JSON.stringify({}), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const cappedIds = levelIds.slice(0, 200);
  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const fullMap = await vm.getMap();
    // Routing decision:
    //  • Worker has quota left  → cdnUrl points back to this Worker (`/t/{id}.{fmt}`).
    //    The Worker proxies from Bunny Storage using the AccessKey ("bunny por
    //    contraseña"), so the client never hits the paid CDN Pull Zone.
    //  • Direct mode (quota near limit) → cdnUrl points at the CDN Pull Zone.
    //    Each request now bypasses the Worker entirely (counts as paid Bunny
    //    CDN bandwidth, but doesn't burn Worker requests).
    const useDirectCdn = isDirectModeActive() && !!env.CDN_PULL_ZONE_URL;
    const requestOrigin = new URL(request.url).origin;
    const storageBase = useDirectCdn
      ? `${env.CDN_PULL_ZONE_URL}/thumbnails`
      : `${requestOrigin}/_storage`;
    const result = {};
    for (const id of cappedIds) {
      const entry = fullMap[id];
      if (!entry) continue;
      let latest;
      let versionCount = 1;
      if (Array.isArray(entry)) {
        const normalized = entry.map((v, i) => normalizeManifestEntry(v, i)).filter(Boolean).sort((a, b) => (a.position || 0) - (b.position || 0));
        if (normalized.length === 0) continue;
        latest = normalized[0];
        versionCount = normalized.length;
      } else {
        latest = normalizeManifestEntry(entry, 0);
        if (!latest) continue;
      }
      const ver = latest.version;
      const fmt = latest.format || "webp";
      const storedPath = latest.path || "thumbnails";
      const bunnyKey = ver === "legacy" ? `${storedPath}/${id}.${fmt}` : `${storedPath}/${id}_${ver}.${fmt}`;
      // Worker route uses the existing /t/{id}.{fmt} endpoint (handleDirectThumbnail
      // already resolves the bunnyKey on the server side, so we don't need to
      // expose the version token in the URL when going via Worker).
      const cdnUrl = useDirectCdn
        ? `${storageBase}/${bunnyKey}`
        : `${requestOrigin}/t/${id}.${fmt}`;
      const revisionToken = `${versionCount}:${latest.id || "legacy"}:${ver}:${fmt}`;
      result[id] = {
        format: fmt,
        type: latest.type || (fmt === "gif" ? "gif" : fmt === "mp4" ? "video" : "static"),
        version: ver,
        id: latest.id || "legacy",
        cdnUrl,
        revisionToken
      };
    }
    const responsePayload = { ...result };
    // Always advertise the CDN base URL so the client can fall back to it
    // if the Worker becomes unreachable mid-session.
    if (env.CDN_PULL_ZONE_URL) {
      responsePayload._cdnBaseUrl = env.CDN_PULL_ZONE_URL;
    }
    // Surface the routing mode for diagnostics / future client behaviour.
    responsePayload._routing = useDirectCdn ? "cdn" : "worker";
    return new Response(JSON.stringify(responsePayload), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    if (error instanceof StorageError) {
      const cached = memCache.get("manifest_map");
      if (cached !== void 0) {
        console.warn("[Degradation] Serving cached manifest data");
        return new Response(JSON.stringify({ ...cached, _degraded: true }), {
          headers: { "Content-Type": "application/json", ...corsHeaders() }
        });
      }
      return new Response(JSON.stringify({ error: "Storage temporarily unavailable", code: "STORAGE_ERROR", retryable: true }), {
        status: 502,
        headers: { "Content-Type": "application/json", "Retry-After": "5", ...corsHeaders() }
      });
    }
    console.error("[Thumbnails] Manifest error:", error);
    return new Response(JSON.stringify({ error: "Internal error", code: "INTERNAL_ERROR" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleManifest, "handleManifest");

// ── Batch download helpers ──────────────────────────────────────────
// Uint8Array → base64 in safe chunks (avoid stack overflow on large buffers)
function uint8ToBase64(bytes) {
  const CHUNK = 0x8000;
  let binary = "";
  for (let i = 0; i < bytes.length; i += CHUNK) {
    const chunk = bytes.subarray(i, Math.min(i + CHUNK, bytes.length));
    binary += String.fromCharCode.apply(null, chunk);
  }
  return btoa(binary);
}
__name(uint8ToBase64, "uint8ToBase64");

// Limit concurrency for bunny.get() calls within a batch (Workers subrequest limit = 50).
async function batchMapWithConcurrency(items, concurrency, fn) {
  const results = new Array(items.length);
  let cursor = 0;
  const workers = new Array(Math.min(concurrency, items.length)).fill(0).map(async () => {
    while (true) {
      const idx = cursor++;
      if (idx >= items.length) return;
      try {
        results[idx] = await fn(items[idx], idx);
      } catch (e) {
        results[idx] = { __error: e && e.message ? e.message : String(e) };
      }
    }
  });
  await Promise.all(workers);
  return results;
}
__name(batchMapWithConcurrency, "batchMapWithConcurrency");

// ── /api/thumbnails/batch — Download level thumbnails in one request ──
// POST { ids: number[] } → { items: { [id]: { ok, format?, data?(base64), reason? } } }
async function handleBatchThumbnails(request, env) {
  let body;
  try {
    body = await request.json();
  } catch (e) {
    return new Response(JSON.stringify({ error: "Invalid JSON body" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const rawIds = Array.isArray(body && body.ids) ? body.ids : [];
  if (rawIds.length === 0) {
    return new Response(JSON.stringify({ items: {} }), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  // Normalize, dedupe, cap.
  const seen = /* @__PURE__ */ new Set();
  const ids = [];
  for (const v of rawIds) {
    const n = parseInt(v);
    if (!Number.isFinite(n) || n <= 0) continue;
    if (seen.has(n)) continue;
    seen.add(n);
    ids.push(n);
    if (ids.length >= 40) break; // server cap (Workers subrequest budget + payload)
  }
  if (ids.length === 0) {
    return new Response(JSON.stringify({ items: {} }), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  try {
    // Resolve manifest once for the whole batch.
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const fullMap = await vm.getMap();
    const tasks = ids.map((id) => {
      const entry = fullMap[id];
      if (!entry) return { id, key: null };
      const versions = Array.isArray(entry)
        ? entry.map((v, i) => normalizeManifestEntry(v, i)).filter(Boolean).sort((a, b) => (a.position || 0) - (b.position || 0))
        : [normalizeManifestEntry(entry, 0)].filter(Boolean);
      const latest = versions.length > 0 ? versions[0] : null;
      if (!latest) return { id, key: null };
      const fmt = latest.format || "webp";
      const storedPath = latest.path || "thumbnails";
      const ver = latest.version;
      const key = ver === "legacy" ? `${storedPath}/${id}.${fmt}` : `${storedPath}/${id}_${ver}.${fmt}`;
      return { id, key, format: fmt };
    });
    const results = await batchMapWithConcurrency(tasks, 8, async (task) => {
      if (!task.key) {
        // No version entry. Try legacy filename as a last resort so levels
        // whose versions.json was lost still serve a thumbnail when the file
        // exists in storage. Covers the common case after the queue-accept
        // overwrite bug — files survived even when the manifest didn't.
        const legacyKey = `thumbnails/${task.id}.webp`;
        try {
          const legacy = await env.THUMBNAILS_BUCKET.get(legacyKey, { skipMeta: true, cfCacheTtl: 86400 });
          if (legacy) {
            const buf = await legacy.arrayBuffer();
            return { id: task.id, ok: true, format: "webp", data: uint8ToBase64(new Uint8Array(buf)) };
          }
        } catch (_) {
          // ignore — fall through to not_found
        }
        return { id: task.id, ok: false, reason: "not_found" };
      }
      try {
        let obj = await env.THUMBNAILS_BUCKET.get(task.key, { skipMeta: true, cfCacheTtl: 86400 });
        // Manifest pointed at a key that's missing from storage. Fall back to
        // the legacy filename so the request can still succeed if the file
        // exists under the older naming scheme.
        if (!obj && task.key !== `thumbnails/${task.id}.${task.format || "webp"}`) {
          const legacyKey = `thumbnails/${task.id}.${task.format || "webp"}`;
          try {
            obj = await env.THUMBNAILS_BUCKET.get(legacyKey, { skipMeta: true, cfCacheTtl: 86400 });
          } catch (_) {
            // ignore — keep obj as null
          }
        }
        if (!obj) return { id: task.id, ok: false, reason: "not_found" };
        const buf = await obj.arrayBuffer();
        return { id: task.id, ok: true, format: task.format, data: uint8ToBase64(new Uint8Array(buf)) };
      } catch (e) {
        return { id: task.id, ok: false, reason: "storage_error" };
      }
    });
    const items = {};
    // results from batchMapWithConcurrency are positional. We rely on the
    // task.id captured inside the worker function to map back to ids — for
    // unhandled exceptions (rare), batchMapWithConcurrency returns an
    // {__error} placeholder without an id, so we look up the matching id by
    // index to still surface a structured 'storage_error' to the client.
    for (let i = 0; i < results.length; i++) {
      const r = results[i];
      const t = tasks[i];
      if (!r) {
        if (t && t.id) items[t.id] = { ok: false, reason: "missing_result" };
        continue;
      }
      if (r.__error) {
        if (t && t.id) items[t.id] = { ok: false, reason: "storage_error" };
        continue;
      }
      items[r.id] = r.ok
        ? { ok: true, format: r.format, data: r.data }
        : { ok: false, reason: r.reason || "unknown" };
    }
    return new Response(JSON.stringify({ items }), {
      headers: {
        "Content-Type": "application/json",
        "Cache-Control": "private, max-age=60",
        ...corsHeaders()
      }
    });
  } catch (error) {
    console.error("[BatchThumbnails] error:", error);
    return new Response(JSON.stringify({ error: "Internal error", code: "INTERNAL_ERROR" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleBatchThumbnails, "handleBatchThumbnails");

// ── /api/profilebackground/batch — Download profile backgrounds in one request ──
// POST { accountIDs: number[] } → { items: { [id]: { ok, format?, data?(base64), reason? } } }
async function handleBatchProfileBackgrounds(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  let body;
  try {
    body = await request.json();
  } catch (e) {
    return new Response(JSON.stringify({ error: "Invalid JSON body" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const rawIds = Array.isArray(body && body.accountIDs) ? body.accountIDs : [];
  if (rawIds.length === 0) {
    return new Response(JSON.stringify({ items: {} }), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const seen = /* @__PURE__ */ new Set();
  const ids = [];
  for (const v of rawIds) {
    const n = parseInt(v);
    if (!Number.isFinite(n) || n <= 0) continue;
    if (seen.has(n)) continue;
    seen.add(n);
    ids.push(n);
    if (ids.length >= 40) break;
  }
  if (ids.length === 0) {
    return new Response(JSON.stringify({ items: {} }), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  try {
    const results = await batchMapWithConcurrency(ids, 8, async (accountId) => {
      // Resolve canonical key (latest), then fall back to legacy.
      let foundKey = await resolveLatestKey(
        env.THUMBNAILS_BUCKET,
        `profilebackground/${accountId}_`,
        `bg_latest_${accountId}`
      );
      if (!foundKey) {
        foundKey = await resolveLatestKey(
          env.THUMBNAILS_BUCKET,
          `backgrounds/${accountId}_`,
          `bg_legacy_${accountId}`
        );
      }
      if (!foundKey) return { id: accountId, ok: false, reason: "not_found" };
      try {
        const obj = await env.THUMBNAILS_BUCKET.get(foundKey, { skipMeta: true, cfCacheTtl: 86400 });
        if (!obj) return { id: accountId, ok: false, reason: "not_found" };
        const buf = await obj.arrayBuffer();
        const ctype = obj.httpMetadata && obj.httpMetadata.contentType ? obj.httpMetadata.contentType : "";
        const fmt = ctype.includes("gif") ? "gif"
          : ctype.includes("png") ? "png"
          : ctype.includes("mp4") ? "mp4"
          : "webp";
        return { id: accountId, ok: true, format: fmt, data: uint8ToBase64(new Uint8Array(buf)) };
      } catch (e) {
        return { id: accountId, ok: false, reason: "storage_error" };
      }
    });
    const items = {};
    for (const r of results) {
      if (!r) continue;
      if (r.__error) continue;
      items[r.id] = r.ok
        ? { ok: true, format: r.format, data: r.data }
        : { ok: false, reason: r.reason || "unknown" };
    }
    return new Response(JSON.stringify({ items }), {
      headers: {
        "Content-Type": "application/json",
        "Cache-Control": "private, max-age=60",
        ...corsHeaders()
      }
    });
  } catch (error) {
    console.error("[BatchProfileBackgrounds] error:", error);
    return new Response(JSON.stringify({ error: "Internal error", code: "INTERNAL_ERROR" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleBatchProfileBackgrounds, "handleBatchProfileBackgrounds");

// ── /api/profileimgs/batch — Download profile images (avatars) in one request ──
// POST { accountIDs: number[] } → { items: { [id]: { ok, format?, data?(base64), reason? } } }
async function handleBatchProfileImgs(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  let body;
  try {
    body = await request.json();
  } catch (e) {
    return new Response(JSON.stringify({ error: "Invalid JSON body" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const rawIds = Array.isArray(body && body.accountIDs) ? body.accountIDs : [];
  if (rawIds.length === 0) {
    return new Response(JSON.stringify({ items: {} }), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const seen = /* @__PURE__ */ new Set();
  const ids = [];
  for (const v of rawIds) {
    const n = parseInt(v);
    if (!Number.isFinite(n) || n <= 0) continue;
    if (seen.has(n)) continue;
    seen.add(n);
    ids.push(n);
    if (ids.length >= 40) break;
  }
  if (ids.length === 0) {
    return new Response(JSON.stringify({ items: {} }), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  try {
    const results = await batchMapWithConcurrency(ids, 8, async (accountId) => {
      let foundKey = await resolveLatestKey(
        env.THUMBNAILS_BUCKET,
        `profileimgs/${accountId}_`,
        `pimg_latest_${accountId}`
      );
      if (!foundKey) {
        // Legacy: profileimgs/{id}.{ext}
        const exts = ["gif", "webp", "png", "jpg", "jpeg", "bmp", "tiff"];
        for (const ext of exts) {
          const k = `profileimgs/${accountId}.${ext}`;
          const obj = await env.THUMBNAILS_BUCKET.head(k, { skipMeta: true, cfCacheTtl: 86400 });
          if (obj) { foundKey = k; break; }
        }
      }
      if (!foundKey) return { id: accountId, ok: false, reason: "not_found" };
      try {
        const obj = await env.THUMBNAILS_BUCKET.get(foundKey, { skipMeta: true, cfCacheTtl: 86400 });
        if (!obj) return { id: accountId, ok: false, reason: "not_found" };
        const buf = await obj.arrayBuffer();
        const ctype = obj.httpMetadata && obj.httpMetadata.contentType ? obj.httpMetadata.contentType : "";
        const fmt = ctype.includes("gif") ? "gif"
          : ctype.includes("png") ? "png"
          : ctype.includes("jpeg") ? "jpg"
          : "webp";
        return { id: accountId, ok: true, format: fmt, data: uint8ToBase64(new Uint8Array(buf)) };
      } catch (e) {
        return { id: accountId, ok: false, reason: "storage_error" };
      }
    });
    const items = {};
    for (const r of results) {
      if (!r) continue;
      if (r.__error) continue;
      items[r.id] = r.ok
        ? { ok: true, format: r.format, data: r.data }
        : { ok: false, reason: r.reason || "unknown" };
    }
    return new Response(JSON.stringify({ items }), {
      headers: {
        "Content-Type": "application/json",
        "Cache-Control": "private, max-age=60",
        ...corsHeaders()
      }
    });
  } catch (error) {
    console.error("[BatchProfileImgs] error:", error);
    return new Response(JSON.stringify({ error: "Internal error", code: "INTERNAL_ERROR" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleBatchProfileImgs, "handleBatchProfileImgs");

function normalizeManifestEntry(entry, index) {
  if (!entry) return null;
  if (typeof entry === "string") {
    return { id: "legacy", version: entry, format: "webp", path: "thumbnails" };
  }
  return {
    id: entry.id || `${index + 1}`,
    position: typeof entry.position === "number" ? entry.position : index + 1,
    version: entry.version,
    format: entry.format || "webp",
    type: entry.type || "static",
    path: (entry.path || "thumbnails").replace(/^\//, "")
  };
}
__name(normalizeManifestEntry, "normalizeManifestEntry");

// src/services/quotas.js
var QUOTA_TTL = 5 * 6e4;
var DAILY_LIMITS = {
  admin: Infinity,
  moderator: Infinity,
  vip: 100,
  default: 40
};
function todayKey() {
  return (/* @__PURE__ */ new Date()).toISOString().split("T")[0];
}
__name(todayKey, "todayKey");
async function checkUploadQuota(sysBucket, username, role = "default") {
  const limit2 = DAILY_LIMITS[role] ?? DAILY_LIMITS.default;
  if (!isFinite(limit2)) return { allowed: true, remaining: Infinity, limit: limit2, used: 0 };
  const user = username.toLowerCase().trim();
  const memKey = `quota:${user}`;
  let quota = memCache.get(memKey);
  if (!quota) {
    quota = await getR2Json(sysBucket, `data/quotas/${user}.json`) || {};
    memCache.set(memKey, quota, QUOTA_TTL);
  }
  const day = todayKey();
  const used = quota[day] || 0;
  const remaining = Math.max(0, limit2 - used);
  return { allowed: remaining > 0, remaining, limit: limit2, used };
}
__name(checkUploadQuota, "checkUploadQuota");
async function incrementUploadQuota(sysBucket, username) {
  const user = username.toLowerCase().trim();
  const key = `data/quotas/${user}.json`;
  let quota = await getR2Json(sysBucket, key) || {};
  const day = todayKey();
  quota[day] = (quota[day] || 0) + 1;
  const cutoff = /* @__PURE__ */ new Date();
  cutoff.setDate(cutoff.getDate() - 7);
  const cutoffStr = cutoff.toISOString().split("T")[0];
  for (const k of Object.keys(quota)) {
    if (k < cutoffStr) delete quota[k];
  }
  await putR2Json(sysBucket, key, quota);
  memCache.set(`quota:${user}`, quota, QUOTA_TTL);
}
__name(incrementUploadQuota, "incrementUploadQuota");

// src/controllers/suggestions.js
async function handleUploadSuggestion(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const formData = await request.formData();
    const file = formData.get("image");
    const levelId = formData.get("levelId");
    const username = formData.get("username") || "Unknown";
    const accountID = parseInt(formData.get("accountID") || "0");
    if (!file || !isValidLevelID(levelId)) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const banData = await getR2Json(env.SYSTEM_BUCKET, "data/banlist.json");
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (username !== "Unknown" && banned.includes(username.toLowerCase())) {
      return new Response(JSON.stringify({ error: "User is banned" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (accountID > 0) {
      const accountVerification = await verifyAccountForWrite(env, accountID, String(username));
      if (!accountVerification.valid) {
        return new Response(JSON.stringify({ error: "Account verification failed", code: accountVerification.reason }), {
          status: 403,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      }
    } else {
      console.log(`Unauthenticated suggestion upload (accountID=0) by '${username}' for level ${levelId}`);
    }
    if (username !== "Unknown") {
      const role = await getUserRole(username.toLowerCase(), env.SYSTEM_BUCKET);
      const quota = await checkUploadQuota(env.SYSTEM_BUCKET, username, role);
      if (!quota.allowed) {
        return new Response(JSON.stringify({
          error: "Daily upload quota exceeded",
          limit: quota.limit,
          used: quota.used,
          message: `You have reached your daily upload limit (${quota.limit}). Try again tomorrow.`
        }), { status: 429, headers: { "Content-Type": "application/json", ...corsNoStore() } });
      }
    }
    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: "File too large" }), {
        status: 413,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);
    const securityReject = rejectIfMalicious(buffer, file.type || "image/webp", file.name || `suggestion_${levelId}.webp`);
    if (securityReject) return securityReject;
    const dims = getImageDimensions(buffer);
    if (!dims) {
      return new Response(JSON.stringify({ error: "Invalid or unsupported image format" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const queueKey = `data/queue/suggestions/${levelId}.json`;
    let queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);
    let suggestions = [];
    if (queueData) {
      if (Array.isArray(queueData)) {
        suggestions = queueData;
      } else {
        if (!queueData.filename) queueData.filename = `suggestions/${levelId}.webp`;
        suggestions = [queueData];
      }
    }
    if (suggestions.length >= 10) {
      return new Response(JSON.stringify({ error: "Suggestion limit reached for this level (max 10)" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const uniqueId = Math.random().toString(36).substring(7);
    const timestamp = Date.now();
    const filename = `suggestions/${levelId}_${timestamp}_${uniqueId}.webp`;
    await env.THUMBNAILS_BUCKET.put(filename, buffer, {
      httpMetadata: { contentType: "image/webp", cacheControl: NO_STORE_CACHE_CONTROL },
      customMetadata: { uploadedBy: username, uploadedAt: (/* @__PURE__ */ new Date()).toISOString(), category: "suggestion" }
    });
    suggestions.push({
      levelId: parseInt(levelId),
      category: "verify",
      submittedBy: username,
      timestamp,
      status: "pending",
      note: "User suggestion",
      accountID,
      unauthenticated: accountID === 0,
      filename
    });
    await putR2Json(env.SYSTEM_BUCKET, queueKey, suggestions);
    invalidateQueue(request, "verify", env).catch(() => {
    });
    if (username !== "Unknown") {
      if (ctx && ctx.waitUntil) ctx.waitUntil(incrementUploadQuota(env.SYSTEM_BUCKET, username));
      else await incrementUploadQuota(env.SYSTEM_BUCKET, username).catch(() => {
      });
    }
    return new Response(JSON.stringify({ success: true, message: "Suggestion uploaded successfully" }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Suggestion upload error:", error);
    return new Response(JSON.stringify({ error: "Upload failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleUploadSuggestion, "handleUploadSuggestion");
async function handleUploadUpdate(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const formData = await request.formData();
    const file = formData.get("image");
    const levelId = formData.get("levelId");
    const username = formData.get("username") || "Unknown";
    const accountID = parseInt(formData.get("accountID") || "0");
    if (!file || !isValidLevelID(levelId)) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const banData = await getR2Json(env.SYSTEM_BUCKET, "data/banlist.json");
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (username !== "Unknown" && banned.includes(username.toLowerCase())) {
      return new Response(JSON.stringify({ error: "User is banned" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (accountID > 0) {
      const accountVerification = await verifyAccountForWrite(env, accountID, String(username));
      if (!accountVerification.valid) {
        return new Response(JSON.stringify({ error: "Account verification failed", code: accountVerification.reason }), {
          status: 403,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      }
    }
    if (username !== "Unknown") {
      const role = await getUserRole(username.toLowerCase(), env.SYSTEM_BUCKET);
      const quota = await checkUploadQuota(env.SYSTEM_BUCKET, username, role);
      if (!quota.allowed) {
        return new Response(JSON.stringify({
          error: "Daily upload quota exceeded",
          limit: quota.limit,
          used: quota.used,
          message: `You have reached your daily upload limit (${quota.limit}). Try again tomorrow.`
        }), { status: 429, headers: { "Content-Type": "application/json", ...corsNoStore() } });
      }
    }
    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: "File too large" }), {
        status: 413,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);
    const dims = getImageDimensions(buffer);
    if (!dims) {
      return new Response(JSON.stringify({ error: "Invalid or unsupported image format" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const detectedType = dims.type;
    const extMap = { png: "png", webp: "webp", jpeg: "jpg", gif: "gif" };
    const mimeMap = { png: "image/png", webp: "image/webp", jpeg: "image/jpeg", gif: "image/gif" };
    const ext = extMap[detectedType] || "webp";
    const mime = mimeMap[detectedType] || "image/webp";
    const securityReject = rejectIfMalicious(buffer, mime, file.name || `update_${levelId}.${ext}`);
    if (securityReject) return securityReject;
    const queueKey = `data/queue/updates/${levelId}.json`;
    let queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);
    let updates = [];
    if (queueData) {
      if (Array.isArray(queueData)) {
        updates = queueData;
      } else {
        updates = [queueData];
      }
    }
    if (updates.length >= 10) {
      return new Response(JSON.stringify({ error: "Update limit reached for this level (max 10)" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const uniqueId = Math.random().toString(36).substring(7);
    const timestamp = Date.now();
    const key = `updates/${levelId}_${timestamp}_${uniqueId}.${ext}`;
    await env.THUMBNAILS_BUCKET.put(key, buffer, {
      httpMetadata: { contentType: mime, cacheControl: NO_STORE_CACHE_CONTROL },
      customMetadata: {
        uploadedBy: username,
        uploadedAt: (/* @__PURE__ */ new Date()).toISOString(),
        category: "update",
        accountID: accountID.toString(),
        unauthenticated: accountID === 0 ? "true" : "false"
      }
    });
    updates.push({
      levelId: parseInt(levelId),
      category: "update",
      submittedBy: username,
      timestamp,
      status: "pending",
      note: "Update proposal",
      accountID,
      unauthenticated: accountID === 0,
      filename: key
    });
    await putR2Json(env.SYSTEM_BUCKET, queueKey, updates);
    invalidateQueue(request, "update", env).catch(() => {
    });
    if (username !== "Unknown") {
      if (ctx && ctx.waitUntil) ctx.waitUntil(incrementUploadQuota(env.SYSTEM_BUCKET, username));
      else await incrementUploadQuota(env.SYSTEM_BUCKET, username).catch(() => {
      });
    }
    return new Response(JSON.stringify({ success: true, message: "Update uploaded successfully" }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Update upload error:", error);
    return new Response(JSON.stringify({ error: "Upload failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleUploadUpdate, "handleUploadUpdate");
async function handleDownloadSuggestion(request, env) {
  const url2 = new URL(request.url);
  const pathParts = url2.pathname.split("/");
  const filename = pathParts[2];
  const levelId = filename.replace(/\.webp$/, "");
  try {
    let object = await env.THUMBNAILS_BUCKET.get(`suggestions/${filename}`, { skipMeta: true });
    if (!object) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: `suggestions/${levelId}`, limit: 20 });
      if (list.objects.length > 0) {
        const sorted2 = list.objects.sort((a, b) => {
          const getTs = /* @__PURE__ */ __name((k) => {
            const m = k.key.match(/_(\d+)_/);
            return m ? parseInt(m[1]) : 0;
          }, "getTs");
          return getTs(b) - getTs(a);
        });
        object = await env.THUMBNAILS_BUCKET.get(sorted2[0].key, { skipMeta: true });
      }
    }
    if (!object) object = await env.THUMBNAILS_BUCKET.get(`suggestions/${levelId}.webp`, { skipMeta: true });
    if (!object) {
      return new Response(JSON.stringify({ error: "Suggestion not found" }), {
        status: 404,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const buf = await object.arrayBuffer();
    const respHeaders = new Headers();
    object.writeHttpMetadata(respHeaders);
    respHeaders.set("Access-Control-Allow-Origin", "*");
    respHeaders.set("Cache-Control", "public, s-maxage=604800, max-age=604800, stale-while-revalidate=300");
    respHeaders.delete("Pragma");
    respHeaders.delete("Expires");
    if (!respHeaders.has("Content-Type")) respHeaders.set("Content-Type", "image/webp");
    return new Response(buf, { headers: respHeaders });
  } catch (error) {
    console.error("Download suggestion error:", error);
    return new Response(JSON.stringify({ error: "Download failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleDownloadSuggestion, "handleDownloadSuggestion");
async function handleDownloadUpdate(request, env) {
  const url2 = new URL(request.url);
  const pathParts = url2.pathname.split("/");
  const filename = pathParts[2];
  const levelId = filename.replace(/\.webp$/, "");
  try {
    let object = await env.THUMBNAILS_BUCKET.get(`updates/${filename}`, { skipMeta: true });
    if (!object) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: `updates/${levelId}`, limit: 20 });
      if (list.objects.length > 0) {
        const sorted2 = list.objects.sort((a, b) => {
          const getTs = /* @__PURE__ */ __name((k) => {
            const m = k.key.match(/_(\d+)\./);
            return m ? parseInt(m[1]) : 0;
          }, "getTs");
          return getTs(b) - getTs(a);
        });
        object = await env.THUMBNAILS_BUCKET.get(sorted2[0].key, { skipMeta: true });
      }
    }
    if (!object) object = await env.THUMBNAILS_BUCKET.get(`updates/${levelId}.webp`, { skipMeta: true });
    if (!object) {
      return new Response(JSON.stringify({ error: "Update not found" }), {
        status: 404,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const buf = await object.arrayBuffer();
    const respHeaders = new Headers();
    object.writeHttpMetadata(respHeaders);
    respHeaders.set("Access-Control-Allow-Origin", "*");
    respHeaders.set("Cache-Control", "public, s-maxage=604800, max-age=604800, stale-while-revalidate=300");
    respHeaders.delete("Pragma");
    respHeaders.delete("Expires");
    if (!respHeaders.has("Content-Type")) respHeaders.set("Content-Type", "image/webp");
    return new Response(buf, { headers: respHeaders });
  } catch (error) {
    console.error("Download update error:", error);
    return new Response(JSON.stringify({ error: "Download failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleDownloadUpdate, "handleDownloadUpdate");

// src/controllers/profiles.js
var PROFILE_KEY_TTL = 18e5;
async function resolveLatestKey(bucket, prefix, memKey) {
  const cached = memCache.get(memKey);
  if (cached) return cached;
  const list = await bucket.list({ prefix });
  if (list.objects.length === 0) {
    // Cache negative result for 5 minutes to avoid repeated list calls
    memCache.set(memKey, null, 3e5);
    return null;
  }
  const sorted2 = list.objects.sort((a, b) => {
    const getTs = /* @__PURE__ */ __name((k) => {
      const m = k.match(/_(\d+)\./);
      return m ? parseInt(m[1]) : 0;
    }, "getTs");
    return getTs(b.key) - getTs(a.key);
  });
  const key = sorted2[0].key;
  memCache.set(memKey, key, PROFILE_KEY_TTL);
  return key;
}
__name(resolveLatestKey, "resolveLatestKey");
async function handleUploadProfileConfig(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const formData = await request.formData();
    const accountID = formData.get("accountID");
    const username = formData.get("username");
    const config = formData.get("config");
    if (!accountID || !username || typeof config !== "string" || !config || config.length > 65536) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const writeAuth = await authorizeAccountWrite(request, env, username, accountID);
    if (!writeAuth.authorized) return writeAuth.response;
    try {
      JSON.parse(config);
    } catch (e) {
      return new Response(JSON.stringify({ error: "Invalid JSON" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const key = `profiles/config/${writeAuth.accountID}.json`;
    await env.THUMBNAILS_BUCKET.put(key, config, {
      httpMetadata: { contentType: "application/json", cacheControl: NO_STORE_CACHE_CONTROL }
    });
    invalidateProfileConfig(request, writeAuth.accountID, env).catch(() => {
    });
    return new Response(JSON.stringify({ success: true }), {
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleUploadProfileConfig, "handleUploadProfileConfig");
async function handleGetProfileConfig(request, env) {
  const url2 = new URL(request.url);
  const accountID = url2.pathname.split("/").pop().replace(".json", "");
  const key = `profiles/config/${accountID}.json`;
  try {
    const object = await env.THUMBNAILS_BUCKET.get(key, { skipMeta: true });
    if (!object) {
      return new Response(JSON.stringify({}), {
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const data = await object.text();
    memCache.set(`profile_config_${accountID}`, data, 3e5);
    return new Response(data, {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    if (error instanceof StorageError) {
      const cached = memCache.get(`profile_config_${accountID}`);
      if (cached !== void 0) {
        console.warn(`[Degradation] Serving cached profile config for ${accountID}`);
        return new Response(cached, {
          headers: { "Content-Type": "application/json", ...corsHeaders() }
        });
      }
      return new Response(JSON.stringify({ error: "Storage temporarily unavailable", code: "STORAGE_ERROR", retryable: true }), {
        status: 502,
        headers: { "Content-Type": "application/json", "Retry-After": "5", ...corsHeaders() }
      });
    }
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleGetProfileConfig, "handleGetProfileConfig");
async function handleUploadBackground(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const formData = await request.formData();
    const file = formData.get("image");
    const accountID = parseInt(formData.get("accountID") || "0");
    const username = formData.get("username") || "";
    const levelId = formData.get("levelId") || accountID.toString();
    if (!file || !accountID) {
      return new Response(JSON.stringify({ error: "Missing required fields (image, accountID)" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const accountVerification = await verifyAccountForWrite(env, accountID, username);
    if (!accountVerification.valid) {
      const errorMessages = {
        ACCOUNT_REQUIRED: "Valid accountID required",
        UNOFFICIAL_SERVER: "Upload requires official Boomlings server connection",
        ACCOUNT_NOT_FOUND: "Account not found on official servers",
        ACCOUNT_MISMATCH: "Account verification failed",
        USERNAME_MISMATCH: "Username does not match account",
        VERIFY_FAILED: "Account verification service unavailable"
      };
      return new Response(
        JSON.stringify({ error: errorMessages[accountVerification.reason] || "Account verification failed", code: accountVerification.reason }),
        { status: 403, headers: { "Content-Type": "application/json", ...corsNoStore() } }
      );
    }
    const banData = await getR2Json(env.SYSTEM_BUCKET, `data/bans/${accountID}.json`);
    if (banData && banData.banned) {
      return new Response(JSON.stringify({ error: "User is banned" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (username) {
      const role = await getUserRole(username.toLowerCase(), env.SYSTEM_BUCKET);
      const quota = await checkUploadQuota(env.SYSTEM_BUCKET, username, role);
      if (!quota.allowed) {
        return new Response(JSON.stringify({
          error: "Daily upload quota exceeded",
          limit: quota.limit,
          used: quota.used,
          message: `You have reached your daily upload limit (${quota.limit}). Try again tomorrow.`
        }), { status: 429, headers: { "Content-Type": "application/json", ...corsNoStore() } });
      }
    }
    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: "File too large" }), {
        status: 413,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);
    const fileType = file.type || "image/webp";
    let extension = "webp";
    if (fileType === "image/png") extension = "png";
    else if (fileType === "image/jpeg") extension = "jpg";
    else if (fileType === "image/gif") extension = "gif";
    const securityReject = rejectIfMalicious(buffer, fileType, file.name || `bg_${accountID}.${extension}`);
    if (securityReject) return securityReject;
    const usernameLower = username ? username.toLowerCase() : "";
    const authResult = await verifyModAuth(request, env, usernameLower, accountID);
    const isModerator = authResult.authorized;
    const isAdmin = authResult.authorized && ADMIN_USERS.includes(usernameLower);
    const isKnownModOrAdmin = await isModeratorOrAdmin(env, usernameLower);
    const modCodeMismatch = isKnownModOrAdmin && !isModerator && !!authResult.invalidCode;
    const [whitelistUsers, vips] = await Promise.all([
      getWhitelist(env.SYSTEM_BUCKET, "profilebackground"),
      getVips(env.SYSTEM_BUCKET)
    ]);
    const isWhitelisted = whitelistUsers.includes(usernameLower);
    const isVip = vips.includes(usernameLower);
    const ts = Date.now().toString();
    let uploadKey;
    let uploadCategory;
    if (!isModerator && !isAdmin && !isWhitelisted && !isVip) {
      uploadKey = `pending_profilebackground/${accountID}_${ts}.${extension}`;
      uploadCategory = "pending_profilebackground";
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountID}_` });
      if (pendingList.objects.length > 0) {
        await Promise.all(pendingList.objects.map((o) => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    } else {
      uploadKey = `profilebackground/${accountID}_${ts}.${extension}`;
      uploadCategory = "profilebackground";
      const prefixes = [`profilebackground/${accountID}.`, `profilebackground/${accountID}_`];
      const keysToDelete = [];
      for (const prefix of prefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix });
        for (const obj of list.objects) keysToDelete.push(obj.key);
      }
      if (keysToDelete.length > 0) await Promise.all(keysToDelete.map((k) => env.THUMBNAILS_BUCKET.delete(k)));
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountID}_` });
      if (pendingList.objects.length > 0) await Promise.all(pendingList.objects.map((o) => env.THUMBNAILS_BUCKET.delete(o.key)));
      if (ctx) ctx.waitUntil(env.SYSTEM_BUCKET.delete(`data/queue/profilebackground/${accountID}.json`));
      else await env.SYSTEM_BUCKET.delete(`data/queue/profilebackground/${accountID}.json`);
    }
    await env.THUMBNAILS_BUCKET.put(uploadKey, buffer, {
      httpMetadata: { contentType: fileType, cacheControl: "no-store, no-cache, must-revalidate, max-age=0" },
      customMetadata: {
        uploadedBy: username || "unknown",
        updated_by: username || "unknown",
        uploadedAt: (/* @__PURE__ */ new Date()).toISOString(),
        originalFormat: extension,
        version: ts,
        accountID: accountID.toString(),
        moderatorUpload: isModerator ? "true" : "false",
        whitelistUpload: isWhitelisted ? "true" : "false",
        category: uploadCategory,
        contentKind: "profilebackground"
      }
    });
    if (!isModerator && !isAdmin && !isWhitelisted && !isVip) {
      const queueKey = `data/queue/profilebackground/${accountID}.json`;
      const queueItem = {
        levelId: parseInt(accountID),
        accountID,
        submittedBy: username || "unknown",
        timestamp: Date.now(),
        status: "pending",
        category: "profilebackground",
        filename: uploadKey,
        format: extension
      };
      if (ctx) ctx.waitUntil(putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem));
      else await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);
    }
    if (isModerator || isAdmin || isWhitelisted || isVip) {
      logAudit(env.SYSTEM_BUCKET, "profilebackground_upload", {
        accountID,
        username: usernameLower,
        direct: true,
        reason: isVip ? "vip" : isWhitelisted ? "whitelist" : "moderator"
      }, ctx);
    }
    const isPending = uploadCategory === "pending_profilebackground";
    if (!isPending) {
      invalidateProfileBackground(request, accountID, env).catch(() => {
      });
    }
    const responseData = {
      success: true,
      message: isPending ? "Profile background submitted for verification" : "Background uploaded successfully",
      key: uploadKey,
      moderatorUpload: isModerator,
      whitelistUpload: isWhitelisted,
      vipUpload: isVip,
      pendingVerification: isPending,
      contentKind: "profilebackground"
    };
    if (modCodeMismatch) {
      responseData.modCodeMismatch = true;
      responseData.alert = "Mod code no coincidente";
    }
    if (username) {
      if (ctx && ctx.waitUntil) ctx.waitUntil(incrementUploadQuota(env.SYSTEM_BUCKET, username));
      else await incrementUploadQuota(env.SYSTEM_BUCKET, username).catch(() => {
      });
    }
    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Background upload error:", error);
    return new Response(JSON.stringify({ error: "Background upload failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleUploadBackground, "handleUploadBackground");
async function handleUploadBackgroundGIF(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const formData = await request.formData();
    const file = formData.get("image");
    const accountID = parseInt(formData.get("accountID") || "0");
    const username = formData.get("username") || "";
    if (!file || !accountID) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: "File too large" }), {
        status: 413,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (file.type !== "image/gif") {
      return new Response(JSON.stringify({ error: "Invalid file type. Only GIF allowed." }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const accountVerification = await verifyAccountForWrite(env, accountID, username);
    if (!accountVerification.valid) {
      const errorMessages = {
        ACCOUNT_REQUIRED: "Valid accountID required",
        UNOFFICIAL_SERVER: "Upload requires official Boomlings server connection",
        ACCOUNT_NOT_FOUND: "Account not found on official servers",
        USERNAME_MISMATCH: "Username does not match account",
        VERIFY_FAILED: "Account verification service unavailable"
      };
      return new Response(
        JSON.stringify({ error: errorMessages[accountVerification.reason] || "Account verification failed", code: accountVerification.reason }),
        { status: 403, headers: { "Content-Type": "application/json", ...corsNoStore() } }
      );
    }
    const usernameLower = username ? username.toLowerCase() : "";
    const vips = await getVips(env.SYSTEM_BUCKET);
    const isVip = vips.includes(usernameLower);
    const isModOrAdmin = await isModeratorOrAdmin(env, usernameLower);
    if (!isVip && !isModOrAdmin) {
      return new Response(JSON.stringify({ error: "Background GIF uploads are restricted to VIPs, moderators and admins" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);
    const securityReject = rejectIfMalicious(buffer, "image/gif", file.name || `bg_${accountID}.gif`);
    if (securityReject) return securityReject;
    const ts = Date.now().toString();
    const key = `profilebackground/${accountID}_${ts}.gif`;
    const prefixes = [`profilebackground/${accountID}.`, `profilebackground/${accountID}_`];
    const keysToDelete = [];
    for (const prefix of prefixes) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix });
      for (const obj of list.objects) keysToDelete.push(obj.key);
    }
    if (keysToDelete.length > 0) await Promise.all(keysToDelete.map((k) => env.THUMBNAILS_BUCKET.delete(k)));
    await env.THUMBNAILS_BUCKET.put(key, buffer, {
      httpMetadata: { contentType: "image/gif", cacheControl: "no-store, no-cache, must-revalidate, max-age=0" },
      customMetadata: {
        uploadedBy: username || "unknown",
        updated_by: username || "unknown",
        uploadedAt: (/* @__PURE__ */ new Date()).toISOString(),
        originalFormat: "gif",
        version: ts,
        accountID: accountID.toString(),
        moderatorUpload: isModOrAdmin ? "true" : "false",
        vipUpload: isVip ? "true" : "false",
        category: "profilebackground",
        contentKind: "profilebackground"
      }
    });
    const responseData = { success: true, message: "Background GIF uploaded successfully", key, contentKind: "profilebackground" };
    invalidateProfileBackground(request, accountID, env).catch(() => {
    });
    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Background GIF upload error:", error);
    return new Response(JSON.stringify({ error: "Upload failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleUploadBackgroundGIF, "handleUploadBackgroundGIF");
async function handleUploadBackgroundVideo(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const formData = await request.formData();
    const file = formData.get("image");
    const accountID = parseInt(formData.get("accountID") || "0");
    const username = formData.get("username") || "";
    if (!file || !accountID) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const maxVideoSize = parseInt(env.MAX_VIDEO_UPLOAD_SIZE || "26214400");
    if (file.size > maxVideoSize) {
      return new Response(JSON.stringify({ error: "Video file too large (max 25MB)" }), {
        status: 413,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (file.type !== "video/mp4") {
      return new Response(JSON.stringify({ error: "Invalid file type. Only MP4 video allowed." }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const accountVerification = await verifyAccountForWrite(env, accountID, username);
    if (!accountVerification.valid) {
      const errorMessages = {
        ACCOUNT_REQUIRED: "Valid accountID required",
        UNOFFICIAL_SERVER: "Upload requires official Boomlings server connection",
        ACCOUNT_NOT_FOUND: "Account not found on official servers",
        USERNAME_MISMATCH: "Username does not match account",
        VERIFY_FAILED: "Account verification service unavailable"
      };
      return new Response(
        JSON.stringify({ error: errorMessages[accountVerification.reason] || "Account verification failed", code: accountVerification.reason }),
        { status: 403, headers: { "Content-Type": "application/json", ...corsNoStore() } }
      );
    }
    const usernameLower = username ? username.toLowerCase() : "";
    const isModOrAdmin = await isModeratorOrAdmin(env, usernameLower);
    if (!isModOrAdmin) {
      return new Response(JSON.stringify({ error: "Background video uploads are restricted to moderators and admins only" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);
    const securityReject = rejectIfMalicious(buffer, "video/mp4", file.name || `bg_${accountID}.mp4`);
    if (securityReject) return securityReject;
    const codecReject = rejectIfNonCanonicalCodec(buffer);
    if (codecReject) return codecReject;
    const ts = Date.now().toString();
    const key = `profilebackground/${accountID}_${ts}.mp4`;
    const prefixes = [`profilebackground/${accountID}.`, `profilebackground/${accountID}_`];
    const keysToDelete = [];
    for (const prefix of prefixes) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix });
      for (const obj of list.objects) keysToDelete.push(obj.key);
    }
    if (keysToDelete.length > 0) await Promise.all(keysToDelete.map((k) => env.THUMBNAILS_BUCKET.delete(k)));
    await env.THUMBNAILS_BUCKET.put(key, buffer, {
      httpMetadata: { contentType: "video/mp4", cacheControl: "no-store, no-cache, must-revalidate, max-age=0" },
      customMetadata: {
        uploadedBy: username || "unknown",
        updated_by: username || "unknown",
        uploadedAt: (/* @__PURE__ */ new Date()).toISOString(),
        originalFormat: "mp4",
        version: ts,
        accountID: accountID.toString(),
        moderatorUpload: isModOrAdmin ? "true" : "false",
        category: "profilebackground",
        contentKind: "profilebackground"
      }
    });
    const responseData = { success: true, message: "Background video uploaded successfully", key, contentKind: "profilebackground" };
    invalidateProfileBackground(request, accountID, env).catch(() => {
    });
    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Background video upload error:", error);
    return new Response(JSON.stringify({ error: "Upload failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleUploadBackgroundVideo, "handleUploadBackgroundVideo");
async function handleServeBackground(request, env) {
  const url2 = new URL(request.url);
  const isSelfRequest = url2.searchParams.get("self") === "1";
  const pathParts = url2.pathname.split("/");
  const filename = pathParts[pathParts.length - 1];
  const accountId = filename.replace(/\.[^/.]+$/, "");
  if (!accountId) return new Response("Account ID required", { status: 400 });
  let foundObject = null;
  let isPending = false;
  const latestKey = await resolveLatestKey(env.THUMBNAILS_BUCKET, `profilebackground/${accountId}_`, `bg_latest_${accountId}`);
  if (latestKey) {
    foundObject = await env.THUMBNAILS_BUCKET.get(latestKey, { skipMeta: true, cfCacheTtl: 86400 });
  }
  if (!foundObject) {
    const legacyKey = await resolveLatestKey(env.THUMBNAILS_BUCKET, `backgrounds/${accountId}_`, `bg_legacy_${accountId}`);
    if (legacyKey) {
      foundObject = await env.THUMBNAILS_BUCKET.get(legacyKey, { skipMeta: true, cfCacheTtl: 86400 });
    }
  }
  if (!foundObject && isSelfRequest && await verifyApiKey(request, env)) {
    const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountId}_` });
    if (pendingList.objects.length > 0) {
      const sorted2 = pendingList.objects.sort((a, b) => {
        const getTs = /* @__PURE__ */ __name((k) => {
          const m = k.key.match(/_(\d+)\./);
          return m ? parseInt(m[1]) : 0;
        }, "getTs");
        return getTs(b) - getTs(a);
      });
      foundObject = await env.THUMBNAILS_BUCKET.get(sorted2[0].key, { skipMeta: true });
      isPending = true;
    }
  }
  if (foundObject) {
    const headers = new Headers();
    foundObject.writeHttpMetadata(headers);
    headers.set("etag", foundObject.httpEtag);
    headers.set("Access-Control-Allow-Origin", "*");
    headers.set("X-Content-Kind", "profilebackground");
    if (isPending) {
      headers.set("Cache-Control", "no-store, no-cache, must-revalidate");
      headers.set("X-Pending-Verification", "true");
    } else {
      headers.set("Cache-Control", "public, s-maxage=2592000, max-age=2592000, stale-while-revalidate=300");
      headers.delete("Pragma");
      headers.delete("Expires");
    }
    const body = await foundObject.arrayBuffer();
    return new Response(body, { headers });
  }
  return new Response("Background not found", { status: 404 });
}
__name(handleServeBackground, "handleServeBackground");
async function handleServeProfile(request, env) {
  const url2 = new URL(request.url);
  const isSelfRequest = url2.searchParams.get("self") === "1";
  const pathParts = url2.pathname.split("/");
  const filename = pathParts[pathParts.length - 1];
  const accountId = filename.replace(/\.[^/.]+$/, "");
  if (!accountId) return new Response("Account ID required", { status: 400 });
  let foundObject = null;
  let isPending = false;
  const latestKey = await resolveLatestKey(env.THUMBNAILS_BUCKET, `profiles/${accountId}_`, `prof_latest_${accountId}`);
  if (latestKey) {
    foundObject = await env.THUMBNAILS_BUCKET.get(latestKey, { skipMeta: true, cfCacheTtl: 86400 });
  }
  if (!foundObject) {
    const extensions = ["gif", "webp", "png", "jpg", "jpeg"];
    for (const ext of extensions) {
      const key = `profiles/${accountId}.${ext}`;
      const object = await env.THUMBNAILS_BUCKET.get(key, { skipMeta: true, cfCacheTtl: 86400 });
      if (object) {
        foundObject = object;
        break;
      }
    }
  }
  if (!foundObject && isSelfRequest && await verifyApiKey(request, env)) {
    const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profiles/${accountId}_` });
    if (pendingList.objects.length > 0) {
      const sorted2 = pendingList.objects.sort((a, b) => {
        const getTs = /* @__PURE__ */ __name((k) => {
          const m = k.key.match(/_(\d+)\./);
          return m ? parseInt(m[1]) : 0;
        }, "getTs");
        return getTs(b) - getTs(a);
      });
      foundObject = await env.THUMBNAILS_BUCKET.get(sorted2[0].key, { skipMeta: true });
      isPending = true;
    }
  }
  if (foundObject) {
    const headers = new Headers();
    foundObject.writeHttpMetadata(headers);
    headers.set("etag", foundObject.httpEtag);
    headers.set("Access-Control-Allow-Origin", "*");
    if (isPending) {
      headers.set("Cache-Control", "no-store, no-cache, must-revalidate");
      headers.set("X-Pending-Verification", "true");
    } else {
      headers.set("Cache-Control", "public, s-maxage=2592000, max-age=2592000, stale-while-revalidate=300");
      headers.delete("Pragma");
      headers.delete("Expires");
    }
    const body = await foundObject.arrayBuffer();
    return new Response(body, { headers });
  }
  return new Response("Profile not found", { status: 404 });
}
__name(handleServeProfile, "handleServeProfile");
async function handleServeProfileImg(request, env) {
  const url2 = new URL(request.url);
  const isSelfRequest = url2.searchParams.get("self") === "1";
  const isPendingOnly = url2.searchParams.get("pending") === "1";
  const pathParts = url2.pathname.split("/");
  const filename = pathParts[pathParts.length - 1];
  const accountId = filename.replace(/\.[^/.]+$/, "");
  if (!accountId) return new Response("Account ID required", { status: 400 });
  let foundObject = null;
  let isPending = false;
  const latestKey = await resolveLatestKey(env.THUMBNAILS_BUCKET, `profileimgs/${accountId}_`, `pimg_latest_${accountId}`);
  if (latestKey) {
    foundObject = await env.THUMBNAILS_BUCKET.get(latestKey, { skipMeta: true, cfCacheTtl: 86400 });
  }
  if (!foundObject) {
    const extensions = ["gif", "webp", "png", "jpg", "jpeg", "bmp", "tiff"];
    for (const ext of extensions) {
      const key = `profileimgs/${accountId}.${ext}`;
      const object = await env.THUMBNAILS_BUCKET.get(key, { skipMeta: true, cfCacheTtl: 86400 });
      if (object) {
        foundObject = object;
        break;
      }
    }
  }
  if (!foundObject && isSelfRequest && await verifyApiKey(request, env) || isPendingOnly && await verifyApiKey(request, env)) {
    if (isPendingOnly) foundObject = null;
    if (!foundObject) {
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${accountId}_` });
      if (pendingList.objects.length > 0) {
        const sorted2 = pendingList.objects.sort((a, b) => {
          const getTs = /* @__PURE__ */ __name((k) => {
            const m = k.key.match(/_(\d+)\./);
            return m ? parseInt(m[1]) : 0;
          }, "getTs");
          return getTs(b) - getTs(a);
        });
        foundObject = await env.THUMBNAILS_BUCKET.get(sorted2[0].key, { skipMeta: true });
        isPending = true;
      }
    }
  }
  if (foundObject) {
    const headers = new Headers();
    foundObject.writeHttpMetadata(headers);
    headers.set("etag", foundObject.httpEtag);
    headers.set("Access-Control-Allow-Origin", "*");
    if (isPending) {
      headers.set("Cache-Control", "no-store, no-cache, must-revalidate");
      headers.set("X-Pending-Verification", "true");
    } else {
      headers.set("Cache-Control", "public, s-maxage=2592000, max-age=2592000, stale-while-revalidate=300");
      headers.delete("Pragma");
      headers.delete("Expires");
    }
    const body = await foundObject.arrayBuffer();
    return new Response(body, { headers });
  }
  return new Response("Profile image not found", { status: 404 });
}
__name(handleServeProfileImg, "handleServeProfileImg");
async function handleProfileBundle(request, env) {
  const url2 = new URL(request.url);
  const pathParts = url2.pathname.split("/");
  const accountId = pathParts[pathParts.length - 1];
  if (!accountId || accountId === "bundle") {
    return new Response(JSON.stringify({ error: "Account ID required" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const username = (url2.searchParams.get("username") || "").toLowerCase().trim();
  const bundleCacheKey = `bundle_${accountId}_${username}`;
  const cachedBundle = memCache.get(bundleCacheKey);
  if (cachedBundle !== void 0) {
    return new Response(JSON.stringify(cachedBundle), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  try {
    const workerOrigin = new URL(request.url).origin;
    const leaderboardCacheKey = "leaderboard_full";
    let leaderboard = memCache.get(leaderboardCacheKey);
    const leaderboardPromise = leaderboard !== void 0 ? Promise.resolve(leaderboard) : getR2Json(env.SYSTEM_BUCKET, "data/system/creator_leaderboard.json").then((r) => {
      const lb = r || [];
      memCache.set(leaderboardCacheKey, lb, 72e5);
      return lb;
    });
    const [configObj, moderators, bgList, imgList, badgeObj, resolvedLeaderboard, musicConfig] = await Promise.all([
      env.THUMBNAILS_BUCKET.get(`profiles/config/${accountId}.json`, { skipMeta: true }),
      getModerators(env.SYSTEM_BUCKET),
      env.THUMBNAILS_BUCKET.list({ prefix: `profilebackground/${accountId}_` }),
      env.THUMBNAILS_BUCKET.list({ prefix: `profileimgs/${accountId}_` }),
      env.SYSTEM_BUCKET.get(`data/badges/${accountId}.json`, { skipMeta: true }),
      leaderboardPromise,
      getR2Json(env.SYSTEM_BUCKET, `profile-music/${accountId}.json`)
    ]);
    leaderboard = resolvedLeaderboard;
    let config = null;
    if (configObj) {
      try {
        config = JSON.parse(await configObj.text());
      } catch {
        config = null;
      }
    }
    let isModerator = false;
    let role = null;
    if (username) {
      if (ADMIN_USERS.includes(username)) {
        isModerator = true;
        role = "admin";
      } else if (moderators.includes(username)) {
        isModerator = true;
        role = "mod";
      }
    }
    const mostRecentKey = /* @__PURE__ */ __name((objects) => {
      if (objects.length === 0) return null;
      const sorted2 = [...objects].sort((a, b) => {
        const getTs = /* @__PURE__ */ __name((k) => {
          const m = k.match(/_(\d+)\./);
          return m ? parseInt(m[1]) : 0;
        }, "getTs");
        return getTs(b.key) - getTs(a.key);
      });
      return sorted2[0].key;
    }, "mostRecentKey");
    const bgKey = mostRecentKey(bgList.objects);
    const backgroundCdnUrl = bgKey ? `${workerOrigin}/profilebackground/${accountId}` : null;
    const imgKey = mostRecentKey(imgList.objects);
    const profileImgCdnUrl = imgKey ? `${workerOrigin}/profileimgs/${accountId}` : null;
    let badge = null;
    if (badgeObj) {
      try {
        badge = JSON.parse(await badgeObj.text());
        memCache.set(`badge_${accountId}`, badge, 18e5);
      } catch {
        badge = null;
      }
    } else {
      memCache.set(`badge_${accountId}`, {}, 18e5);
    }
    const entry = leaderboard.find((c) => String(c.accountID) === String(accountId));
    const stats = entry ? {
      accountID: parseInt(accountId),
      username: entry.username,
      uploadCount: entry.uploadCount || 0,
      avgRating: entry.avgRating || 0,
      totalVotes: entry.totalVotes || 0,
      totalRating: entry.totalRating || 0,
      rank: leaderboard.indexOf(entry) + 1
    } : {
      accountID: parseInt(accountId),
      username: null,
      uploadCount: 0,
      avgRating: 0,
      totalVotes: 0,
      totalRating: 0,
      rank: null
    };
    memCache.set(`profile_stats_${accountId}`, stats, 72e5);
    const bundleResult = {
      config,
      isModerator,
      role,
      backgroundCdnUrl,
      profileImgCdnUrl,
      badge,
      stats,
      music: musicConfig || null
    };
    memCache.set(bundleCacheKey, bundleResult, 3e5);
    return new Response(JSON.stringify(bundleResult), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    if (error instanceof StorageError) {
      const cachedBundle2 = memCache.get(bundleCacheKey);
      if (cachedBundle2 !== void 0) {
        console.warn(`[Degradation] Serving cached bundle for ${accountId}`);
        return new Response(JSON.stringify({ ...cachedBundle2, _degraded: true }), {
          status: 200,
          headers: { "Content-Type": "application/json", ...corsHeaders() }
        });
      }
      return new Response(JSON.stringify({ error: "Storage temporarily unavailable", code: "STORAGE_ERROR", retryable: true }), {
        status: 502,
        headers: { "Content-Type": "application/json", "Retry-After": "5", ...corsHeaders() }
      });
    }
    console.error("[Profiles] Bundle error:", error);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleProfileBundle, "handleProfileBundle");

// ── /api/profile/batch-bundle — Fetch bundles for multiple accounts in one request ──
// Useful for leaderboard cells, score cells, moderator lists
async function handleBatchProfileBundle(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    // Cap was 20 — but each account triggers 4 parallel storage ops
    // (bgList, imgList, badge get, music get), so 20 * 4 = 80 subrequests
    // which exceeds the Workers Free 50-subrequest budget. 12 * 4 = 48,
    // leaving headroom for the leaderboard + moderators preload.
    const accounts = (body.accounts || []).slice(0, 12);
    if (!Array.isArray(accounts) || accounts.length === 0) {
      return new Response(JSON.stringify({ error: "accounts must be a non-empty array" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }

    const workerOrigin = new URL(request.url).origin;

    // Shared data: load leaderboard + moderators once for all profiles
    const leaderboardCacheKey = "leaderboard_full";
    let leaderboard = memCache.get(leaderboardCacheKey);
    if (leaderboard === void 0) {
      leaderboard = await getR2Json(env.SYSTEM_BUCKET, "data/system/creator_leaderboard.json") || [];
      memCache.set(leaderboardCacheKey, leaderboard, 72e5);
    }
    const moderators = await getModerators(env.SYSTEM_BUCKET);

    const bundles = {};
    await Promise.all(accounts.map(async (acc) => {
      const accountId = String(acc.accountID || acc.id || acc);
      const username = (acc.username || "").toLowerCase().trim();
      if (!accountId || accountId === "0") return;

      // Check memCache first
      const bundleCacheKey = `bundle_${accountId}_${username}`;
      const cached = memCache.get(bundleCacheKey);
      if (cached !== void 0) {
        bundles[accountId] = cached;
        return;
      }

      try {
        const [bgList, imgList, badgeObj, musicConfig] = await Promise.all([
          env.THUMBNAILS_BUCKET.list({ prefix: `profilebackground/${accountId}_` }),
          env.THUMBNAILS_BUCKET.list({ prefix: `profileimgs/${accountId}_` }),
          env.SYSTEM_BUCKET.get(`data/badges/${accountId}.json`, { skipMeta: true }),
          getR2Json(env.SYSTEM_BUCKET, `profile-music/${accountId}.json`)
        ]);

        // Mod status
        let isModerator = false;
        let role = null;
        if (username) {
          if (ADMIN_USERS.includes(username)) { isModerator = true; role = "admin"; }
          else if (moderators.includes(username)) { isModerator = true; role = "mod"; }
        }

        // Background/img URLs
        const mostRecentKey = (objects) => {
          if (objects.length === 0) return null;
          const sorted = [...objects].sort((a, b) => {
            const getTs = (k) => { const m = k.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
            return getTs(b.key) - getTs(a.key);
          });
          return sorted[0].key;
        };
        const bgKey = mostRecentKey(bgList.objects);
        const backgroundCdnUrl = bgKey ? `${workerOrigin}/profilebackground/${accountId}` : null;
        const imgKey = mostRecentKey(imgList.objects);
        const profileImgCdnUrl = imgKey ? `${workerOrigin}/profileimgs/${accountId}` : null;

        // Badge
        let badge = null;
        if (badgeObj) {
          try { badge = JSON.parse(await badgeObj.text()); } catch { badge = null; }
        }

        // Stats from leaderboard
        const entry = leaderboard.find((c) => String(c.accountID) === accountId);
        const stats = entry ? {
          uploadCount: entry.uploadCount || 0,
          avgRating: entry.avgRating || 0,
          rank: leaderboard.indexOf(entry) + 1
        } : { uploadCount: 0, avgRating: 0, rank: null };

        const bundleResult = {
          isModerator, role, backgroundCdnUrl, profileImgCdnUrl,
          badge, stats, music: musicConfig || null
        };
        memCache.set(bundleCacheKey, bundleResult, 3e5);
        bundles[accountId] = bundleResult;
      } catch (e) {
        console.warn(`[BatchBundle] Error for account ${accountId}:`, e.message);
        bundles[accountId] = null;
      }
    }));

    return new Response(JSON.stringify({ bundles }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    console.error("[BatchBundle] error:", error);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleBatchProfileBundle, "handleBatchProfileBundle");

async function handleGetProfileStats(request, env) {
  try {
    const url2 = new URL(request.url);
    const pathParts = url2.pathname.split("/");
    const accountId = pathParts[pathParts.length - 1];
    if (!accountId) {
      return new Response(JSON.stringify({ error: "Account ID required" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const cacheKey = `profile_stats_${accountId}`;
    let cached = memCache.get(cacheKey);
    if (cached !== void 0) {
      return new Response(JSON.stringify(cached), {
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const leaderboard = await getR2Json(env.SYSTEM_BUCKET, "data/system/creator_leaderboard.json") || [];
    const entry = leaderboard.find((c) => String(c.accountID) === String(accountId));
    const result = entry ? {
      accountID: parseInt(accountId),
      username: entry.username,
      uploadCount: entry.uploadCount || 0,
      avgRating: entry.avgRating || 0,
      totalVotes: entry.totalVotes || 0,
      totalRating: entry.totalRating || 0,
      rank: leaderboard.indexOf(entry) + 1
    } : {
      accountID: parseInt(accountId),
      username: null,
      uploadCount: 0,
      avgRating: 0,
      totalVotes: 0,
      totalRating: 0,
      rank: null
    };
    memCache.set(cacheKey, result, 72e5);
    return new Response(JSON.stringify(result), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (e) {
    if (e instanceof StorageError) {
      const cached = memCache.get(`profile_stats_${url2.pathname.split("/").pop()}`);
      if (cached !== void 0) {
        console.warn(`[Degradation] Serving cached profile stats`);
        return new Response(JSON.stringify(cached), {
          headers: { "Content-Type": "application/json", ...corsHeaders() }
        });
      }
      return new Response(JSON.stringify({ error: "Storage temporarily unavailable", code: "STORAGE_ERROR", retryable: true }), {
        status: 502,
        headers: { "Content-Type": "application/json", "Retry-After": "5", ...corsHeaders() }
      });
    }
    return new Response(JSON.stringify({ error: e.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleGetProfileStats, "handleGetProfileStats");
// ── /api/profile/bgkind/:accountID ──
// Devuelve un "tipo" liviano del fondo de perfil de la cuenta:
//   { accountID, kind: "image"|"gif"|"video"|"gradient"|"icon-gradient"|"none",
//     hasMedia, mediaUrl, configBackgroundType, colorA?, colorB? }
//
// Sirve como llave rapida para los clientes: pueden saber si tienen que
// descargar el banner (image/gif/video) o pintarlo localmente con un
// degradado (gradient / icon-gradient) sin hacer la peticion completa
// del bundle.
async function handleGetProfileBgKind(request, env) {
  const url2 = new URL(request.url);
  const pathParts = url2.pathname.split("/");
  const accountIdRaw = pathParts[pathParts.length - 1] || "";
  const accountId = accountIdRaw.replace(/[^0-9]/g, "");
  if (!accountId) {
    return new Response(JSON.stringify({ error: "Account ID required" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }

  const cacheKey = `profile_bgkind_${accountId}`;
  const cached = memCache.get(cacheKey);
  if (cached !== void 0) {
    return new Response(JSON.stringify(cached), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }

  try {
    // Cargamos config + lista de assets en paralelo.
    const [configObj, bgList, legacyBgList] = await Promise.all([
      env.THUMBNAILS_BUCKET.get(`profiles/config/${accountId}.json`, { skipMeta: true }),
      env.THUMBNAILS_BUCKET.list({ prefix: `profilebackground/${accountId}_` }),
      env.THUMBNAILS_BUCKET.list({ prefix: `backgrounds/${accountId}_` })
    ]);

    let configBackgroundType = null;
    let gradientEffect = null;
    let gradientSpeed = null;
    let useVideoAudio = false;
    // NOTA: colorA / colorB se omiten a proposito.  El cliente toma los
    // colores del icono en vivo (GameManager para perfil propio,
    // GJUserScore para otros), asi que no tiene sentido devolverlos aqui:
    // serian un snapshot viejo y confundirian a clientes externos.
    if (configObj) {
      try {
        const cfg = JSON.parse(await configObj.text());
        if (cfg && typeof cfg === "object") {
          configBackgroundType = typeof cfg.backgroundType === "string"
            ? cfg.backgroundType
            : null;
          if (typeof cfg.gradientEffect === "string") {
            const validEffects = ["none", "rotate", "pulse", "shift", "slide"];
            gradientEffect = validEffects.includes(cfg.gradientEffect)
              ? cfg.gradientEffect
              : "none";
          }
          if (typeof cfg.gradientSpeed === "number" && Number.isFinite(cfg.gradientSpeed)) {
            // Clamp 0.1 - 5.0 igual que el cliente
            gradientSpeed = Math.min(5.0, Math.max(0.1, cfg.gradientSpeed));
          }
          if (typeof cfg.useVideoAudio === "boolean") {
            useVideoAudio = cfg.useVideoAudio;
          }
        }
      } catch (_) {
        // Config corrupta: la ignoramos y caemos a la lista de assets.
      }
    }

    // Localiza el asset mas reciente para deducir extension.
    const pickLatestKey = /* @__PURE__ */ __name((objects) => {
      if (!objects || objects.length === 0) return null;
      const sorted2 = [...objects].sort((a, b) => {
        const getTs = /* @__PURE__ */ __name((k) => {
          const m = k.match(/_(\d+)\./);
          return m ? parseInt(m[1]) : 0;
        }, "getTs");
        return getTs(b.key) - getTs(a.key);
      });
      return sorted2[0].key;
    }, "pickLatestKey");

    let mediaKey = pickLatestKey(bgList.objects);
    if (!mediaKey) mediaKey = pickLatestKey(legacyBgList.objects);

    const extToKind = /* @__PURE__ */ __name((key) => {
      if (!key) return null;
      const ext = (key.split(".").pop() || "").toLowerCase();
      if (ext === "mp4" || ext === "mov" || ext === "m4v") return "video";
      if (ext === "gif") return "gif";
      return "image";
    }, "extToKind");

    let mediaKind = extToKind(mediaKey);

    // Decision final: la config explicita gana si pide "none" o
    // "icon-gradient".  Para "thumbnail" / "gradient" preferimos lo que
    // realmente tiene el usuario en el bucket (si hay imagen, devolvemos
    // imagen).  Si no hay nada, devolvemos "gradient" (default render).
    let kind;
    if (configBackgroundType === "none") {
      kind = "none";
    } else if (configBackgroundType === "icon-gradient") {
      kind = "icon-gradient";
    } else if (mediaKind) {
      kind = mediaKind;
    } else if (configBackgroundType === "gradient") {
      kind = "gradient";
    } else {
      kind = "none";
    }

    const workerOrigin = url2.origin;
    const result = {
      accountID: parseInt(accountId),
      kind,
      hasMedia: mediaKey !== null,
      mediaUrl: mediaKey ? `${workerOrigin}/profilebackground/${accountId}` : null,
      configBackgroundType
    };
    // colorA / colorB ya no se devuelven: el cliente los obtiene en vivo
    // del jugador (GameManager / GJUserScore).
    // Solo expongamos los campos de gradient cuando aporten algo: si no hay
    // animacion explicita (none) y la velocidad es la base, omitimos para
    // mantener la respuesta liviana.
    if (gradientEffect && gradientEffect !== "none") {
      result.gradientEffect = gradientEffect;
    }
    if (gradientSpeed !== null && gradientSpeed !== 1.0) {
      result.gradientSpeed = gradientSpeed;
    }
    // useVideoAudio se expone solo cuando esta activo: los clientes que no
    // lo soporten simplemente lo ignoran como cualquier campo desconocido.
    if (useVideoAudio) {
      result.useVideoAudio = true;
    }

    memCache.set(cacheKey, result, 6e5); // 10 min
    return new Response(JSON.stringify(result), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    if (error instanceof StorageError) {
      const cached2 = memCache.get(cacheKey);
      if (cached2 !== void 0) {
        console.warn(`[Degradation] Serving cached profile bgkind for ${accountId}`);
        return new Response(JSON.stringify({ ...cached2, _degraded: true }), {
          headers: { "Content-Type": "application/json", ...corsHeaders() }
        });
      }
      return new Response(JSON.stringify({ error: "Storage temporarily unavailable", code: "STORAGE_ERROR", retryable: true }), {
        status: 502,
        headers: { "Content-Type": "application/json", "Retry-After": "5", ...corsHeaders() }
      });
    }
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleGetProfileBgKind, "handleGetProfileBgKind");
async function handleBatchCheckProfiles(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const accountIDs = body.accountIDs;
    if (!Array.isArray(accountIDs) || accountIDs.length === 0) {
      return new Response(JSON.stringify({ error: "accountIDs must be a non-empty array" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    // Cap was 100 — but each id triggers up to 3 storage ops in parallel
    // (resolveLatestKey for profilebackground, fallback resolve for legacy
    // backgrounds, getR2Json for the config). With 100 ids that's up to
    // 300 subrequests, far above the 50-subrequest Workers Free budget.
    // 16 ids * 3 ops = 48 keeps us safely under the limit.
    const ids = accountIDs.slice(0, 16).map((id) => parseInt(id)).filter((id) => id > 0);
    if (ids.length === 0) {
      return new Response(JSON.stringify({ found: [], notFound: [], configs: {} }), {
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const found = [];
    const notFound = [];
    const configs = {};
    const checks = ids.map(async (accountId) => {
      const key = await resolveLatestKey(
        env.THUMBNAILS_BUCKET,
        `profilebackground/${accountId}_`,
        `bg_latest_${accountId}`
      );
      if (key) {
        found.push(accountId);
        const configKey = `profiles/config/${accountId}.json`;
        const configObj = await env.THUMBNAILS_BUCKET.get(configKey, { skipMeta: true });
        if (configObj) {
          try {
            const configData = JSON.parse(await configObj.text());
            configs[accountId] = configData;
          } catch (_) {
          }
        }
      } else {
        const legacyKey = await resolveLatestKey(
          env.THUMBNAILS_BUCKET,
          `backgrounds/${accountId}_`,
          `bg_legacy_${accountId}`
        );
        if (legacyKey) {
          found.push(accountId);
          const configKey = `profiles/config/${accountId}.json`;
          const configObj = await env.THUMBNAILS_BUCKET.get(configKey, { skipMeta: true });
          if (configObj) {
            try {
              const configData = JSON.parse(await configObj.text());
              configs[accountId] = configData;
            } catch (_) {
            }
          }
        } else {
          notFound.push(accountId);
        }
      }
    });
    await Promise.all(checks);
    return new Response(JSON.stringify({ found, notFound, configs }), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (e) {
    return new Response(JSON.stringify({ error: e.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleBatchCheckProfiles, "handleBatchCheckProfiles");

// src/controllers/badges.js
var BADGE_MEM_TTL = 18e5;
var EMOTE_NAME_MAX = 64;
var EMOTE_NAME_RE = /^[a-zA-Z0-9_-]+$/;
function extractAccountId(path) {
  const parts = path.split("/");
  return parts[parts.length - 1];
}
__name(extractAccountId, "extractAccountId");
async function handleSetCustomBadge(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  let formData;
  try {
    formData = await request.formData();
  } catch {
    return new Response(JSON.stringify({ error: "Invalid form data" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const accountID = formData.get("accountID");
  const username = formData.get("username");
  const emoteName = formData.get("emoteName");
  if (!accountID || !username || typeof emoteName !== "string" || !emoteName) {
    return new Response(JSON.stringify({ error: "Missing required fields: username, accountID and emoteName" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  if (emoteName.length > EMOTE_NAME_MAX) {
    return new Response(JSON.stringify({ error: `emoteName must be ${EMOTE_NAME_MAX} characters or fewer` }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  if (!EMOTE_NAME_RE.test(emoteName)) {
    return new Response(JSON.stringify({ error: "emoteName may only contain alphanumeric characters, underscores, and hyphens" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const writeAuth = await authorizeAccountWrite(request, env, username, accountID);
  if (!writeAuth.authorized) return writeAuth.response;
  try {
    const ban = await env.SYSTEM_BUCKET.get("data/bans/" + writeAuth.accountID + ".json", { skipMeta: true });
    if (ban) {
      return new Response(JSON.stringify({ error: "Account is banned" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
  } catch {
  }
  const payload = JSON.stringify({ emote: emoteName, updatedAt: (/* @__PURE__ */ new Date()).toISOString() });
  try {
    await env.SYSTEM_BUCKET.put("data/badges/" + writeAuth.accountID + ".json", payload, {
      httpMetadata: {
        contentType: "application/json",
        cacheControl: NO_STORE_CACHE_CONTROL
      }
    });
  } catch (err) {
    return new Response(JSON.stringify({ error: "Failed to store badge: " + err.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  invalidateCustomBadge(request, writeAuth.accountID, env).catch(() => {
  });
  return new Response(JSON.stringify({ success: true }), {
    headers: { "Content-Type": "application/json", ...corsNoStore() }
  });
}
__name(handleSetCustomBadge, "handleSetCustomBadge");
async function handleGetCustomBadge(request, env) {
  const url2 = new URL(request.url);
  const accountID = extractAccountId(url2.pathname);
  if (!accountID) {
    return new Response(JSON.stringify({ error: "Missing accountID in path" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const memKey = `badge_${accountID}`;
  const cached = memCache.get(memKey);
  if (cached !== void 0) {
    return new Response(JSON.stringify(cached), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  let badgeData;
  try {
    const obj = await env.SYSTEM_BUCKET.get("data/badges/" + accountID + ".json", { skipMeta: true });
    if (!obj) {
      memCache.set(memKey, {}, BADGE_MEM_TTL);
      return new Response(JSON.stringify({}), {
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const text = await obj.text();
    badgeData = JSON.parse(text);
  } catch (err) {
    if (err instanceof StorageError) {
      const cached2 = memCache.get(memKey);
      if (cached2 !== void 0) {
        console.warn(`[Degradation] Serving cached badge for ${accountID}`);
        return new Response(JSON.stringify(cached2), {
          headers: { "Content-Type": "application/json", ...corsHeaders() }
        });
      }
      return new Response(JSON.stringify({ error: "Storage temporarily unavailable", code: "STORAGE_ERROR", retryable: true }), {
        status: 502,
        headers: { "Content-Type": "application/json", "Retry-After": "5", ...corsHeaders() }
      });
    }
    return new Response(JSON.stringify({ error: "Failed to retrieve badge: " + err.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  memCache.set(memKey, badgeData, BADGE_MEM_TTL);
  return new Response(JSON.stringify(badgeData), {
    headers: { "Content-Type": "application/json", ...corsHeaders() }
  });
}
__name(handleGetCustomBadge, "handleGetCustomBadge");
async function handleDeleteCustomBadge(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  let formData;
  try {
    formData = await request.formData();
  } catch {
    return new Response(JSON.stringify({ error: "Invalid form data" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const accountID = formData.get("accountID");
  const username = formData.get("username");
  if (!accountID || !username) {
    return new Response(JSON.stringify({ error: "Missing required fields: username and accountID" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const writeAuth = await authorizeAccountWrite(request, env, username, accountID);
  if (!writeAuth.authorized) return writeAuth.response;
  try {
    await env.SYSTEM_BUCKET.delete("data/badges/" + writeAuth.accountID + ".json");
  } catch (err) {
    return new Response(JSON.stringify({ error: "Failed to delete badge: " + err.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  invalidateCustomBadge(request, writeAuth.accountID, env).catch(() => {
  });
  return new Response(JSON.stringify({ success: true }), {
    headers: { "Content-Type": "application/json", ...corsNoStore() }
  });
}
__name(handleDeleteCustomBadge, "handleDeleteCustomBadge");
var BATCH_MAX_IDS = 20;
async function handleGetCustomBadgeBatch(request, env) {
  const url2 = new URL(request.url);
  const idsParam = url2.searchParams.get("ids") || "";
  if (!idsParam) {
    return new Response(JSON.stringify({ error: "Missing required query param: ids" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const rawIds = idsParam.split(",").map((s) => s.trim()).filter(Boolean);
  if (rawIds.length === 0) {
    return new Response(JSON.stringify({ badges: {} }), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  if (rawIds.length > BATCH_MAX_IDS) {
    return new Response(JSON.stringify({ error: `Maximum ${BATCH_MAX_IDS} IDs per request` }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const accountIDs = [];
  for (const id of rawIds) {
    if (!/^\d+$/.test(id)) {
      return new Response(JSON.stringify({ error: "Invalid accountID: " + id }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    accountIDs.push(id);
  }
  const badges = {};
  await Promise.all(accountIDs.map(async (accountID) => {
    const memKey = `badge_${accountID}`;
    const cached = memCache.get(memKey);
    if (cached !== void 0) {
      badges[accountID] = cached;
      return;
    }
    try {
      const obj = await env.SYSTEM_BUCKET.get("data/badges/" + accountID + ".json", { skipMeta: true });
      if (!obj) {
        memCache.set(memKey, {}, BADGE_MEM_TTL);
        badges[accountID] = {};
        return;
      }
      const text = await obj.text();
      const badgeData = JSON.parse(text);
      memCache.set(memKey, badgeData, BADGE_MEM_TTL);
      badges[accountID] = badgeData;
    } catch {
      badges[accountID] = {};
    }
  }));
  return new Response(JSON.stringify({ badges }), {
    headers: { "Content-Type": "application/json", ...corsHeaders() }
  });
}
__name(handleGetCustomBadgeBatch, "handleGetCustomBadgeBatch");

// src/controllers/music.js
async function handleGetProfileMusicAudio(request, env) {
  const url2 = new URL(request.url);
  const pathParts = url2.pathname.split("/");
  const accountID = pathParts[pathParts.length - 2];
  if (!accountID) {
    return new Response("Not found", { status: 404, headers: corsHeaders() });
  }
  try {
    const configKey = `profile-music/${accountID}.json`;
    const config = await getR2Json(env.SYSTEM_BUCKET, configKey);
    const extension = config?.format || "mp3";
    let audioKey = `profile-music/${accountID}.${extension}`;
    let audioObj = await env.THUMBNAILS_BUCKET.get(audioKey, { skipMeta: true, cfCacheTtl: 0 });
    if (!audioObj) {
      const altExtension = extension === "mp3" ? "wav" : "mp3";
      audioKey = `profile-music/${accountID}.${altExtension}`;
      audioObj = await env.THUMBNAILS_BUCKET.get(audioKey, { skipMeta: true, cfCacheTtl: 0 });
    }
    if (!audioObj) {
      return new Response("Music not found", { status: 404, headers: corsHeaders() });
    }
    const contentType = audioKey.endsWith(".wav") ? "audio/wav" : "audio/mpeg";
    const headers = {
      "Content-Type": contentType,
      "Cache-Control": "public, s-maxage=2592000, max-age=2592000, stale-while-revalidate=300",
      ...corsHeaders()
    };
    if (config) {
      headers["X-Start-Ms"] = (config.startMs ?? 0).toString();
      headers["X-End-Ms"] = (config.endMs ?? 20000).toString();
      // Permite volumen 0 (silenciado) sin sobrescribir a 1.0.
      headers["X-Volume"] = (typeof config.volume === "number" && isFinite(config.volume)
        ? Math.max(0, Math.min(1, config.volume))
        : 1.0).toString();
    }
    const body = await audioObj.arrayBuffer();
    return new Response(body, { status: 200, headers });
  } catch (error) {
    console.error("Serve profile music audio error:", error);
    return new Response("Error serving music", { status: 500, headers: corsHeaders() });
  }
}
__name(handleGetProfileMusicAudio, "handleGetProfileMusicAudio");
async function handleGetProfileMusic(request, env) {
  const url2 = new URL(request.url);
  const pathParts = url2.pathname.split("/");
  const accountID = pathParts[pathParts.length - 1];
  if (!accountID || accountID === "undefined") {
    return new Response(JSON.stringify({ error: "Account ID required" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  try {
    const configKey = `profile-music/${accountID}.json`;
    const config = await getR2Json(env.SYSTEM_BUCKET, configKey);
    if (!config) {
      return new Response(JSON.stringify({ error: "No music configured" }), {
        status: 404,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    return new Response(JSON.stringify(config), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    console.error("Get profile music error:", error);
    return new Response(JSON.stringify({ error: "Failed to get music config" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleGetProfileMusic, "handleGetProfileMusic");
async function handleUploadProfileMusic(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const data = await request.json();
    let { accountID } = data;
    const { username, songID, startMs, endMs, volume, songName, artistName, isCustom } = data;
    if (!accountID || !username || !songID) {
      return new Response(JSON.stringify({ error: "Username, Account ID and Song ID required" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    // Las canciones custom usan songID = -1 (sigue siendo "truthy" para el guard de arriba).
    // Validamos el resto de campos para evitar entradas invalidas.
    if (typeof startMs !== "number" || typeof endMs !== "number") {
      return new Response(JSON.stringify({ error: "startMs and endMs must be numbers" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const duration = endMs - startMs;
    if (duration > 2e4) {
      return new Response(JSON.stringify({ error: "Fragment cannot exceed 20 seconds" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (duration < 5e3) {
      return new Response(JSON.stringify({ error: "Fragment must be at least 5 seconds" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const writeAuth = await authorizeAccountWrite(request, env, username, accountID);
    if (!writeAuth.authorized) return writeAuth.response;
    accountID = writeAuth.accountID;
    let audioBuffer;
    if (data.audioData) {
      console.log(`[ProfileMusic] Receiving audio from client (base64 length: ${data.audioData.length})`);
      const maxAudioSize = 10 * 1024 * 1024;
      if (data.audioData.length > maxAudioSize * 1.37) {
        return new Response(JSON.stringify({ error: "Audio file too large (max 10 MB)" }), {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      }
      try {
        const binaryString = atob(data.audioData);
        audioBuffer = new Uint8Array(binaryString.length);
        for (let i = 0; i < binaryString.length; i++) {
          audioBuffer[i] = binaryString.charCodeAt(i);
        }
        console.log(`[ProfileMusic] Decoded audio: ${audioBuffer.length} bytes`);
        if (audioBuffer.length < 100) {
          return new Response(JSON.stringify({ error: "Audio file too small" }), {
            status: 400,
            headers: { "Content-Type": "application/json", ...corsNoStore() }
          });
        }
        const isMp3 = audioBuffer[0] === 255 && (audioBuffer[1] & 224) === 224 || audioBuffer[0] === 73 && audioBuffer[1] === 68 && audioBuffer[2] === 51;
        const isWav = audioBuffer[0] === 82 && audioBuffer[1] === 73 && audioBuffer[2] === 70 && audioBuffer[3] === 70;
        if (!isMp3 && !isWav) {
          return new Response(JSON.stringify({ error: "Unsupported audio format (only MP3 and WAV are accepted)" }), {
            status: 400,
            headers: { "Content-Type": "application/json", ...corsNoStore() }
          });
        }
        const extension = isWav ? "wav" : "mp3";
        const altExtension = isWav ? "mp3" : "wav";
        const contentType = isWav ? "audio/wav" : "audio/mpeg";
        console.log(`[ProfileMusic] Detected format: ${extension}, size: ${audioBuffer.length} bytes`);
        // Borra el archivo del formato alterno si existe (e.g. el usuario subio
        // antes un .mp3 y ahora sube un .wav). Evita que conviva basura en R2
        // y que el fallback de lectura sirva el archivo viejo.
        try {
          await env.THUMBNAILS_BUCKET.delete(`profile-music/${accountID}.${altExtension}`);
        } catch (e) {
        }
        const audioKey = `profile-music/${accountID}.${extension}`;
        await env.THUMBNAILS_BUCKET.put(audioKey, audioBuffer, {
          httpMetadata: { contentType, cacheControl: NO_STORE_CACHE_CONTROL },
          customMetadata: {
            songID: songID.toString(),
            startMs: startMs.toString(),
            endMs: endMs.toString(),
            uploadedBy: username || "unknown",
            uploadedAt: (/* @__PURE__ */ new Date()).toISOString(),
            format: extension
          }
        });
        // Volumen: clamp a [0, 1]. Si no llega o no es numero, usar 1.0 como default.
        const safeVolume = typeof volume === "number" && isFinite(volume)
          ? Math.max(0, Math.min(1, volume))
          : 1;
        const config = {
          accountID,
          username,
          songID,
          startMs,
          endMs,
          volume: safeVolume,
          enabled: true,
          songName: songName || "",
          artistName: artistName || "",
          isCustom: !!isCustom,
          format: extension,
          updatedAt: (/* @__PURE__ */ new Date()).toISOString()
        };
        const configKey = `profile-music/${accountID}.json`;
        await putR2Json(env.SYSTEM_BUCKET, configKey, config);
        console.log(`[ProfileMusic] Uploaded ${extension} for account ${accountID}: ${audioBuffer.length} bytes`);
        invalidateProfileMusic(request, accountID, env).catch(() => {
        });
        return new Response(JSON.stringify({ success: true, message: "Profile music uploaded successfully", config }), {
          status: 200,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      } catch (e) {
        console.error(`[ProfileMusic] Failed to process audio: ${e.message}`);
        return new Response(JSON.stringify({ error: "Invalid audio data" }), {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      }
    }
    return new Response(JSON.stringify({
      error: "Audio data required",
      details: "Please download the song in GD first using the Download button"
    }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Upload profile music error:", error);
    return new Response(JSON.stringify({ error: "Failed to upload music", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleUploadProfileMusic, "handleUploadProfileMusic");
async function handleDeleteProfileMusic(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const data = await request.json();
    let { accountID } = data;
    const { username } = data;
    if (!accountID || !username) {
      return new Response(JSON.stringify({ error: "Username and Account ID required" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const writeAuth = await authorizeAccountWrite(request, env, username, accountID);
    if (!writeAuth.authorized) return writeAuth.response;
    accountID = writeAuth.accountID;
    try {
      await env.THUMBNAILS_BUCKET.delete(`profile-music/${accountID}.mp3`);
    } catch (e) {
    }
    try {
      await env.THUMBNAILS_BUCKET.delete(`profile-music/${accountID}.wav`);
    } catch (e) {
    }
    const configKey = `profile-music/${accountID}.json`;
    await env.SYSTEM_BUCKET.delete(configKey);
    console.log(`[ProfileMusic] Deleted music for account ${accountID}`);
    invalidateProfileMusic(request, accountID, env).catch(() => {
    });
    return new Response(JSON.stringify({ success: true, message: "Profile music deleted" }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Delete profile music error:", error);
    return new Response(JSON.stringify({ error: "Failed to delete music" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleDeleteProfileMusic, "handleDeleteProfileMusic");
async function handleServeProfileMusic(request, env) {
  const url2 = new URL(request.url);
  const pathParts = url2.pathname.split("/");
  const filename = pathParts[pathParts.length - 1];
  const accountID = filename.replace(/\.(mp3|wav)$/, "");
  if (!accountID) {
    return new Response("Not found", { status: 404 });
  }
  try {
    const configKey = `profile-music/${accountID}.json`;
    const config = await getR2Json(env.SYSTEM_BUCKET, configKey);
    const extension = config?.format || "mp3";
    let audioKey = `profile-music/${accountID}.${extension}`;
    let audioObj = await env.THUMBNAILS_BUCKET.get(audioKey, { skipMeta: true, cfCacheTtl: 0 });
    if (!audioObj) {
      const altExtension = extension === "mp3" ? "wav" : "mp3";
      audioKey = `profile-music/${accountID}.${altExtension}`;
      audioObj = await env.THUMBNAILS_BUCKET.get(audioKey, { skipMeta: true, cfCacheTtl: 0 });
    }
    if (!audioObj) {
      return new Response("Music not found", { status: 404, headers: corsHeaders() });
    }
    const contentType = audioKey.endsWith(".wav") ? "audio/wav" : "audio/mpeg";
    const headers = {
      "Content-Type": contentType,
      // Force cacheable — Bunny storage may return no-cache/private
      "Cache-Control": "public, s-maxage=2592000, max-age=2592000, stale-while-revalidate=300",
      ...corsHeaders()
    };
    if (config) {
      headers["X-Start-Ms"] = (config.startMs ?? 0).toString();
      headers["X-End-Ms"] = (config.endMs ?? 20000).toString();
      // Permite volumen 0 (silenciado) sin sobrescribir a 1.0.
      headers["X-Volume"] = (typeof config.volume === "number" && isFinite(config.volume)
        ? Math.max(0, Math.min(1, config.volume))
        : 1.0).toString();
    }
    const body = await audioObj.arrayBuffer();
    return new Response(body, { status: 200, headers });
  } catch (error) {
    console.error("Serve profile music error:", error);
    return new Response("Error serving music", { status: 500, headers: corsHeaders() });
  }
}
__name(handleServeProfileMusic, "handleServeProfileMusic");

// src/services/profanity.js
var PROFANITY_LIST = [
  // ── English ──
  "fuck",
  "fucker",
  "fucking",
  "fucked",
  "motherfucker",
  "shit",
  "shitty",
  "bullshit",
  "shitting",
  "bitch",
  "bitches",
  "bitching",
  "asshole",
  "arsehole",
  "bastard",
  "bastards",
  "damn",
  "damned",
  "dammit",
  "dick",
  "dickhead",
  "cunt",
  "cunts",
  "whore",
  "whores",
  "slut",
  "sluts",
  "piss",
  "pissed",
  "cock",
  "cocksucker",
  "nigger",
  "nigga",
  "niggers",
  "niggas",
  "retard",
  "retarded",
  "faggot",
  "fag",
  "faggots",
  "twat",
  "wanker",
  "crap",
  "crappy",
  // ── Spanish ──
  "puta",
  "putas",
  "putita",
  "putitas",
  "puto",
  "putos",
  "putito",
  "putazo",
  "mierda",
  "mierdas",
  "mierdero",
  "pendejo",
  "pendejos",
  "pendeja",
  "pendejas",
  "cabron",
  "cabr\xF3n",
  "cabrones",
  "cabrona",
  "chingar",
  "chingada",
  "chingado",
  "chingados",
  "chingadera",
  "verga",
  "vergudo",
  "vergota",
  "culero",
  "culera",
  "culeros",
  "culo",
  "joder",
  "jodido",
  "jodida",
  "jodete",
  "marica",
  "maricon",
  "maric\xF3n",
  "mariconada",
  "mam\xF3n",
  "mamon",
  "mamonazo",
  "huevon",
  "huev\xF3n",
  "g\xFCey",
  "pinche",
  "pinches",
  "co\xF1o",
  "cono",
  "zorra",
  "zorras",
  "idiota",
  "idiotas",
  "imbecil",
  "imb\xE9cil",
  "imbeciles",
  "estupido",
  "est\xFApido",
  "estupida",
  "est\xFApida",
  "malparido",
  "malparida",
  "hijueputa",
  "hijodeputa",
  "hijaputa",
  "gonorrea",
  "baboso",
  "babosa",
  "tarado",
  "tarada",
  "boludo",
  "boluda",
  "pelotudo",
  "pelotuda",
  "forro",
  "forra",
  "negro de mierda",
  "negra de mierda",
  "subnormal"
];
var WORD_SET = new Set(PROFANITY_LIST.map((w) => w.toLowerCase()));
var sorted = [...PROFANITY_LIST].sort((a, b) => b.length - a.length);
var escaped = sorted.map((w) => w.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"));
var PROFANITY_REGEX = new RegExp(`\\b(${escaped.join("|")})\\b`, "gi");
function censorWord(word) {
  if (word.length <= 2) return "*".repeat(word.length);
  return word[0] + "*".repeat(word.length - 2) + word[word.length - 1];
}
__name(censorWord, "censorWord");
function censorText(text) {
  if (!text || typeof text !== "string") return text || "";
  return text.replace(PROFANITY_REGEX, (match) => censorWord(match));
}
__name(censorText, "censorText");

// src/controllers/ratings.js
function isValidLevelID(id) {
  return /^\d{1,15}$/.test(String(id));
}
__name(isValidLevelID, "isValidLevelID");
async function resolveUploadedBy(env, levelID) {
  const cacheKey = `uploader_${levelID}`;
  const cached = memCache.get(cacheKey);
  if (cached !== void 0) return cached === "__none__" ? null : cached;
  const acceptedKeys = await listR2Keys(
    env.SYSTEM_BUCKET,
    `data/history/accepted/${levelID}`
  );
  if (acceptedKeys.length > 0) {
    const results = await Promise.all(
      acceptedKeys.map((k) => getR2Json(env.SYSTEM_BUCKET, k))
    );
    for (const aData of results) {
      if (aData) {
        const uploader = aData.originalSubmission?.submittedBy || aData.submittedBy || "";
        const accID = aData.originalSubmission?.accountID || aData.accountID || 0;
        if (uploader && uploader !== "Unknown") {
          const result = {
            uploadedBy: uploader,
            accountID: accID ? parseInt(accID) : 0
          };
          memCache.set(cacheKey, result, 6e5);
          return result;
        }
      }
    }
  }
  const [suggestionsData, updatesData] = await Promise.all([
    getR2Json(env.SYSTEM_BUCKET, `data/queue/suggestions/${levelID}.json`),
    getR2Json(env.SYSTEM_BUCKET, `data/queue/updates/${levelID}.json`)
  ]);
  for (const qData of [suggestionsData, updatesData]) {
    if (qData) {
      const items = Array.isArray(qData) ? qData : [qData];
      for (const item of items) {
        const uploader = item.submittedBy || item.uploadedBy || "";
        const accID = item.accountID || 0;
        if (uploader && uploader !== "Unknown" && uploader !== "System") {
          const result = {
            uploadedBy: uploader,
            accountID: accID ? parseInt(accID) : 0
          };
          memCache.set(cacheKey, result, 6e5);
          return result;
        }
      }
    }
  }
  memCache.set(cacheKey, "__none__", 12e4);
  return null;
}
__name(resolveUploadedBy, "resolveUploadedBy");
async function handleProfileRatingVote(request, env) {
  if (request.method !== "POST")
    return new Response("Method not allowed", { status: 405 });
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: {
        "Content-Type": "application/json",
        ...corsNoStore(request.headers.get("Origin"))
      }
    });
  }
  try {
    let body;
    try {
      body = await request.json();
    } catch {
      return new Response(JSON.stringify({ error: "Invalid JSON body" }), { status: 400, headers: { "Content-Type": "application/json", ...corsNoStore(request.headers.get("Origin")) } });
    }
    const { accountID, stars, username, message } = body;
    if (!accountID || !stars || !username) {
      return new Response(JSON.stringify({ error: "Missing fields" }), {
        status: 400,
        headers: {
          "Content-Type": "application/json",
          ...corsNoStore(request.headers.get("Origin"))
        }
      });
    }
    // Acepta ratings en pasos de 0.5 (0.5, 1.0, 1.5, ..., 5.0). Normaliza al
    // paso mas cercano para rechazar floats arbitrarios como 2.34.
    const ratingNum = Number(stars);
    if (!Number.isFinite(ratingNum) || ratingNum < 0.5 || ratingNum > 5) {
      return new Response(JSON.stringify({ error: "Stars must be 0.5-5" }), {
        status: 400,
        headers: {
          "Content-Type": "application/json",
          ...corsNoStore(request.headers.get("Origin"))
        }
      });
    }
    const normalizedStars = Math.round(ratingNum * 2) / 2;
    const banData = memCache.get("system_banlist") ?? await (async () => {
      const d = await getR2Json(env.SYSTEM_BUCKET, "data/banlist.json");
      memCache.set("system_banlist", d, 3e5);
      return d;
    })();
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (banned.includes(username.toLowerCase())) {
      return new Response(JSON.stringify({ error: "User is banned" }), {
        status: 403,
        headers: {
          "Content-Type": "application/json",
          ...corsNoStore(request.headers.get("Origin"))
        }
      });
    }
    const key = `profile-ratings/${accountID.toString()}.json`;
    let data = await getR2Json(env.SYSTEM_BUCKET, key) || {
      total: 0,
      count: 0,
      votes: {}
    };
    const userLower = username.toLowerCase();
    const previousVote = data.votes?.[userLower];
    if (previousVote) {
      data.total = (data.total || 0) - previousVote.stars + normalizedStars;
    } else {
      data.total = (data.total || 0) + normalizedStars;
      data.count = (data.count || 0) + 1;
    }
    data.votes = data.votes || {};
    data.votes[userLower] = {
      stars: normalizedStars,
      message: censorText((message || "").substring(0, 150)),
      timestamp: Date.now()
    };
    await putR2Json(env.SYSTEM_BUCKET, key, data);
    invalidateProfileRating(request, accountID.toString(), env).catch(() => {
    });
    const average = data.count > 0 ? data.total / data.count : 0;
    return new Response(
      JSON.stringify({
        success: true,
        average: Math.round(average * 100) / 100,
        count: data.count,
        updated: !!previousVote
      }),
      {
        headers: {
          "Content-Type": "application/json",
          ...corsNoStore(request.headers.get("Origin"))
        }
      }
    );
  } catch (e) {
    console.error("[Ratings] handleProfileRatingVote error:", e);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: {
        "Content-Type": "application/json",
        ...corsNoStore(request.headers.get("Origin"))
      }
    });
  }
}
__name(handleProfileRatingVote, "handleProfileRatingVote");
async function handleGetProfileRating(request, env) {
  const url2 = new URL(request.url);
  const accountID = url2.pathname.split("/").pop();
  const username = url2.searchParams.get("username");
  const key = `profile-ratings/${accountID}.json`;

  // memCache for profile ratings (invalidated on vote)
  const memKey = `profile_rating_${accountID}`;
  let data = memCache.get(memKey);
  if (data === void 0) {
    data = await getR2Json(env.SYSTEM_BUCKET, key) || { total: 0, count: 0, votes: {} };
    memCache.set(memKey, data, 3e5);
  }

  const average = data.count > 0 ? data.total / data.count : 0;
  const userVote = username && data.votes?.[username.toLowerCase()] ? data.votes[username.toLowerCase()] : null;
  const reviews = [];
  if (data.votes) {
    for (const [user, vote] of Object.entries(data.votes)) {
      if (vote.message) {
        reviews.push({
          username: user,
          stars: vote.stars,
          message: vote.message,
          timestamp: vote.timestamp || 0
        });
      }
    }
    reviews.sort((a, b) => b.timestamp - a.timestamp);
  }
  return new Response(
    JSON.stringify({
      average: Math.round(average * 100) / 100,
      count: data.count,
      userVote: userVote ? { stars: userVote.stars, message: userVote.message || "" } : null,
      reviews: reviews.slice(0, 20)
    }),
    {
      headers: {
        "Content-Type": "application/json",
        ...corsHeaders(request.headers.get("Origin"))
      }
    }
  );
}
__name(handleGetProfileRating, "handleGetProfileRating");
async function handleVoteV2(request, env) {
  if (request.method !== "POST")
    return new Response("Method not allowed", { status: 405 });
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: {
        "Content-Type": "application/json",
        ...corsNoStore(request.headers.get("Origin"))
      }
    });
  }
  try {
    let body;
    try {
      body = await request.json();
    } catch {
      return new Response(JSON.stringify({ error: "Invalid JSON body" }), { status: 400, headers: { "Content-Type": "application/json" } });
    }
    const { levelID, stars, username, thumbnailId, accountID, isOfficialServer } = body;
    if (!levelID || !stars || !username)
      return new Response("Missing fields", { status: 400 });
    if (!isValidLevelID(levelID))
      return new Response("Invalid levelID", { status: 400 });
    if (stars < 1 || stars > 5)
      return new Response("Invalid stars", { status: 400 });
    const accountVerification = await verifyAccountForWrite(env, accountID, username);
    if (!accountVerification.valid) {
      const errorMessages = {
        ACCOUNT_REQUIRED: "Valid accountID required to vote",
        UNOFFICIAL_SERVER: "Voting requires official Boomlings server connection",
        ACCOUNT_NOT_FOUND: "Account not found on official servers",
        ACCOUNT_MISMATCH: "Account verification failed",
        USERNAME_MISMATCH: "Username does not match account",
        VERIFY_FAILED: "Account verification service unavailable"
      };
      return new Response(
        JSON.stringify({ error: errorMessages[accountVerification.reason] || "Account verification failed", code: accountVerification.reason }),
        { status: 403, headers: { "Content-Type": "application/json", ...corsNoStore(request.headers.get("Origin")) } }
      );
    }
    const voterAccountID = accountVerification.accountID;
    const levelStr = levelID.toString();
    let key = `ratings-v2/${levelStr}.json`;
    if (thumbnailId) key = `ratings-v2/${levelStr}_${thumbnailId}.json`;
    let data = await getR2Json(env.SYSTEM_BUCKET, key) || {
      total: 0,
      count: 0,
      votes: {}
    };
    const userLower = String(username).toLowerCase();
    const voteKey = `${userLower}_${voterAccountID}`;
    if (data.votes && data.votes[voteKey]) {
      return new Response(
        JSON.stringify({ success: false, message: "Already voted" }),
        {
          status: 400,
          headers: {
            "Content-Type": "application/json",
            ...corsNoStore(request.headers.get("Origin"))
          }
        }
      );
    }
    if (!data.uploadedBy) {
      try {
        const result = await resolveUploadedBy(env, levelStr);
        if (result) {
          data.uploadedBy = result.uploadedBy;
          if (result.accountID) data.accountID = result.accountID;
        }
      } catch (e) {
        console.warn("Failed to fetch uploadedBy (v2)", e);
      }
    }
    data.votes = data.votes || {};
    data.votes[voteKey] = stars;
    data.total = (data.total || 0) + stars;
    data.count = (data.count || 0) + 1;
    const currentAverage = data.total / data.count;
    if (currentAverage <= 3) {
      await putR2Json(
        env.SYSTEM_BUCKET,
        `data/queue/updates/${levelStr}.json`,
        {
          levelId: parseInt(levelStr),
          category: "verify",
          submittedBy: "System",
          timestamp: Date.now(),
          status: "pending",
          note: `Low rating detected: ${currentAverage.toFixed(2)} stars`,
          average: currentAverage
        }
      );
    }
    const today = (/* @__PURE__ */ new Date()).toISOString().split("T")[0];
    if (!data.daily || data.daily.date !== today)
      data.daily = { date: today, total: 0, count: 0 };
    data.daily.total += stars;
    data.daily.count += 1;
    const thisWeek = getWeekNumber(/* @__PURE__ */ new Date());
    if (!data.weekly || data.weekly.week !== thisWeek)
      data.weekly = { week: thisWeek, total: 0, count: 0 };
    data.weekly.total += stars;
    data.weekly.count += 1;
    await putR2Json(env.SYSTEM_BUCKET, key, data);
    const uploaderAccountID = data.accountID || 0;
    await Promise.all([
      updateLeaderboard(
        env,
        "daily",
        levelStr,
        data.daily,
        data.uploadedBy,
        uploaderAccountID
      ),
      updateLeaderboard(
        env,
        "weekly",
        levelStr,
        data.weekly,
        data.uploadedBy,
        uploaderAccountID
      )
    ]);
    const v2CachePromises = [
      updateTopThumbnailsCache(
        env,
        levelStr,
        { total: data.total, count: data.count },
        data.uploadedBy,
        data.accountID
      )
    ];
    if (data.uploadedBy && data.uploadedBy !== "Unknown") {
      v2CachePromises.push(
        updateCreatorLeaderboardCache(env, data.uploadedBy, {
          addRating: stars,
          accountID: data.accountID
        })
      );
    }
    await Promise.all(v2CachePromises);
    invalidateRating(request, levelStr, env).catch(() => {
    });
    return new Response(
      JSON.stringify({
        success: true,
        average: data.total / data.count,
        count: data.count
      }),
      {
        headers: {
          "Content-Type": "application/json",
          ...corsNoStore(request.headers.get("Origin"))
        }
      }
    );
  } catch (e) {
    console.error("[Ratings] handleVoteV2 error:", e);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500
    });
  }
}
__name(handleVoteV2, "handleVoteV2");
async function handleGetRatingV2(request, env) {
  const url2 = new URL(request.url);
  const levelID = url2.pathname.split("/").pop();
  if (!isValidLevelID(levelID)) {
    return new Response(JSON.stringify({ average: 0, count: 0, userVote: 0 }), {
      headers: {
        "Content-Type": "application/json",
        ...corsHeaders(request.headers.get("Origin"))
      }
    });
  }
  const username = url2.searchParams.get("username");
  const thumbnailId = url2.searchParams.get("thumbnailId");

  // Try memCache first for hot ratings
  const ratingMemKey = `rating_v2_${levelID}${thumbnailId ? '_' + thumbnailId : ''}`;
  const cachedRating = memCache.get(ratingMemKey);
  if (cachedRating !== void 0) {
    const average = cachedRating.count > 0 ? cachedRating.total / cachedRating.count : 0;
    const userVote = username && cachedRating.votes && cachedRating.votes[username] ? cachedRating.votes[username] : 0;
    return new Response(
      JSON.stringify({ average, count: cachedRating.count, userVote }),
      { headers: { "Content-Type": "application/json", ...corsHeaders(request.headers.get("Origin")) } }
    );
  }

  // Build candidate keys and try them in parallel (not sequential)
  const candidates = [];
  if (thumbnailId) candidates.push(`ratings-v2/${levelID}_${thumbnailId}.json`);
  candidates.push(`ratings-v2/${levelID}.json`);
  if (thumbnailId) candidates.push(`ratings/${levelID}_${thumbnailId}.json`);
  candidates.push(`ratings/${levelID}.json`);

  // Parallel fetch — first non-null wins
  const results = await Promise.all(candidates.map((key) => getR2Json(env.SYSTEM_BUCKET, key)));
  let data = null;
  for (const r of results) {
    if (r) { data = r; break; }
  }
  if (!data) data = { total: 0, count: 0, votes: {} };

  // Cache for 2 minutes
  memCache.set(ratingMemKey, data, 12e4);

  const average = data.count > 0 ? data.total / data.count : 0;
  const userVote = username && data.votes && data.votes[username] ? data.votes[username] : 0;
  return new Response(
    JSON.stringify({ average, count: data.count, userVote }),
    {
      headers: {
        "Content-Type": "application/json",
        ...corsHeaders(request.headers.get("Origin"))
      }
    }
  );
}
__name(handleGetRatingV2, "handleGetRatingV2");

// src/controllers/featured.js
async function handleSetDailyLevel(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    let body;
    try {
      body = await request.json();
    } catch {
      return new Response(JSON.stringify({ error: "Invalid JSON body" }), { status: 400, headers: { "Content-Type": "application/json", ...corsNoStore() } });
    }
    const { levelID, username, accountID } = body;
    if (!levelID || !username) {
      return new Response(
        JSON.stringify({ error: "Missing required fields" }),
        {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    const authResult = await verifyModAuth(
      request,
      env,
      username,
      accountID || 0
    );
    if (!authResult.authorized) {
      return new Response(JSON.stringify({ error: "Not authorized" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const now = Date.now();
    const expiresAt = now + 24 * 60 * 60 * 1e3;
    const data = {
      levelID: parseInt(levelID),
      setAt: now,
      expiresAt,
      setBy: username
    };
    await putR2Json(env.SYSTEM_BUCKET, "data/daily/current.json", data);
    putR2Json(env.SYSTEM_BUCKET, "public/api/daily/current.json", { success: true, data }).catch(() => {
    });
    memCache.invalidate("featured_daily");
    invalidateFeatured(request, env).catch(() => {
    });
    dispatchWebhook(env, "daily", data).catch(() => {
    });
    const origin = new URL(request.url).origin;
    const wtReq = cfCacheKey(new Request(`${origin}/api/daily/current`));
    const wtResp = makeCacheable(
      new Response(JSON.stringify({ success: true, data }), {
        headers: {
          "Content-Type": "application/json",
          "Access-Control-Allow-Origin": "*"
        }
      }),
      3600
    );
    cfCachePut(wtReq, wtResp).catch(() => {
    });
    memCache.set("featured_daily", data, 3e5);
    const historyKey = `data/daily/history/${(/* @__PURE__ */ new Date()).toISOString().split("T")[0]}.json`;
    await putR2Json(env.SYSTEM_BUCKET, historyKey, data);
    return new Response(JSON.stringify({ success: true, data }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("[Featured] handleSetDailyLevel error:", error);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleSetDailyLevel, "handleSetDailyLevel");
async function handleGetDailyLevel(request, env) {
  try {
    const memKey = "featured_daily";
    let data = memCache.get(memKey);
    if (data === void 0) {
      data = await getR2Json(env.SYSTEM_BUCKET, "data/daily/current.json");
      if (data) memCache.set(memKey, data, 3e5);
    }
    if (!data) {
      return new Response(JSON.stringify({ error: "No daily level set" }), {
        status: 404,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    if (Date.now() > data.expiresAt) {
      return new Response(
        JSON.stringify({ error: "Daily level expired", expired: true }),
        {
          status: 404,
          headers: { "Content-Type": "application/json", ...corsHeaders() }
        }
      );
    }
    return new Response(JSON.stringify({ success: true, data }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    console.error("[Featured] handleGetDailyLevel error:", error);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleGetDailyLevel, "handleGetDailyLevel");
async function handleSetWeeklyLevel(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    let body;
    try {
      body = await request.json();
    } catch {
      return new Response(JSON.stringify({ error: "Invalid JSON body" }), { status: 400, headers: { "Content-Type": "application/json", ...corsNoStore() } });
    }
    const { levelID, username, accountID } = body;
    if (!levelID || !username) {
      return new Response(
        JSON.stringify({ error: "Missing required fields" }),
        {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        }
      );
    }
    const authResult = await verifyModAuth(
      request,
      env,
      username,
      accountID || 0
    );
    if (!authResult.authorized) {
      return new Response(JSON.stringify({ error: "Not authorized" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const now = Date.now();
    const expiresAt = now + 7 * 24 * 60 * 60 * 1e3;
    const data = {
      levelID: parseInt(levelID),
      setAt: now,
      expiresAt,
      setBy: username
    };
    await putR2Json(env.SYSTEM_BUCKET, "data/weekly/current.json", data);
    putR2Json(env.SYSTEM_BUCKET, "public/api/weekly/current.json", { success: true, data }).catch(() => {
    });
    memCache.invalidate("featured_weekly");
    invalidateFeatured(request, env).catch(() => {
    });
    dispatchWebhook(env, "weekly", data).catch(() => {
    });
    const origin = new URL(request.url).origin;
    const wtReq = cfCacheKey(new Request(`${origin}/api/weekly/current`));
    const wtResp = makeCacheable(
      new Response(JSON.stringify({ success: true, data }), {
        headers: {
          "Content-Type": "application/json",
          "Access-Control-Allow-Origin": "*"
        }
      }),
      3600
    );
    cfCachePut(wtReq, wtResp).catch(() => {
    });
    memCache.set("featured_weekly", data, 3e5);
    const historyKey = `data/weekly/history/${getWeekNumber(/* @__PURE__ */ new Date())}.json`;
    await putR2Json(env.SYSTEM_BUCKET, historyKey, data);
    return new Response(JSON.stringify({ success: true, data }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("[Featured] handleSetWeeklyLevel error:", error);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleSetWeeklyLevel, "handleSetWeeklyLevel");
async function handleGetWeeklyLevel(request, env) {
  try {
    const memKey = "featured_weekly";
    let data = memCache.get(memKey);
    if (data === void 0) {
      data = await getR2Json(env.SYSTEM_BUCKET, "data/weekly/current.json");
      if (data) memCache.set(memKey, data, 3e5);
    }
    if (!data) {
      return new Response(JSON.stringify({ error: "No weekly level set" }), {
        status: 404,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    if (Date.now() > data.expiresAt) {
      return new Response(
        JSON.stringify({ error: "Weekly level expired", expired: true }),
        {
          status: 404,
          headers: { "Content-Type": "application/json", ...corsHeaders() }
        }
      );
    }
    return new Response(JSON.stringify({ success: true, data }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    console.error("[Featured] handleGetWeeklyLevel error:", error);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleGetWeeklyLevel, "handleGetWeeklyLevel");
async function handleGetDailyWeeklyHistory(request, env) {
  try {
    const url2 = new URL(request.url);
    const type = url2.searchParams.get("type") || "daily";
    const limit2 = parseInt(url2.searchParams.get("limit")) || 50;
    if (type !== "daily" && type !== "weekly") {
      return new Response(
        JSON.stringify({ error: "Invalid type, must be daily or weekly" }),
        {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsHeaders() }
        }
      );
    }
    const prefix = `data/${type}/history/`;
    const keys = await listR2Keys(env.SYSTEM_BUCKET, prefix);
    const items = [];
    const slice = keys.slice(-limit2).reverse();
    const dataResults = await Promise.all(
      slice.map((key) => getR2Json(env.SYSTEM_BUCKET, key))
    );
    for (const data of dataResults) {
      if (data) items.push(data);
    }
    items.sort((a, b) => (b.setAt || 0) - (a.setAt || 0));
    return new Response(
      JSON.stringify({ success: true, type, count: items.length, items }),
      {
        status: 200,
        headers: {
          "Content-Type": "application/json",
          "Cache-Control": "public, max-age=1209600",
          ...corsHeaders()
        }
      }
    );
  } catch (error) {
    console.error("Get daily/weekly history error:", error);
    return new Response(
      JSON.stringify({
        error: "Failed to get history",
        details: error.message
      }),
      {
        status: 500,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      }
    );
  }
}
__name(handleGetDailyWeeklyHistory, "handleGetDailyWeeklyHistory");
async function handleGetLeaderboard(request, env) {
  const url2 = new URL(request.url);
  const type = url2.searchParams.get("type") || "daily";
  const date = /* @__PURE__ */ new Date();
  let key;
  if (type === "daily")
    key = `data/leaderboards/daily/${date.toISOString().split("T")[0]}.json`;
  else if (type === "weekly")
    key = `data/leaderboards/weekly/${getWeekNumber(date)}.json`;
  else
    return new Response(JSON.stringify({ error: "Invalid type" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  const memKey = `leaderboard:${key}`;
  let data = memCache.get(memKey);
  if (data === void 0) {
    data = await getR2Json(env.SYSTEM_BUCKET, key) || [];
    memCache.set(memKey, data, 12e4);
  }
  return new Response(JSON.stringify({ success: true, type, data }), {
    status: 200,
    headers: { "Content-Type": "application/json", ...corsHeaders() }
  });
}
__name(handleGetLeaderboard, "handleGetLeaderboard");
async function handleGetTopCreators(request, env) {
  try {
    const url2 = new URL(request.url);
    const page2 = parseInt(url2.searchParams.get("page") || "0");
    const limit2 = parseInt(url2.searchParams.get("limit") || "20");
    const cacheKey = "top_creators";
    let cache = memCache.get(cacheKey);
    if (cache === void 0) {
      cache = await getR2Json(
        env.SYSTEM_BUCKET,
        "data/system/creator_leaderboard.json"
      ) || [];
      memCache.set(cacheKey, cache, 72e5);
    }
    const start = page2 * limit2;
    const slice = cache.slice(start, start + limit2);
    return new Response(
      JSON.stringify({
        success: true,
        total: cache.length,
        page: page2,
        creators: slice
      }),
      {
        headers: {
          "Content-Type": "application/json",
          ...corsHeaders(request.headers.get("Origin"))
        }
      }
    );
  } catch (e) {
    if (e instanceof StorageError) {
      const cache = memCache.get("top_creators");
      if (cache !== void 0) {
        console.warn("[Degradation] Serving cached top creators");
        const start = page * limit;
        const slice = cache.slice(start, start + limit);
        return new Response(
          JSON.stringify({ success: true, total: cache.length, page, creators: slice, _degraded: true }),
          { headers: { "Content-Type": "application/json", ...corsHeaders(request.headers.get("Origin")) } }
        );
      }
      return new Response(
        JSON.stringify({ error: "Storage temporarily unavailable", code: "STORAGE_ERROR", retryable: true }),
        { status: 502, headers: { "Content-Type": "application/json", "Retry-After": "5", ...corsHeaders(request.headers.get("Origin")) } }
      );
    }
    return new Response(
      JSON.stringify({ success: false, message: e.message }),
      {
        status: 500,
        headers: {
          "Content-Type": "application/json",
          ...corsHeaders(request.headers.get("Origin"))
        }
      }
    );
  }
}
__name(handleGetTopCreators, "handleGetTopCreators");
async function handleGetTopThumbnails(request, env) {
  try {
    const url2 = new URL(request.url);
    const page2 = parseInt(url2.searchParams.get("page") || "0");
    const limit2 = parseInt(url2.searchParams.get("limit") || "20");
    const cacheKey = "top_thumbnails";
    let cache = memCache.get(cacheKey);
    if (cache === void 0) {
      cache = await getR2Json(
        env.SYSTEM_BUCKET,
        "data/system/top_thumbnails.json"
      ) || [];
      memCache.set(cacheKey, cache, 72e5);
    }
    const start = page2 * limit2;
    const slice = cache.slice(start, start + limit2);
    return new Response(
      JSON.stringify({
        success: true,
        total: cache.length,
        page: page2,
        thumbnails: slice
      }),
      {
        headers: {
          "Content-Type": "application/json",
          ...corsHeaders(request.headers.get("Origin"))
        }
      }
    );
  } catch (e) {
    if (e instanceof StorageError) {
      const cache = memCache.get("top_thumbnails");
      if (cache !== void 0) {
        console.warn("[Degradation] Serving cached top thumbnails");
        const start = page * limit;
        const slice = cache.slice(start, start + limit);
        return new Response(
          JSON.stringify({ success: true, total: cache.length, page, thumbnails: slice, _degraded: true }),
          { headers: { "Content-Type": "application/json", ...corsHeaders(request.headers.get("Origin")) } }
        );
      }
      return new Response(
        JSON.stringify({ error: "Storage temporarily unavailable", code: "STORAGE_ERROR", retryable: true }),
        { status: 502, headers: { "Content-Type": "application/json", "Retry-After": "5", ...corsHeaders(request.headers.get("Origin")) } }
      );
    }
    return new Response(
      JSON.stringify({ success: false, message: e.message }),
      {
        status: 500,
        headers: {
          "Content-Type": "application/json",
          ...corsHeaders(request.headers.get("Origin"))
        }
      }
    );
  }
}
__name(handleGetTopThumbnails, "handleGetTopThumbnails");

// src/controllers/queue.js

// ── /api/queue/summary — All queue categories counts + first items in one request ──
async function handleQueueSummary(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const url2 = new URL(request.url);
  const queueUsername = url2.searchParams.get("username") || "";
  const queueAccountID = parseInt(url2.searchParams.get("accountID") || "0");
  const auth = await verifyModAuth(request, env, queueUsername, queueAccountID);
  if (!auth.authorized) {
    return modAuthForbiddenResponse(auth);
  }
  if (!await isModeratorOrAdmin(env, queueUsername)) {
    return new Response(JSON.stringify({ error: "Moderator/Admin privileges required" }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }

  const previewLimit = parseInt(url2.searchParams.get("preview") || "3");

  try {
    // Fetch all queue categories in parallel
    const categories = [
      { name: "verify", prefixes: ["data/queue/suggestions/", "data/queue/thumbnails/"] },
      { name: "update", prefixes: ["data/queue/updates/"] },
      { name: "report", prefixes: ["data/queue/reports/"] },
      { name: "profileimgs", prefixes: ["data/queue/profileimgs/"] },
      { name: "profilebackground", prefixes: ["data/queue/profilebackground/"] }
    ];

    const results = await Promise.all(categories.map(async (cat) => {
      let allKeys = [];
      for (const prefix of cat.prefixes) {
        const keys = await listR2Keys(env.SYSTEM_BUCKET, prefix);
        allKeys = allKeys.concat(keys);
      }
      const count = allKeys.length;

      // Load preview items (first N)
      const previewKeys = allKeys.slice(0, previewLimit);
      const previewData = await Promise.all(previewKeys.map((key) => getR2Json(env.SYSTEM_BUCKET, key)));
      const items = previewData.filter(Boolean).map((data) => {
        if (Array.isArray(data) && data.length > 0) {
          const first = data[0];
          return {
            levelId: first.levelId,
            category: first.category || cat.name,
            submittedBy: first.submittedBy,
            timestamp: data[data.length - 1].timestamp || first.timestamp,
            status: first.status || "pending",
            accountID: first.accountID
          };
        }
        return data;
      });

      return { category: cat.name, count, preview: items };
    }));

    const summary = {};
    let totalPending = 0;
    for (const r of results) {
      summary[r.category] = { count: r.count, preview: r.preview };
      totalPending += r.count;
    }

    return new Response(JSON.stringify({ success: true, totalPending, queues: summary }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("[Queue] handleQueueSummary error:", error);
    return new Response(JSON.stringify({ error: "Failed to get queue summary" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleQueueSummary, "handleQueueSummary");

async function handleGetQueue(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const url2 = new URL(request.url);
  const queueUsername = url2.searchParams.get("username") || "";
  const queueAccountID = parseInt(url2.searchParams.get("accountID") || "0");
  const auth = await verifyModAuth(request, env, queueUsername, queueAccountID);
  if (!auth.authorized) {
    console.log(`[Security] Get queue blocked: ${queueUsername || "(no username)"}`);
    return modAuthForbiddenResponse(auth);
  }
  if (!await isModeratorOrAdmin(env, queueUsername)) {
    return new Response(JSON.stringify({ error: "Moderator/Admin privileges required" }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const category = url2.pathname.split("/").pop();
  try {
    let prefixes = [];
    if (category === "verify") {
      prefixes = ["data/queue/suggestions/", "data/queue/thumbnails/"];
    } else if (category === "update") {
      prefixes = ["data/queue/updates/"];
    } else if (category === "report") {
      prefixes = ["data/queue/reports/"];
    } else if (category === "profileimgs") {
      prefixes = ["data/queue/profileimgs/"];
    } else if (category === "profilebackground") {
      prefixes = ["data/queue/profilebackground/"];
    } else {
      prefixes = [`data/queue/${category}/`];
    }
    const keys = [];
    for (const prefix of prefixes) {
      const prefixKeys = await listR2Keys(env.SYSTEM_BUCKET, prefix);
      keys.push(...prefixKeys);
    }
    const items = [];
    const dataResults = await Promise.all(keys.map((key) => getR2Json(env.SYSTEM_BUCKET, key)));
    for (const data of dataResults) {
      if (data) {
        if (Array.isArray(data) && data.length > 0) {
          const first = data[0];
          items.push({
            levelId: first.levelId,
            category: first.category || category,
            submittedBy: first.submittedBy,
            timestamp: data[data.length - 1].timestamp || first.timestamp,
            status: first.status || "pending",
            note: first.note,
            accountID: first.accountID,
            suggestions: data
          });
        } else {
          items.push(data);
        }
      }
    }
    items.sort((a, b) => {
      const timeA = Array.isArray(a) ? a[a.length - 1]?.timestamp || 0 : a.timestamp || 0;
      const timeB = Array.isArray(b) ? b[b.length - 1]?.timestamp || 0 : b.timestamp || 0;
      return timeB - timeA;
    });
    return new Response(JSON.stringify({ success: true, items }), {
      status: 200,
      headers: { "Content-Type": "application/json", "Surrogate-Control": "no-store", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Get queue error:", error);
    return new Response(JSON.stringify({ error: "Failed to get queue", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", "Surrogate-Control": "no-store", ...corsNoStore() }
    });
  }
}
__name(handleGetQueue, "handleGetQueue");
async function handleAcceptQueue(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { levelId, category, username } = body;
    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) {
      console.log(`[Security] Accept queue blocked: ${username}`);
      return modAuthForbiddenResponse(auth);
    }
    if (!await isModeratorOrAdmin(env, username)) {
      return new Response(JSON.stringify({ error: "Moderator/Admin privileges required" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (!levelId || !category) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    let queueFolder = category;
    let sourceFolder = "";
    if (category === "verify") {
      queueFolder = "suggestions";
      sourceFolder = "suggestions";
    } else if (category === "update") {
      queueFolder = "updates";
      sourceFolder = "updates";
    } else if (category === "report") {
      queueFolder = "reports";
    } else if (category === "profileimg") {
      queueFolder = "profileimgs";
    } else if (category === "profilebackground") {
      queueFolder = "profilebackground";
    } else if (category === "thumbnails") {
      queueFolder = "thumbnails";
    }
    if (category === "report" && body.type === "user") {
      const userQueueKey = `data/queue/reports/user_${levelId}.json`;
      const userReportData = await getR2Json(env.SYSTEM_BUCKET, userQueueKey);
      if (!userReportData) {
        return new Response(JSON.stringify({ error: "User report not found" }), {
          status: 404,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      }
      const targetUsername = (userReportData.reportedUsername || "").toLowerCase();
      if (!targetUsername) {
        return new Response(JSON.stringify({ error: "No reported username found" }), {
          status: 400,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      }
      const banData = await getR2Json(env.SYSTEM_BUCKET, "data/banlist.json") || { banned: [], details: {} };
      const banned = Array.isArray(banData.banned) ? banData.banned : [];
      const details = banData.details || {};
      if (!banned.includes(targetUsername)) banned.push(targetUsername);
      details[targetUsername] = {
        reason: `Banned via user reports (${userReportData.reports?.length || 0} reports)`,
        bannedBy: username || "Unknown",
        timestamp: Date.now(),
        date: (/* @__PURE__ */ new Date()).toISOString()
      };
      await putR2Json(env.SYSTEM_BUCKET, "data/banlist.json", { banned, details });
      await env.SYSTEM_BUCKET.delete(userQueueKey);
      logAudit(env.SYSTEM_BUCKET, "user_ban_from_reports", {
        targetUsername,
        reportCount: userReportData.reports?.length,
        bannedBy: username
      }, ctx);
      return new Response(JSON.stringify({ success: true, message: `User ${targetUsername} has been banned` }), {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    let queueKey = `data/queue/${queueFolder}/${levelId}.json`;
    let queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);
    if (!queueData && category === "verify") {
      queueKey = `data/queue/thumbnails/${levelId}.json`;
      queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);
      if (queueData) {
        queueFolder = "thumbnails";
        sourceFolder = "";
      }
    }
    const queueSubmitter = Array.isArray(queueData) ? queueData[0]?.submittedBy || "unknown" : queueData?.submittedBy || "unknown";
    const queueAccountID = Array.isArray(queueData) ? queueData[0]?.accountID || 0 : queueData?.accountID || 0;
    if (category === "thumbnails" || queueFolder === "thumbnails") {
      let pendingKey = null;
      if (queueData && queueData.filename) {
        pendingKey = queueData.filename;
      } else if (Array.isArray(queueData) && queueData[0]?.filename) {
        pendingKey = queueData[0].filename;
      }
      if (!pendingKey) {
        const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_thumbnails/${levelId}_` });
        if (pendingList.objects.length === 0) {
          await env.SYSTEM_BUCKET.delete(queueKey);
          return new Response(JSON.stringify({ error: "No pending thumbnail found" }), {
            status: 404,
            headers: { "Content-Type": "application/json", ...corsNoStore() }
          });
        }
        const sorted2 = pendingList.objects.sort((a, b) => {
          const getTs = /* @__PURE__ */ __name((k) => {
            const m = k.key.match(/_(\d+)\./);
            return m ? parseInt(m[1]) : 0;
          }, "getTs");
          return getTs(b) - getTs(a);
        });
        pendingKey = sorted2[0].key;
      }
      const pendingObj = await env.THUMBNAILS_BUCKET.get(pendingKey, { skipMeta: true });
      if (!pendingObj) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: "Failed to read pending thumbnail" }), {
          status: 500,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      }
      const pendingBuffer = await pendingObj.arrayBuffer();
      const realMime = detectMimeType(new Uint8Array(pendingBuffer)) || pendingObj.httpMetadata?.contentType || "image/webp";
      const pendingSecReject = rejectIfMalicious(new Uint8Array(pendingBuffer), realMime);
      if (pendingSecReject) return pendingSecReject;
      const ext = pendingKey.split(".").pop() || "webp";
      const ts = Date.now().toString();
      const destKey = `thumbnails/${levelId}_${ts}.${ext}`;
      await env.THUMBNAILS_BUCKET.put(destKey, pendingBuffer, {
        httpMetadata: { contentType: realMime, cacheControl: "public, max-age=604800" },
        customMetadata: {
          uploadedBy: queueSubmitter,
          updated_by: queueSubmitter,
          uploadedAt: (/* @__PURE__ */ new Date()).toISOString(),
          originalSubmitter: queueSubmitter,
          version: ts,
          status: "accepted"
        }
      });
      // Use VersionManager.appendVersion so existing thumbnails for the level
      // are preserved instead of being overwritten by a single-entry array.
      const queueVersionManager = new VersionManager(env.SYSTEM_BUCKET);
      const queueAppendRes = await queueVersionManager.appendVersion(
        levelId,
        ts,
        ext,
        "thumbnails",
        ext === "gif" ? "gif" : "static",
        {
          uploadedBy: queueSubmitter,
          uploadedAt: (/* @__PURE__ */ new Date()).toISOString()
        },
        MAX_THUMBNAILS_PER_LEVEL
      );
      // Clean up any thumbnails evicted by the per-level cap.
      for (const removed of queueAppendRes.removed) {
        const removedPath = (removed.path || "thumbnails").replace(/^\//, "");
        const removedKey = `${removedPath}/${levelId}_${removed.version}.${removed.format || "webp"}`;
        if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(removedKey));
        else await env.THUMBNAILS_BUCKET.delete(removedKey);
      }
      // Drop any leftover pending file for this level so it doesn't get
      // re-promoted accidentally.
      if (pendingKey) {
        if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(pendingKey));
        else await env.THUMBNAILS_BUCKET.delete(pendingKey);
      }
      // Reset cached rating data so the new thumbnail starts fresh.
      if (ctx) ctx.waitUntil(env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`));
      else await env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`);
      await env.SYSTEM_BUCKET.delete(queueKey);
      logAudit(env.SYSTEM_BUCKET, "accept_thumbnail", {
        levelId,
        moderator: username || "Unknown",
        subCategory: "thumbnails"
      }, ctx);
      ctx.waitUntil(invalidateQueue(env.SYSTEM_BUCKET, "thumbnails", String(levelId)));
      // Invalidate the gallery / list / info caches so the new thumbnail
      // shows up immediately instead of waiting on the 10 min CF edge TTL.
      invalidateThumbnail(request, levelId, env).catch(() => {
      });
      memCache.invalidate(`thumbnails_list_${levelId}`);
      ctx.waitUntil(dispatchWebhook(env, "thumbnail_accepted", {
        levelId: String(levelId),
        moderator: username,
        submittedBy: queueSubmitter
      }));
      // Promote the accepted thumbnail into latest_uploads.json so the
      // public /api/latest-uploads feed reflects user-suggested thumbnails
      // *only* once a moderator has approved them. Suggestions sitting in
      // the queue must never appear here.
      const updateLatestFromQueue = /* @__PURE__ */ __name(async () => {
        const latestKey = "data/system/latest_uploads.json";
        let latest = await getR2Json(env.SYSTEM_BUCKET, latestKey) || [];
        latest = latest.filter((item) => item.levelId !== parseInt(levelId));
        // Best-effort enrichment with persisted level metadata so the entry
        // matches the shape produced by direct moderator uploads.
        let lvlName = null;
        let lvlCreator = null;
        try {
          const meta = await getR2Json(env.SYSTEM_BUCKET, `data/levelmeta/${levelId}.json`);
          if (meta) {
            lvlName = meta.levelName || meta.name || null;
            lvlCreator = meta.creator || meta.creatorName || null;
          }
        } catch (_) {}
        latest.unshift({
          levelId: parseInt(levelId),
          username: queueSubmitter,
          timestamp: Date.now(),
          accountID: queueAccountID,
          acceptedBy: username || "unknown",
          ...(lvlName ? { levelName: lvlName } : {}),
          ...(lvlCreator ? { creator: lvlCreator } : {})
        });
        if (latest.length > 20) latest = latest.slice(0, 20);
        await putR2Json(env.SYSTEM_BUCKET, latestKey, latest);
        memCache.invalidate("latest_uploads");
      }, "updateLatestFromQueue");
      if (ctx) ctx.waitUntil(updateLatestFromQueue());
      else await updateLatestFromQueue();
      return new Response(JSON.stringify({ success: true, message: "Thumbnail accepted and published" }), {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (category === "profilebackground") {
      const accountId = levelId;
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountId}_` });
      if (pendingList.objects.length === 0) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: "No pending profile background found" }), {
          status: 404,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      }
      const sorted2 = pendingList.objects.sort((a, b) => {
        const getTs = /* @__PURE__ */ __name((k) => {
          const m = k.key.match(/_(\d+)\./);
          return m ? parseInt(m[1]) : 0;
        }, "getTs");
        return getTs(b) - getTs(a);
      });
      const pendingObj = await env.THUMBNAILS_BUCKET.get(sorted2[0].key, { skipMeta: true });
      if (!pendingObj) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: "Failed to read pending background" }), {
          status: 500,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      }
      const pendingBuffer = await pendingObj.arrayBuffer();
      const realBgMime = detectMimeType(new Uint8Array(pendingBuffer)) || pendingObj.httpMetadata?.contentType || "image/png";
      const pendingBgSecReject = rejectIfMalicious(new Uint8Array(pendingBuffer), realBgMime);
      if (pendingBgSecReject) return pendingBgSecReject;
      const ext = sorted2[0].key.split(".").pop() || "png";
      const ts = Date.now().toString();
      const destKey = `profilebackground/${accountId}_${ts}.${ext}`;
      const existingPrefixes = [`profilebackground/${accountId}.`, `profilebackground/${accountId}_`];
      const existingKeys = [];
      for (const pfx of existingPrefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: pfx });
        existingKeys.push(...list.objects.map((o) => o.key));
      }
      if (existingKeys.length > 0) await Promise.all(existingKeys.map((k) => env.THUMBNAILS_BUCKET.delete(k)));
      await env.THUMBNAILS_BUCKET.put(destKey, pendingBuffer, {
        httpMetadata: { contentType: pendingObj.httpMetadata?.contentType || `image/${ext}`, cacheControl: NO_STORE_CACHE_CONTROL },
        customMetadata: {
          uploadedBy: queueData?.submittedBy || "unknown",
          updated_by: queueData?.submittedBy || "unknown",
          acceptedBy: username || "unknown",
          acceptedAt: (/* @__PURE__ */ new Date()).toISOString(),
          accountID: accountId.toString(),
          category: "profilebackground",
          contentKind: "profilebackground"
        }
      });
      await Promise.all(sorted2.map((o) => env.THUMBNAILS_BUCKET.delete(o.key)));
      await env.SYSTEM_BUCKET.delete(queueKey);
      logAudit(env.SYSTEM_BUCKET, "profilebackground_accept", { accountID: accountId, acceptedBy: username }, ctx);
      invalidateProfileBackground(request, accountId, env).catch(() => {
      });
      return new Response(JSON.stringify({ success: true, message: "Background approved and published" }), {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (sourceFolder) {
      let sourceKey = body.targetFilename;
      if (!sourceKey) sourceKey = `${sourceFolder}/${levelId}.webp`;
      let sourceObject = await env.THUMBNAILS_BUCKET.get(sourceKey, { skipMeta: true });
      if (!sourceObject) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: `${sourceFolder}/${levelId}`, limit: 20 });
        if (list.objects.length > 0) {
          const sorted2 = list.objects.sort((a, b) => {
            const getTs = /* @__PURE__ */ __name((k) => {
              const m = k.key.match(/_(\d+)/);
              return m ? parseInt(m[1]) : 0;
            }, "getTs");
            return getTs(b) - getTs(a);
          });
          sourceKey = sorted2[0].key;
          sourceObject = await env.THUMBNAILS_BUCKET.get(sourceKey, { skipMeta: true });
        }
      }
      if (!sourceObject) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: "Source thumbnail not found", details: `Could not find file for level ${levelId} in ${sourceFolder}/` }), {
          status: 404,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      }
      const thumbnailBuffer = await sourceObject.arrayBuffer();
      const realThumbMime = detectMimeType(new Uint8Array(thumbnailBuffer)) || sourceObject.httpMetadata?.contentType || "image/webp";
      const approveSecReject = rejectIfMalicious(new Uint8Array(thumbnailBuffer), realThumbMime);
      if (approveSecReject) return approveSecReject;
      const versionManager = new VersionManager(env.SYSTEM_BUCKET);
      const version = Date.now().toString();
      const destKey = `thumbnails/${levelId}_${version}.webp`;
      await env.THUMBNAILS_BUCKET.put(destKey, thumbnailBuffer, {
        httpMetadata: { contentType: "image/webp", cacheControl: NO_STORE_CACHE_CONTROL },
        customMetadata: {
          acceptedBy: username || "unknown",
          acceptedAt: (/* @__PURE__ */ new Date()).toISOString(),
          status: "accepted",
          originalSubmitter: queueSubmitter,
          uploadedBy: queueSubmitter,
          updated_by: queueSubmitter,
          version
        }
      });
      const appendRes = await versionManager.appendVersion(levelId, version, "webp", "thumbnails", "static", {
        uploadedBy: queueSubmitter,
        uploadedAt: (/* @__PURE__ */ new Date()).toISOString()
      }, MAX_THUMBNAILS_PER_LEVEL);
      if (ctx) ctx.waitUntil(env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`));
      else await env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`);
      for (const removed of appendRes.removed) {
        const removedPath = (removed.path || "thumbnails").replace(/^\//, "");
        const removedKey = `${removedPath}/${levelId}_${removed.version}.${removed.format || "webp"}`;
        if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(removedKey));
        else await env.THUMBNAILS_BUCKET.delete(removedKey);
      }
      if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(sourceKey));
      else await env.THUMBNAILS_BUCKET.delete(sourceKey);
      if (category === "verify" && queueData) {
        let suggestions = Array.isArray(queueData) ? queueData : [queueData];
        const deletionPromises = suggestions.map((s) => {
          const fname = s.filename || `suggestions/${levelId}.webp`;
          if (fname !== sourceKey) return env.THUMBNAILS_BUCKET.delete(fname);
          return Promise.resolve();
        });
        if (ctx) ctx.waitUntil(Promise.all(deletionPromises));
        else await Promise.all(deletionPromises);
      }
    }
    if (category === "verify" || category === "update") {
      const updateLatest = /* @__PURE__ */ __name(async () => {
        const latestKey = "data/system/latest_uploads.json";
        let latest = await getR2Json(env.SYSTEM_BUCKET, latestKey) || [];
        latest = latest.filter((item) => item.levelId !== parseInt(levelId));
        latest.unshift({
          levelId: parseInt(levelId),
          username: queueSubmitter,
          timestamp: Date.now(),
          accountID: queueAccountID,
          acceptedBy: username || "unknown"
        });
        if (latest.length > 20) latest = latest.slice(0, 20);
        await putR2Json(env.SYSTEM_BUCKET, latestKey, latest);
        memCache.invalidate("latest_uploads");
      }, "updateLatest");
      if (ctx) ctx.waitUntil(updateLatest());
      else await updateLatest();
      const webhookPayload = {
        levelId: parseInt(levelId),
        username: queueSubmitter,
        timestamp: Date.now(),
        is_update: category === "update"
      };
      if (ctx) ctx.waitUntil(dispatchWebhook(env, "upload", webhookPayload));
      else await dispatchWebhook(env, "upload", webhookPayload);
    }
    await env.SYSTEM_BUCKET.delete(queueKey);
    if (category === "verify" || category === "update") {
      invalidateThumbnail(request, levelId, env).catch(() => {
      });
      if (queueSubmitter && queueSubmitter !== "unknown") {
        const updateCreator = /* @__PURE__ */ __name(() => updateCreatorLeaderboardCache(env, queueSubmitter, {
          incrementUpload: true,
          accountID: queueAccountID
        }), "updateCreator");
        if (ctx) ctx.waitUntil(updateCreator());
        else await updateCreator();
      }
    }
    return new Response(JSON.stringify({ success: true, message: "Item accepted and added to history" }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Accept queue error:", error);
    return new Response(JSON.stringify({ error: "Failed to accept item", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleAcceptQueue, "handleAcceptQueue");
async function handleClaimQueue(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { levelId, category, username } = body;
    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) {
      console.log(`[Security] Claim queue blocked: ${username}`);
      return modAuthForbiddenResponse(auth);
    }
    if (!await isModeratorOrAdmin(env, username)) {
      return new Response(JSON.stringify({ error: "Moderator/Admin privileges required" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (!levelId || !category || !username) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    let queueFolder = category;
    if (category === "verify") queueFolder = "suggestions";
    else if (category === "update") queueFolder = "updates";
    else if (category === "report") queueFolder = "reports";
    else if (category === "profileimg") queueFolder = "profileimgs";
    else if (category === "profilebackground") queueFolder = "profilebackground";
    else if (category === "thumbnails") queueFolder = "thumbnails";
    const isUserReport = category === "report" && body.type === "user";
    let queueKey = isUserReport ? `data/queue/reports/user_${levelId}.json` : `data/queue/${queueFolder}/${levelId}.json`;
    let queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);
    if (!queueData && !isUserReport && category === "verify") {
      queueKey = `data/queue/thumbnails/${levelId}.json`;
      queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);
      if (queueData) queueFolder = "thumbnails";
    }
    if (!queueData) {
      return new Response(JSON.stringify({ error: "Queue item not found" }), {
        status: 404,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    queueData.claimedBy = username;
    queueData.claimedAt = (/* @__PURE__ */ new Date()).toISOString();
    queueData.status = "claimed";
    await putR2Json(env.SYSTEM_BUCKET, queueKey, queueData);
    const claimKey = `data/claims/${category}/${isUserReport ? "user_" : ""}${levelId}.json`;
    await putR2Json(env.SYSTEM_BUCKET, claimKey, {
      levelId: parseInt(levelId),
      category,
      claimedBy: username,
      claimedAt: (/* @__PURE__ */ new Date()).toISOString()
    });
    invalidateQueue(request, category, env).catch(() => {
    });
    return new Response(JSON.stringify({ success: true, message: "Level claimed successfully", claimedBy: username }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Claim queue error:", error);
    return new Response(JSON.stringify({ error: "Failed to claim item", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleClaimQueue, "handleClaimQueue");
async function handleRejectQueue(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { levelId, category, username, reason } = body;
    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) {
      console.log(`[Security] Reject queue blocked: ${username}`);
      return modAuthForbiddenResponse(auth);
    }
    if (!await isModeratorOrAdmin(env, username)) {
      return new Response(JSON.stringify({ error: "Moderator/Admin privileges required" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (!levelId || !category) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    let queueFolder = category;
    let sourceFolder = "";
    if (category === "verify") {
      queueFolder = "suggestions";
      sourceFolder = "suggestions";
    } else if (category === "update") {
      queueFolder = "updates";
      sourceFolder = "updates";
    } else if (category === "report") {
      queueFolder = "reports";
    } else if (category === "profileimg") {
      queueFolder = "profileimgs";
    } else if (category === "profilebackground") {
      queueFolder = "profilebackground";
    } else if (category === "thumbnails") {
      queueFolder = "thumbnails";
    }
    const isUserReport = category === "report" && body.type === "user";
    let queueKey = isUserReport ? `data/queue/reports/user_${levelId}.json` : `data/queue/${queueFolder}/${levelId}.json`;
    let itemData = await getR2Json(env.SYSTEM_BUCKET, queueKey);
    if (!itemData && !isUserReport && category === "verify") {
      queueKey = `data/queue/thumbnails/${levelId}.json`;
      itemData = await getR2Json(env.SYSTEM_BUCKET, queueKey);
      if (itemData) queueFolder = "thumbnails";
    }
    await env.SYSTEM_BUCKET.delete(queueKey);
    if (category === "profileimg") {
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${levelId}_` });
      if (pendingList.objects.length > 0) {
        await Promise.all(pendingList.objects.map((o) => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    } else if (category === "profilebackground") {
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${levelId}_` });
      if (pendingList.objects.length > 0) {
        await Promise.all(pendingList.objects.map((o) => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    } else if (category === "thumbnails" || queueFolder === "thumbnails") {
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_thumbnails/${levelId}_` });
      if (pendingList.objects.length > 0) {
        await Promise.all(pendingList.objects.map((o) => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    } else if (sourceFolder) {
      if (itemData) {
        const items = Array.isArray(itemData) ? itemData : [itemData];
        const deletePromises = items.map((item) => {
          const fname = item.filename || `${sourceFolder}/${levelId}.webp`;
          return env.THUMBNAILS_BUCKET.delete(fname);
        });
        await Promise.all(deletePromises);
      }
      await env.THUMBNAILS_BUCKET.delete(`${sourceFolder}/${levelId}.webp`);
      const remainingList = await env.THUMBNAILS_BUCKET.list({ prefix: `${sourceFolder}/${levelId}_`, limit: 50 });
      if (remainingList.objects.length > 0) {
        await Promise.all(remainingList.objects.map((o) => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    }
    const logKey = `data/history/rejected/${levelId}-${Date.now()}.json`;
    await putR2Json(env.SYSTEM_BUCKET, logKey, {
      levelId: parseInt(levelId),
      category,
      rejectedBy: username || "unknown",
      rejectedAt: (/* @__PURE__ */ new Date()).toISOString(),
      reason: reason || "No reason provided",
      originalData: itemData
    });
    invalidateQueue(request, category, env).catch(() => {
    });
    return new Response(JSON.stringify({ success: true, message: "Item rejected and thumbnail deleted" }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Reject queue error:", error);
    return new Response(JSON.stringify({ error: "Failed to reject item", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleRejectQueue, "handleRejectQueue");
var BATCH_CONCURRENCY = 5;
async function batchProcess(items, handler) {
  const results = [];
  for (let i = 0; i < items.length; i += BATCH_CONCURRENCY) {
    const chunk = items.slice(i, i + BATCH_CONCURRENCY);
    const chunkResults = await Promise.allSettled(chunk.map(handler));
    results.push(...chunkResults);
  }
  return results;
}
__name(batchProcess, "batchProcess");
async function handleBatchAcceptQueue(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { items, username, accountID } = body;
    if (!Array.isArray(items) || items.length === 0) {
      return new Response(JSON.stringify({ error: "items array is required" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (items.length > 50) {
      return new Response(JSON.stringify({ error: "Max 50 items per batch" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const auth = await verifyModAuth(request, env, username, parseInt(accountID || "0"));
    if (!auth.authorized) return modAuthForbiddenResponse(auth);
    if (!await isModeratorOrAdmin(env, username)) {
      return new Response(JSON.stringify({ error: "Moderator/Admin privileges required" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const results = await batchProcess(items, async (item) => {
      const fakeBody = JSON.stringify({ ...item, username, accountID });
      const fakeRequest = new Request(
        new URL(`/api/queue/accept/${item.levelId}`, request.url).toString(),
        { method: "POST", headers: request.headers, body: fakeBody }
      );
      return handleAcceptQueue(fakeRequest, env, ctx);
    });
    const summary = {
      total: items.length,
      succeeded: results.filter((r) => r.status === "fulfilled").length,
      failed: results.filter((r) => r.status === "rejected").length,
      details: results.map((r, i) => ({
        levelId: items[i].levelId,
        status: r.status === "fulfilled" ? "accepted" : "failed",
        error: r.status === "rejected" ? r.reason?.message : void 0
      }))
    };
    return new Response(JSON.stringify({ success: true, ...summary }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Batch accept error:", error);
    return new Response(JSON.stringify({ error: "Batch accept failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleBatchAcceptQueue, "handleBatchAcceptQueue");
async function handleBatchRejectQueue(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { items, username, accountID, reason } = body;
    if (!Array.isArray(items) || items.length === 0) {
      return new Response(JSON.stringify({ error: "items array is required" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (items.length > 50) {
      return new Response(JSON.stringify({ error: "Max 50 items per batch" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const auth = await verifyModAuth(request, env, username, parseInt(accountID || "0"));
    if (!auth.authorized) return modAuthForbiddenResponse(auth);
    if (!await isModeratorOrAdmin(env, username)) {
      return new Response(JSON.stringify({ error: "Moderator/Admin privileges required" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const results = await batchProcess(items, async (item) => {
      const fakeBody = JSON.stringify({ ...item, username, accountID, reason: item.reason || reason });
      const fakeRequest = new Request(
        new URL(`/api/queue/reject/${item.levelId}`, request.url).toString(),
        { method: "POST", headers: request.headers, body: fakeBody }
      );
      return handleRejectQueue(fakeRequest, env);
    });
    const summary = {
      total: items.length,
      succeeded: results.filter((r) => r.status === "fulfilled").length,
      failed: results.filter((r) => r.status === "rejected").length,
      details: results.map((r, i) => ({
        levelId: items[i].levelId,
        status: r.status === "fulfilled" ? "rejected" : "failed",
        error: r.status === "rejected" ? r.reason?.message : void 0
      }))
    };
    return new Response(JSON.stringify({ success: true, ...summary }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Batch reject error:", error);
    return new Response(JSON.stringify({ error: "Batch reject failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleBatchRejectQueue, "handleBatchRejectQueue");

// src/controllers/batch.js
// ── /api/init — Single request at mod startup combining moderator check + manifest + featured ──
async function handleInit(request, env) {
  try {
    let body;
    try {
      body = await request.json();
    } catch {
      return new Response(JSON.stringify({ error: "Invalid JSON body" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const username = (body.username || "").toLowerCase().trim();
    const accountID = parseInt(body.accountID || "0");
    const levelIds = (body.levelIds || []).slice(0, 200).map((id) => parseInt(id)).filter((id) => id > 0);

    // Run all sub-queries in parallel for maximum speed
    const [moderators, vips, dailyData, weeklyData, manifestResult] = await Promise.all([
      getModerators(env.SYSTEM_BUCKET),
      getVips(env.SYSTEM_BUCKET),
      (async () => {
        const memKey = "featured_daily";
        let data = memCache.get(memKey);
        if (data === void 0) {
          data = await getR2Json(env.SYSTEM_BUCKET, "data/daily/current.json");
          if (data) memCache.set(memKey, data, 3e5);
        }
        if (!data || Date.now() > data.expiresAt) return null;
        return data;
      })(),
      (async () => {
        const memKey = "featured_weekly";
        let data = memCache.get(memKey);
        if (data === void 0) {
          data = await getR2Json(env.SYSTEM_BUCKET, "data/weekly/current.json");
          if (data) memCache.set(memKey, data, 3e5);
        }
        if (!data || Date.now() > data.expiresAt) return null;
        return data;
      })(),
      (async () => {
        if (levelIds.length === 0) return {};
        const vm = new VersionManager(env.SYSTEM_BUCKET);
        const fullMap = await vm.getMap();
        // Match handleManifest's routing logic: prefer Worker (Bunny Storage
        // by AccessKey, "bunny por contraseña") until quota tracker flips
        // _directMode on, then fall back to the paid CDN Pull Zone.
        const useDirectCdn = isDirectModeActive() && !!env.CDN_PULL_ZONE_URL;
        const requestOrigin = new URL(request.url).origin;
        const storageBase = useDirectCdn
          ? `${env.CDN_PULL_ZONE_URL}/thumbnails`
          : `${requestOrigin}/_storage`;
        const result = {};
        for (const id of levelIds) {
          const entry = fullMap[id];
          if (!entry) continue;
          let latest;
          let versionCount = 1;
          if (Array.isArray(entry)) {
            const normalized = entry.map((v, i) => normalizeManifestEntry(v, i)).filter(Boolean)
              .sort((a, b) => (a.position || 0) - (b.position || 0));
            if (normalized.length === 0) continue;
            latest = normalized[0];
            versionCount = normalized.length;
          } else {
            latest = normalizeManifestEntry(entry, 0);
            if (!latest) continue;
          }
          const ver = latest.version;
          const fmt = latest.format || "webp";
          const storedPath = latest.path || "thumbnails";
          const bunnyKey = ver === "legacy"
            ? `${storedPath}/${id}.${fmt}`
            : `${storedPath}/${id}_${ver}.${fmt}`;
          const cdnUrl = useDirectCdn
            ? `${storageBase}/${bunnyKey}`
            : `${requestOrigin}/t/${id}.${fmt}`;
          const revisionToken = `${versionCount}:${latest.id || "legacy"}:${ver}:${fmt}`;
          result[id] = { format: fmt, version: ver, id: latest.id || "legacy", cdnUrl, revisionToken };
        }
        return result;
      })()
    ]);

    // Compute moderator status
    const isAdmin = ADMIN_USERS.includes(username);
    let isModerator = isAdmin;
    let isVip = isAdmin;
    if (!isAdmin) {
      isModerator = moderators.includes(username);
      if (isModerator) isVip = true;
    }
    if (!isVip) {
      isVip = vips.includes(username);
    }

    // Build response
    const responseData = {
      moderator: { isModerator, isAdmin, isVip, accountID },
      manifest: manifestResult,
      daily: dailyData ? { success: true, data: dailyData } : null,
      weekly: weeklyData ? { success: true, data: weeklyData } : null
    };
    if (env.CDN_PULL_ZONE_URL) {
      responseData.cdnBaseUrl = env.CDN_PULL_ZONE_URL;
    }

    // Report authentication state, but never issue or return moderator secrets.
    // Username/accountID are public and therefore cannot authorize secret
    // recovery. Codes must be provisioned through an out-of-band admin flow.
    if ((isModerator || isAdmin) && accountID > 0) {
      try {
        const [identity, auth] = await Promise.all([
          verifyGdAccount(env, username, accountID),
          verifyModAuth(request, env, username, accountID)
        ]);
        responseData.moderator.authenticated = auth.authorized === true;
        if (!identity.ok) responseData.moderator.gdVerificationFailed = true;
      } catch {}
    }

    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("[Init] handleInit error:", error);
    return new Response(JSON.stringify({ error: "Internal error", code: "INTERNAL_ERROR" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleInit, "handleInit");

// ── /api/v2/ratings/batch — Batch ratings for multiple levels in one request ──
async function handleBatchRatings(request, env) {
  try {
    let body;
    try {
      body = await request.json();
    } catch {
      return new Response(JSON.stringify({ error: "Invalid JSON body" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    // Cap was 50 — but each id can probe up to 4 candidate JSON files
    // (ratings-v2/{id}_{thumb}, ratings-v2/{id}, ratings/{id}_{thumb},
    // ratings/{id}), so 50 * 4 = 200 subrequests. The 50-subrequest
    // Workers Free budget would silently fail. We cap to 12 here, and
    // also short-circuit the candidate loop on the first hit.
    const levelIds = (body.levelIds || []).slice(0, 12).map((id) => String(id)).filter(Boolean);
    const username = (body.username || "").toLowerCase().trim();
    const thumbnailIds = body.thumbnailIds || {}; // optional map: { levelId: thumbnailId }

    if (levelIds.length === 0) {
      return new Response(JSON.stringify({ ratings: {} }), {
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }

    const ratings = {};
    await Promise.all(levelIds.map(async (levelID) => {
      const thumbId = thumbnailIds[levelID] || null;
      const candidates = [];
      if (thumbId) candidates.push(`ratings-v2/${levelID}_${thumbId}.json`);
      candidates.push(`ratings-v2/${levelID}.json`);
      if (thumbId) candidates.push(`ratings/${levelID}_${thumbId}.json`);
      candidates.push(`ratings/${levelID}.json`);

      let data = null;
      for (const key of candidates) {
        data = await getR2Json(env.SYSTEM_BUCKET, key);
        if (data) break;
      }
      if (!data) data = { total: 0, count: 0, votes: {} };

      const average = data.count > 0 ? data.total / data.count : 0;
      let userVote = 0;
      if (username && data.votes) {
        // Check both old format (username only) and new format (username_accountID)
        for (const [voteKey, stars] of Object.entries(data.votes)) {
          if (voteKey === username || voteKey.startsWith(username + "_")) {
            userVote = stars;
            break;
          }
        }
      }
      ratings[levelID] = { average, count: data.count, userVote };
    }));

    return new Response(JSON.stringify({ ratings }), {
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    console.error("[Ratings] handleBatchRatings error:", error);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleBatchRatings, "handleBatchRatings");

// ── /api/discovery — Combines top-creators + top-thumbnails + latest-uploads + leaderboard ──
async function handleDiscovery(request, env) {
  try {
    const url2 = new URL(request.url);
    const creatorsLimit = parseInt(url2.searchParams.get("creatorsLimit") || "20");
    const thumbnailsLimit = parseInt(url2.searchParams.get("thumbnailsLimit") || "20");
    const uploadsLimit = parseInt(url2.searchParams.get("uploadsLimit") || "10");

    const [topCreators, topThumbnails, latestUploads, dailyData, weeklyData, leaderboard] = await Promise.all([
      (async () => {
        const cacheKey = "top_creators";
        let cache = memCache.get(cacheKey);
        if (cache === void 0) {
          cache = await getR2Json(env.SYSTEM_BUCKET, "data/system/creator_leaderboard.json") || [];
          memCache.set(cacheKey, cache, 72e5);
        }
        return cache.slice(0, creatorsLimit);
      })(),
      (async () => {
        const cacheKey = "top_thumbnails";
        let cache = memCache.get(cacheKey);
        if (cache === void 0) {
          cache = await getR2Json(env.SYSTEM_BUCKET, "data/system/top_thumbnails.json") || [];
          memCache.set(cacheKey, cache, 72e5);
        }
        return cache.slice(0, thumbnailsLimit);
      })(),
      (async () => {
        const memKey = "latest_uploads";
        let data = memCache.get(memKey);
        if (data === void 0) {
          data = await getR2Json(env.SYSTEM_BUCKET, "data/system/latest_uploads.json");
          memCache.set(memKey, data, 3e4);
        }
        return (data || []).slice(0, uploadsLimit);
      })(),
      (async () => {
        const memKey = "featured_daily";
        let data = memCache.get(memKey);
        if (data === void 0) {
          data = await getR2Json(env.SYSTEM_BUCKET, "data/daily/current.json");
          if (data) memCache.set(memKey, data, 3e5);
        }
        if (!data || Date.now() > data.expiresAt) return null;
        return data;
      })(),
      (async () => {
        const memKey = "featured_weekly";
        let data = memCache.get(memKey);
        if (data === void 0) {
          data = await getR2Json(env.SYSTEM_BUCKET, "data/weekly/current.json");
          if (data) memCache.set(memKey, data, 3e5);
        }
        if (!data || Date.now() > data.expiresAt) return null;
        return data;
      })(),
      (async () => {
        const cacheKey = "leaderboard_full";
        let cache = memCache.get(cacheKey);
        if (cache === void 0) {
          cache = await getR2Json(env.SYSTEM_BUCKET, "data/system/creator_leaderboard.json") || [];
          memCache.set(cacheKey, cache, 72e5);
        }
        return cache.slice(0, 50);
      })()
    ]);

    const responseData = {
      topCreators: { creators: topCreators, total: topCreators.length },
      topThumbnails: { thumbnails: topThumbnails, total: topThumbnails.length },
      latestUploads: { uploads: latestUploads },
      daily: dailyData ? { success: true, data: dailyData } : null,
      weekly: weeklyData ? { success: true, data: weeklyData } : null,
      leaderboard: leaderboard
    };
    if (env.CDN_PULL_ZONE_URL) {
      responseData.cdnBaseUrl = env.CDN_PULL_ZONE_URL;
    }

    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    if (error instanceof StorageError) {
      return new Response(JSON.stringify({ error: "Storage temporarily unavailable", code: "STORAGE_ERROR", retryable: true }), {
        status: 502,
        headers: { "Content-Type": "application/json", "Retry-After": "5", ...corsHeaders() }
      });
    }
    console.error("[Discovery] handleDiscovery error:", error);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleDiscovery, "handleDiscovery");

// src/controllers/admin.js
async function handleModeratorCheck(request, env) {
  if (!await verifyApiKey(request, env)) {
    const receivedKey = request.headers.get("X-API-Key") || "(none)";
    console.error(`[ModCheck] API key mismatch (receivedLen=${receivedKey === "(none)" ? 0 : receivedKey.length})`);
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const url2 = new URL(request.url);
  const username = url2.searchParams.get("username");
  const accountID = parseInt(url2.searchParams.get("accountID") || "0");
  if (!username) {
    return new Response(JSON.stringify({ error: "Missing username" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const usernameLower = username.toLowerCase();
  const isAdmin = ADMIN_USERS.includes(usernameLower);
  let isModerator = isAdmin;
  let isVip = isAdmin;
  if (!isAdmin) {
    try {
      const moderators = await getModerators(env.SYSTEM_BUCKET);
      isModerator = moderators.includes(usernameLower);
      if (isModerator) isVip = true;
    } catch (e) {
      console.error("Error fetching moderators list:", e);
    }
  }
  if (!isVip) {
    try {
      const vips = await getVips(env.SYSTEM_BUCKET);
      isVip = vips.includes(usernameLower);
    } catch (e) {
      console.error("Error fetching VIP list:", e);
    }
  }
  let isHelper = false;
  let isIdea = false;
  try {
    isHelper = (await getHelpers(env.SYSTEM_BUCKET)).includes(usernameLower);
  } catch (e) {
    console.error("Error fetching helper list:", e);
  }
  try {
    isIdea = (await getIdeas(env.SYSTEM_BUCKET)).includes(usernameLower);
  } catch (e) {
    console.error("Error fetching idea list:", e);
  }
  console.log(`[ModCheck] username="${username}" lowercase="${usernameLower}" isAdmin=${isAdmin} isMod=${isModerator} isVip=${isVip} accountID=${accountID}`);
  let gdVerified = false;
  if ((isModerator || isAdmin) && accountID > 0) {
    try {
      const verify = await verifyGdAccount(env, username, accountID);
      gdVerified = verify.ok === true;
    } catch (e) {
      console.error(`[ModCheck] Identity verification error for ${username}:`, e);
    }
  }
  const modAuth = await verifyModAuth(request, env, usernameLower, accountID);
  const responseData = {
    isModerator,
    isAdmin,
    isVip,
    isHelper,
    isIdea,
    accountID,
    authenticated: modAuth.authorized === true,
    accountRequiredForGlobalUploads: true
  };
  if ((isModerator || isAdmin) && !gdVerified) {
    responseData.gdVerificationFailed = true;
  }
  return new Response(JSON.stringify(responseData), {
    status: 200,
    headers: { "Content-Type": "application/json", ...corsNoStore() }
  });
}
__name(handleModeratorCheck, "handleModeratorCheck");
async function handleSetDaily(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response("Unauthorized", { status: 401 });
  }
  try {
    const body = await request.json();
    const { levelID, type, username } = body;
    if (!levelID) return new Response("Missing levelID", { status: 400 });
    if (!username) return new Response("Missing username", { status: 400 });
    const admin = await requireAdmin(request, env, { username, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Set daily/weekly blocked: ${username} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }
    const key = "data/daily_weekly.json";
    let data = await getR2Json(env.SYSTEM_BUCKET, key) || { daily: null, weekly: null };
    if (type === "weekly") data.weekly = levelID;
    else if (type === "daily") data.daily = levelID;
    else if (type === "unset") {
      if (data.daily == levelID) data.daily = null;
      if (data.weekly == levelID) data.weekly = null;
    }
    await putR2Json(env.SYSTEM_BUCKET, key, data);
    memCache.invalidate("featured_daily");
    memCache.invalidate("featured_weekly");
    invalidateFeatured(request, env).catch(() => {
    });
    if (type === "daily" || type === "weekly") {
      dispatchWebhook(env, type, { levelID: parseInt(levelID), setBy: username, setAt: Date.now() }).catch(() => {
      });
    }
    return new Response(JSON.stringify({ success: true }), {
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (e) {
    console.error("[Admin] Error:", e);
    return new Response(JSON.stringify({ error: "Internal error" }), { status: 500 });
  }
}
__name(handleSetDaily, "handleSetDaily");
async function handleGetBanList(request, env) {
  if (!await verifyApiKey(request, env)) return new Response("Unauthorized", { status: 401 });
  const url2 = new URL(request.url);
  const blUsername = url2.searchParams.get("username") || "";
  const blAccountID = parseInt(url2.searchParams.get("accountID") || "0");
  if (!blUsername || blAccountID <= 0) {
    return new Response(JSON.stringify({ error: "Moderator credentials required" }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const auth = await verifyModAuth(request, env, blUsername, blAccountID);
  if (!auth.authorized || !await isModeratorOrAdmin(env, blUsername)) {
    console.log(`[Security] Get banlist blocked: ${blUsername}`);
    return modAuthForbiddenResponse(auth);
  }
  try {
    const data = await getR2Json(env.SYSTEM_BUCKET, "data/banlist.json");
    return new Response(JSON.stringify({ banned: data?.banned || [], details: data?.details || {} }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (e) {
    console.error("[Admin] handleGetBanList error:", e);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleGetBanList, "handleGetBanList");
async function handleCheckBanned(request, env) {
  if (!await verifyApiKey(request, env)) return new Response("Unauthorized", { status: 401 });
  const url2 = new URL(request.url);
  const username = (url2.searchParams.get("username") || "").toLowerCase().trim();
  const accountID = parseInt(url2.searchParams.get("accountID") || "0");
  try {
    let banned = false;
    let reason = "";
    if (username) {
      const data = await getR2Json(env.SYSTEM_BUCKET, "data/banlist.json");
      const list = Array.isArray(data?.banned) ? data.banned : [];
      if (list.includes(username)) {
        banned = true;
        reason = data?.details?.[username]?.reason || "";
      }
    }
    if (!banned && accountID > 0) {
      const banData = await getR2Json(env.SYSTEM_BUCKET, `data/bans/${accountID}.json`);
      if (banData && banData.banned) {
        banned = true;
        reason = banData.reason || reason;
      }
    }
    return new Response(JSON.stringify({ banned, reason }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (e) {
    console.error("[Ban] handleCheckBanned error:", e);
    return new Response(JSON.stringify({ banned: false, error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleCheckBanned, "handleCheckBanned");
async function handleBanUser(request, env) {
  if (!await verifyApiKey(request, env)) return new Response("Unauthorized", { status: 401 });
  try {
    const data = await request.json();
    const adminName = (data.admin || data.adminUser || "").toString().trim();
    const admin = await requireAdmin(request, env, { username: adminName, accountID: data.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Ban user blocked: ${adminName} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }
    const username = (data?.username || "").toString().trim().toLowerCase();
    if (!username) {
      return new Response(JSON.stringify({ error: "username required" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const moderators = await getModerators(env.SYSTEM_BUCKET);
    const isAdminTarget = ADMIN_USERS.includes(username);
    const isModTarget = moderators.includes(username) || isAdminTarget;
    if (isModTarget) {
      return new Response(JSON.stringify({ error: "cannot ban moderator/admin" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const banData = await getR2Json(env.SYSTEM_BUCKET, "data/banlist.json") || { banned: [], details: {} };
    const banned = Array.isArray(banData.banned) ? banData.banned : [];
    const details = banData.details || {};
    if (!banned.includes(username)) banned.push(username);
    details[username] = {
      reason: data.reason || "No reason provided",
      bannedBy: data.admin || "Unknown",
      timestamp: Date.now(),
      date: (/* @__PURE__ */ new Date()).toISOString()
    };
    const ok = await putR2Json(env.SYSTEM_BUCKET, "data/banlist.json", { banned, details });
    memCache.invalidate("system_banlist");
    invalidateBanList(request, env).catch(() => {
    });
    if (!ok) {
      return new Response(JSON.stringify({ error: "failed to write banlist" }), {
        status: 500,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    return new Response(JSON.stringify({ success: true, banned }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (e) {
    console.error("[Admin] handleBanUser error:", e);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleBanUser, "handleBanUser");
async function handleUnbanUser(request, env) {
  if (!await verifyApiKey(request, env)) return new Response("Unauthorized", { status: 401 });
  try {
    const data = await request.json();
    const adminName = (data.admin || data.adminUser || "").toString().trim();
    const admin = await requireAdmin(request, env, { username: adminName, accountID: data.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Unban user blocked: ${adminName} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }
    const username = (data?.username || "").toString().trim().toLowerCase();
    if (!username) {
      return new Response(JSON.stringify({ error: "username required" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const banData = await getR2Json(env.SYSTEM_BUCKET, "data/banlist.json") || { banned: [], details: {} };
    let banned = Array.isArray(banData.banned) ? banData.banned : [];
    let details = banData.details || {};
    banned = banned.filter((u) => u !== username);
    if (details[username]) delete details[username];
    await putR2Json(env.SYSTEM_BUCKET, "data/banlist.json", { banned, details });
    memCache.invalidate("system_banlist");
    invalidateBanList(request, env).catch(() => {
    });
    return new Response(JSON.stringify({ success: true }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (e) {
    console.error("[Admin] handleUnbanUser error:", e);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleUnbanUser, "handleUnbanUser");
async function handleAddModerator(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { username, adminUser } = body;
    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Add moderator blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }
    const usernameLower = username.toLowerCase();
    try {
      // If the admin sent an explicit accountID we can use the cached
      // identity verification helper (writes/reads from SYSTEM_BUCKET so
      // repeated promotions don't burn external subrequests).
      if (body.accountID && parseInt(body.accountID) > 0) {
        const verify = await verifyGdAccount(env, username, parseInt(body.accountID));
        if (!verify.ok) {
          return new Response(JSON.stringify({ success: false, message: `User "${username}" not found on GDBrowser or accountID mismatch` }), {
            status: 400,
            headers: { "Content-Type": "application/json", ...corsNoStore() }
          });
        }
      } else {
        // Legacy path: just check the user exists (no accountID binding).
        const profileRes = await fetch(`https://gdbrowser.com/api/profile/${encodeURIComponent(username)}`);
        if (!profileRes.ok) {
          return new Response(JSON.stringify({ success: false, message: `User "${username}" not found on GDBrowser` }), {
            status: 400,
            headers: { "Content-Type": "application/json", ...corsNoStore() }
          });
        }
      }
    } catch (e) {
      console.warn("GDBrowser validation failed (allowing anyway):", e);
    }
    const moderators = await getModerators(env.SYSTEM_BUCKET);
    if (moderators.includes(usernameLower)) {
      return new Response(JSON.stringify({ success: false, message: "User is already a moderator" }), {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    moderators.push(usernameLower);
    await putR2Json(env.SYSTEM_BUCKET, "data/moderators.json", { moderators });
    putR2Json(env.SYSTEM_BUCKET, "public/api/moderators.json", { moderators }).catch(() => {
    });
    invalidateModeration(request, env).catch(() => {
    });
    console.log(`[ModChange] ${adminUser} added ${usernameLower} as moderator`);
    return new Response(JSON.stringify({ success: true, message: `${username} added as moderator`, moderators }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Add moderator error:", error);
    return new Response(JSON.stringify({ error: "Failed to add moderator", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleAddModerator, "handleAddModerator");
async function handleRemoveModerator(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { username, adminUser } = body;
    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Remove moderator blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }
    const moderators = await getModerators(env.SYSTEM_BUCKET);
    const usernameLower = username.toLowerCase();
    const newModerators = moderators.filter((mod) => mod !== usernameLower);
    if (newModerators.length === moderators.length) {
      return new Response(JSON.stringify({ success: false, message: "User was not a moderator" }), {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    await putR2Json(env.SYSTEM_BUCKET, "data/moderators.json", { moderators: newModerators });
    putR2Json(env.SYSTEM_BUCKET, "public/api/moderators.json", { moderators: newModerators }).catch(() => {
    });
    try {
      await env.SYSTEM_BUCKET.delete(`data/auth/${usernameLower}.json`);
    } catch (e) {
      console.warn(`Failed to delete auth code for ${usernameLower}:`, e);
    }
    invalidateModeration(request, env).catch(() => {
    });
    console.log(`[ModChange] ${adminUser} removed ${usernameLower} from moderators`);
    return new Response(JSON.stringify({ success: true, message: `${username} removed from moderators`, moderators: newModerators }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Remove moderator error:", error);
    return new Response(JSON.stringify({ error: "Failed to remove moderator", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleRemoveModerator, "handleRemoveModerator");
async function handleAddVip(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { username, adminUser } = body;
    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Add VIP blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }
    const vips = await getVips(env.SYSTEM_BUCKET);
    const usernameLower = username.toLowerCase();
    if (vips.includes(usernameLower)) {
      return new Response(JSON.stringify({ success: false, message: "User is already a VIP" }), {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    vips.push(usernameLower);
    await putR2Json(env.SYSTEM_BUCKET, "data/vips.json", { vips });
    console.log(`[VipChange] ${adminUser} added ${usernameLower} as VIP`);
    return new Response(JSON.stringify({ success: true, message: `${username} added as VIP`, vips }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Add VIP error:", error);
    return new Response(JSON.stringify({ error: "Failed to add VIP", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleAddVip, "handleAddVip");
async function handleRemoveVip(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { username, adminUser } = body;
    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Remove VIP blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }
    const vips = await getVips(env.SYSTEM_BUCKET);
    const usernameLower = username.toLowerCase();
    const newVips = vips.filter((v) => v !== usernameLower);
    if (newVips.length === vips.length) {
      return new Response(JSON.stringify({ success: false, message: "User was not a VIP" }), {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    await putR2Json(env.SYSTEM_BUCKET, "data/vips.json", { vips: newVips });
    console.log(`[VipChange] ${adminUser} removed ${usernameLower} from VIPs`);
    return new Response(JSON.stringify({ success: true, message: `${username} removed from VIPs`, vips: newVips }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Remove VIP error:", error);
    return new Response(JSON.stringify({ error: "Failed to remove VIP", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleRemoveVip, "handleRemoveVip");
async function handleAddRole(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { role, username, adminUser } = body;
    if (!role || !username || !adminUser) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (!ROLE_STORE[role]) {
      return new Response(JSON.stringify({ error: "Invalid role" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Add role ${role} blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }
    // Verify the TARGET owns the supplied accountID (distinct from the admin's).
    const targetAccountID = parseInt(body.targetAccountID || "0");
    try {
      if (targetAccountID > 0) {
        const verify = await verifyGdAccount(env, username, targetAccountID);
        if (!verify.ok) {
          return new Response(JSON.stringify({ success: false, message: `User "${username}" not found on GDBrowser or accountID mismatch` }), {
            status: 400,
            headers: { "Content-Type": "application/json", ...corsNoStore() }
          });
        }
      } else {
        const profileRes = await fetch(`https://gdbrowser.com/api/profile/${encodeURIComponent(username)}`);
        if (!profileRes.ok) {
          return new Response(JSON.stringify({ success: false, message: `User "${username}" not found on GDBrowser` }), {
            status: 400,
            headers: { "Content-Type": "application/json", ...corsNoStore() }
          });
        }
      }
    } catch (e) {
      console.warn("GDBrowser validation failed (allowing anyway):", e);
    }
    const usernameLower = username.toLowerCase();
    const list = await getRoleList(env.SYSTEM_BUCKET, role);
    if (list.includes(usernameLower)) {
      return new Response(JSON.stringify({ success: false, message: `User is already a ${role}` }), {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    list.push(usernameLower);
    await writeRoleList(env, role, list);
    if (ROLE_STORE[role].invalidate) invalidateModeration(request, env).catch(() => {});
    console.log(`[RoleChange] ${adminUser} added ${usernameLower} as ${role}`);
    return new Response(JSON.stringify({ success: true, message: `${username} added as ${role}`, members: list }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Add role error:", error);
    return new Response(JSON.stringify({ error: "Failed to add role", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleAddRole, "handleAddRole");
async function handleRemoveRole(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { role, username, adminUser } = body;
    if (!role || !username || !adminUser) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (!ROLE_STORE[role]) {
      return new Response(JSON.stringify({ error: "Invalid role" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Remove role ${role} blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }
    const usernameLower = username.toLowerCase();
    const list = await getRoleList(env.SYSTEM_BUCKET, role);
    const newList = list.filter((u) => u !== usernameLower);
    if (newList.length === list.length) {
      return new Response(JSON.stringify({ success: false, message: `User was not a ${role}` }), {
        status: 200,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    await writeRoleList(env, role, newList);
    if (role === "mod") {
      try { await env.SYSTEM_BUCKET.delete(`data/auth/${usernameLower}.json`); } catch (e) {}
      invalidateModeration(request, env).catch(() => {});
    }
    console.log(`[RoleChange] ${adminUser} removed ${usernameLower} from ${role}`);
    return new Response(JSON.stringify({ success: true, message: `${username} removed from ${role}`, members: newList }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Remove role error:", error);
    return new Response(JSON.stringify({ error: "Failed to remove role", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleRemoveRole, "handleRemoveRole");
async function handleRoleMembers(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const url2 = new URL(request.url);
  const role = url2.searchParams.get("role") || "";
  if (!ROLE_STORE[role]) {
    return new Response(JSON.stringify({ error: "Invalid role" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const adminUser = request.headers.get("X-Admin-User") || url2.searchParams.get("username") || "";
  const adminAccountID = parseInt(url2.searchParams.get("accountID") || "0");
  if (!adminUser || !ADMIN_USERS.includes(adminUser.toLowerCase())) {
    return new Response(JSON.stringify({ error: "Admin privileges required" }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const admin = await requireAdmin(request, env, { username: adminUser, accountID: adminAccountID });
  if (!admin.authorized) return forbiddenResponse(admin.error);
  const members = await getRoleList(env.SYSTEM_BUCKET, role);
  return new Response(JSON.stringify({ success: true, role, count: members.length, members }), {
    status: 200,
    headers: { "Content-Type": "application/json", ...corsNoStore() }
  });
}
__name(handleRoleMembers, "handleRoleMembers");
async function handleListModerators(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const url2 = new URL(request.url);
  const adminUser = request.headers.get("X-Admin-User") || url2.searchParams.get("username") || "";
  const adminAccountID = parseInt(url2.searchParams.get("accountID") || "0");
  if (!adminUser || !ADMIN_USERS.includes(adminUser.toLowerCase())) {
    return new Response(JSON.stringify({ error: "Admin privileges required" }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const admin = await requireAdmin(request, env, { username: adminUser, accountID: adminAccountID });
  if (!admin.authorized) return forbiddenResponse(admin.error);
  const moderators = await getModerators(env.SYSTEM_BUCKET);
  return new Response(JSON.stringify({ success: true, count: moderators.length, moderators }), {
    status: 200,
    headers: { "Content-Type": "application/json", ...corsNoStore() }
  });
}
__name(handleListModerators, "handleListModerators");
async function handleGetWhitelist(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const url2 = new URL(request.url);
  const wlUsername = url2.searchParams.get("username") || "";
  const wlAccountID = parseInt(url2.searchParams.get("accountID") || "0");
  const auth = await verifyModAuth(request, env, wlUsername, wlAccountID);
  if (!auth.authorized) return modAuthForbiddenResponse(auth);
  const type = url2.searchParams.get("type") || "profilebackground";
  const users = await getWhitelist(env.SYSTEM_BUCKET, type);
  return new Response(JSON.stringify({ success: true, type, users }), {
    status: 200,
    headers: { "Content-Type": "application/json", ...corsNoStore() }
  });
}
__name(handleGetWhitelist, "handleGetWhitelist");
async function handleAddWhitelist(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { username, type } = body;
    const wlType = type || "profilebackground";
    const adminUser = (body.adminUser || body.moderator || body.actor || "").toString().trim();
    if (!isSafeUsername(username) || !isSafeUsername(adminUser) || !/^[a-z0-9_-]{1,32}$/.test(wlType)) {
      return new Response(JSON.stringify({ error: "Valid target, actor and whitelist type required" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) return modAuthForbiddenResponse(auth);
    if (!await isModeratorOrAdmin(env, adminUser)) return forbiddenResponse("Moderator/Admin privileges required");
    const users = await addToWhitelist(env.SYSTEM_BUCKET, username, adminUser, wlType);
    logAudit(env.SYSTEM_BUCKET, "whitelist_add", {
      target: username.toLowerCase(),
      addedBy: adminUser,
      type: wlType
    }, ctx);
    return new Response(JSON.stringify({ success: true, users }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Add whitelist error:", error);
    return new Response(JSON.stringify({ error: "Failed to add to whitelist", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleAddWhitelist, "handleAddWhitelist");
async function handleRemoveWhitelist(request, env, ctx) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { username, type } = body;
    const wlType = type || "profilebackground";
    const adminUser = (body.adminUser || body.moderator || body.actor || "").toString().trim();
    if (!isSafeUsername(username) || !isSafeUsername(adminUser) || !/^[a-z0-9_-]{1,32}$/.test(wlType)) {
      return new Response(JSON.stringify({ error: "Valid target, actor and whitelist type required" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) return modAuthForbiddenResponse(auth);
    if (!await isModeratorOrAdmin(env, adminUser)) return forbiddenResponse("Moderator/Admin privileges required");
    const users = await removeFromWhitelist(env.SYSTEM_BUCKET, username, adminUser, wlType);
    logAudit(env.SYSTEM_BUCKET, "whitelist_remove", {
      target: username.toLowerCase(),
      removedBy: adminUser,
      type: wlType
    }, ctx);
    return new Response(JSON.stringify({ success: true, users }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Remove whitelist error:", error);
    return new Response(JSON.stringify({ error: "Failed to remove from whitelist", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleRemoveWhitelist, "handleRemoveWhitelist");
async function handleDiscordLink(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const discordId = (body.discordId || "").toString().trim();
    const username = (body.username || "").toString().trim();
    const accountID = parseInt(body.accountID || "0");
    if (!discordId || !username) {
      return new Response(JSON.stringify({ error: "Missing discordId or username" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const auth = await verifyModAuth(request, env, username, accountID);
    if (!auth.authorized) {
      return modAuthForbiddenResponse(auth);
    }
    const usernameLower = username.toLowerCase();
    const isAdmin = ADMIN_USERS.includes(usernameLower);
    let role = "authenticated";
    if (isAdmin) {
      role = "admin";
    } else {
      const moderators = await getModerators(env.SYSTEM_BUCKET);
      if (moderators.includes(usernameLower)) {
        role = "moderator";
      } else {
        const vips = await getVips(env.SYSTEM_BUCKET);
        if (vips.includes(usernameLower)) {
          role = "vip";
        }
      }
    }
    const linkData = {
      discordId,
      username: usernameLower,
      accountID,
      role,
      linkedAt: (/* @__PURE__ */ new Date()).toISOString()
    };
    const linkKey = `data/discord-links/${discordId}.json`;
    const ok = await putR2Json(env.SYSTEM_BUCKET, linkKey, linkData);
    if (!ok) {
      return new Response(JSON.stringify({ error: "Failed to store link" }), {
        status: 500,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const reverseKey = `data/discord-links/by-username/${usernameLower}.json`;
    await putR2Json(env.SYSTEM_BUCKET, reverseKey, { discordId, username: usernameLower });
    console.log(`[Discord] Linked Discord ${discordId} \u2194 GD ${usernameLower} (role: ${role})`);
    return new Response(JSON.stringify({
      success: true,
      username: usernameLower,
      accountID,
      role,
      linkedAt: linkData.linkedAt
    }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (e) {
    console.error("[Discord] Link error:", e);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleDiscordLink, "handleDiscordLink");
async function handleDiscordLinkCheck(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const url2 = new URL(request.url);
  const discordId = url2.pathname.split("/").pop();
  if (!discordId) {
    return new Response(JSON.stringify({ error: "Missing discordId" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const linkKey = `data/discord-links/${discordId}.json`;
  const data = await getR2Json(env.SYSTEM_BUCKET, linkKey);
  if (!data) {
    return new Response(JSON.stringify({ linked: false }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  return new Response(JSON.stringify({
    linked: true,
    username: data.username,
    accountID: data.accountID,
    role: data.role,
    linkedAt: data.linkedAt
  }), {
    status: 200,
    headers: { "Content-Type": "application/json", ...corsNoStore() }
  });
}
__name(handleDiscordLinkCheck, "handleDiscordLinkCheck");
async function handleDiscordUnlink(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const url2 = new URL(request.url);
  const discordId = url2.pathname.split("/").pop();
  if (!discordId) {
    return new Response(JSON.stringify({ error: "Missing discordId" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const linkKey = `data/discord-links/${discordId}.json`;
  const existing = await getR2Json(env.SYSTEM_BUCKET, linkKey);
  if (!existing) {
    return new Response(JSON.stringify({ success: false, message: "No link found" }), {
      status: 404,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    await env.SYSTEM_BUCKET.delete(linkKey);
    if (existing.username) {
      await env.SYSTEM_BUCKET.delete(`data/discord-links/by-username/${existing.username}.json`);
    }
  } catch (e) {
    console.warn("[Discord] Cleanup error:", e);
  }
  console.log(`[Discord] Unlinked Discord ${discordId} from GD ${existing.username}`);
  return new Response(JSON.stringify({ success: true }), {
    status: 200,
    headers: { "Content-Type": "application/json", ...corsNoStore() }
  });
}
__name(handleDiscordUnlink, "handleDiscordUnlink");

// src/controllers/reports.js
async function handleSubmitReport(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { levelId, username, note } = body;
    if (!levelId) {
      return new Response(JSON.stringify({ error: "Missing levelId" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const queueKey = `data/queue/reports/${levelId}.json`;
    const queueItem = {
      levelId: parseInt(levelId),
      category: "report",
      submittedBy: username || "unknown",
      timestamp: Date.now(),
      status: "pending",
      note: censorText(note || "No details provided")
    };
    await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);
    return new Response(JSON.stringify({ success: true, message: "Report submitted successfully" }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Submit report error:", error);
    return new Response(JSON.stringify({ error: "Failed to submit report", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleSubmitReport, "handleSubmitReport");
async function handleSubmitUserReport(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { reportedAccountID, reportedUsername, note, reporterUsername, reporterAccountID } = body;
    if (!reportedAccountID || !reporterUsername) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (reporterAccountID && reporterAccountID === reportedAccountID) {
      return new Response(JSON.stringify({ error: "Cannot report yourself" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const banData = await getR2Json(env.SYSTEM_BUCKET, "data/banlist.json");
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (banned.includes(reporterUsername.toLowerCase())) {
      return new Response(JSON.stringify({ error: "User is banned" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const queueKey = `data/queue/reports/user_${reportedAccountID}.json`;
    let existing = await getR2Json(env.SYSTEM_BUCKET, queueKey);
    const reportEntry = {
      reporter: reporterUsername,
      reporterAccountID: parseInt(reporterAccountID) || 0,
      note: censorText((note || "No details provided").substring(0, 200)),
      timestamp: Date.now()
    };
    if (existing && existing.reports) {
      const alreadyReported = existing.reports.some(
        (r) => r.reporter?.toLowerCase() === reporterUsername.toLowerCase()
      );
      if (alreadyReported) {
        return new Response(JSON.stringify({ error: "You have already reported this user" }), {
          status: 409,
          headers: { "Content-Type": "application/json", ...corsNoStore() }
        });
      }
      existing.reports.push(reportEntry);
      existing.timestamp = Date.now();
      await putR2Json(env.SYSTEM_BUCKET, queueKey, existing);
    } else {
      const queueItem = {
        levelId: parseInt(reportedAccountID),
        category: "report",
        type: "user",
        reportedUsername: reportedUsername || "unknown",
        submittedBy: reporterUsername,
        timestamp: Date.now(),
        status: "pending",
        reports: [reportEntry]
      };
      await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);
    }
    return new Response(JSON.stringify({ success: true, message: "User report submitted" }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Submit user report error:", error);
    return new Response(JSON.stringify({ error: "Failed to submit user report", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleSubmitUserReport, "handleSubmitUserReport");
async function handleFeedbackSubmit(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json();
    const { type, title, description, username } = body;
    if (!type || !title || !description) {
      return new Response(JSON.stringify({ error: "Missing required fields" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const timestamp = Date.now();
    const id = `${timestamp}-${Math.random().toString(36).substring(2, 9)}`;
    const key = `data/feedback/${id}.json`;
    // Hash the IP with a server-side pepper so logs/exports do not leak raw
    // IPs. If FEEDBACK_IP_PEPPER is not configured, omit the IP entirely.
    let ipHash = null;
    const rawIp = request.headers.get("CF-Connecting-IP") || "";
    const pepper = env.FEEDBACK_IP_PEPPER;
    if (rawIp && pepper) {
      try {
        const enc = new TextEncoder();
        const k = await crypto.subtle.importKey("raw", enc.encode(pepper), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
        const sig = await crypto.subtle.sign("HMAC", k, enc.encode(rawIp));
        ipHash = [...new Uint8Array(sig)].slice(0, 8).map((b) => b.toString(16).padStart(2, "0")).join("");
      } catch (_) {
        ipHash = null;
      }
    }
    const feedbackData = {
      id,
      type,
      title: censorText(title),
      description: censorText(description),
      username: username || "Anonymous",
      timestamp,
      status: "pending",
      userAgent: (request.headers.get("User-Agent") || "").substring(0, 200),
      ipHash
    };
    await putR2Json(env.SYSTEM_BUCKET, key, feedbackData);
    return new Response(JSON.stringify({ success: true, message: "Feedback submitted successfully", id }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Feedback submit error:", error);
    return new Response(JSON.stringify({ error: "Failed to submit feedback", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleFeedbackSubmit, "handleFeedbackSubmit");
async function handleFeedbackList(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  if (!await verifyAdminFromRequest(request, env)) {
    return new Response(JSON.stringify({ error: "Admin required" }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const keys = await listR2Keys(env.SYSTEM_BUCKET, "data/feedback/");
    const dataResults = await Promise.all(keys.map((key) => getR2Json(env.SYSTEM_BUCKET, key)));
    const items = dataResults.filter(Boolean);
    items.sort((a, b) => b.timestamp - a.timestamp);
    return new Response(JSON.stringify({ success: true, count: items.length, items }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Get feedback error:", error);
    return new Response(JSON.stringify({ error: "Failed to get feedback", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleFeedbackList, "handleFeedbackList");
async function handleGetHistory(request, env, type) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const url2 = new URL(request.url);
    const limit2 = parseInt(url2.searchParams.get("limit")) || 100;
    const levelId = url2.searchParams.get("levelId");
    let prefix = `data/history/${type}/`;
    if (levelId) prefix += `${levelId}-`;
    const keys = await listR2Keys(env.SYSTEM_BUCKET, prefix);
    const slice = keys.slice(0, limit2);
    const dataResults = await Promise.all(slice.map((key) => getR2Json(env.SYSTEM_BUCKET, key)));
    const items = dataResults.filter(Boolean);
    items.sort((a, b) => {
      const timeA = new Date(a.uploadedAt || a.acceptedAt || a.rejectedAt).getTime();
      const timeB = new Date(b.uploadedAt || b.acceptedAt || b.rejectedAt).getTime();
      return timeB - timeA;
    });
    return new Response(JSON.stringify({ success: true, type, count: items.length, items }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Get history error:", error);
    return new Response(JSON.stringify({ error: "Failed to get history", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleGetHistory, "handleGetHistory");

// src/controllers/petshop.js
var PET_SHOP_CATALOG_KEY = "data/pet-shop/catalog.json";
var PET_SHOP_IMAGE_PREFIX = "pet-shop/";
async function handlePetShopList(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const catalog = await getR2Json(env.SYSTEM_BUCKET, PET_SHOP_CATALOG_KEY);
    const items = catalog?.items || [];
    return new Response(JSON.stringify({ items }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("[PetShop] List error:", error);
    return new Response(JSON.stringify({ error: "Failed to load pet shop" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handlePetShopList, "handlePetShopList");
async function handlePetShopDownload(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const url2 = new URL(request.url);
    const filename = url2.pathname.split("/").pop();
    if (!filename) {
      return new Response(JSON.stringify({ error: "Missing filename" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const key = PET_SHOP_IMAGE_PREFIX + filename;
    const candidates = expandKeyVariants(key);
    let object = null;
    for (const k of candidates) {
      object = await env.THUMBNAILS_BUCKET.get(k, { skipMeta: true });
      if (object) break;
    }
    if (!object) {
      return new Response(JSON.stringify({ error: "Pet not found" }), {
        status: 404,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const ext = filename.split(".").pop()?.toLowerCase();
    let contentType = "application/octet-stream";
    if (ext === "png") contentType = "image/png";
    else if (ext === "gif") contentType = "image/gif";
    else if (ext === "jpg" || ext === "jpeg") contentType = "image/jpeg";
    else if (ext === "webp") contentType = "image/webp";
    return new Response(object.body, {
      status: 200,
      headers: {
        "Content-Type": contentType,
        "Content-Disposition": `attachment; filename="${filename}"`,
        ...corsNoStore()
      }
    });
  } catch (error) {
    console.error("[PetShop] Download error:", error);
    return new Response(JSON.stringify({ error: "Download failed" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handlePetShopDownload, "handlePetShopDownload");
async function handlePetShopUpload(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const formData = await request.formData();
    const file = formData.get("image");
    const name = String(formData.get("name") || "Unknown Pet").trim().slice(0, 50);
    const creator = String(formData.get("creator") || "").trim();
    const accountID = parseInt(formData.get("accountID") || "0", 10);
    if (!file) {
      return new Response(JSON.stringify({ error: "Missing image file" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    if (!creator || accountID <= 0) {
      return new Response(JSON.stringify({ error: "Missing creator or accountID" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const creatorLower = creator.toLowerCase();
    const moderators = await getModerators(env.SYSTEM_BUCKET);
    const isAdmin = ADMIN_USERS.includes(creatorLower);
    const isMod = moderators.includes(creatorLower);
    if (!isAdmin && !isMod) {
      return new Response(JSON.stringify({ error: "Only moderators can upload pets" }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const auth = await verifyModAuth(request, env, creatorLower, accountID);
    if (!auth.authorized) return modAuthForbiddenResponse(auth);
    const accountVerification = await verifyAccountForWrite(env, accountID, creatorLower);
    if (!accountVerification.valid) {
      return new Response(JSON.stringify({ error: "Account verification failed", code: accountVerification.reason }), {
        status: 403,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const fileType = file.type || "";
    const allowedTypes = new Set(["image/png", "image/gif", "image/jpeg", "image/webp"]);
    if (!allowedTypes.has(fileType)) {
      return new Response(JSON.stringify({ error: "Unsupported image type" }), {
        status: 400,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    let format = "png";
    if (fileType === "image/gif") format = "gif";
    else if (fileType === "image/jpeg") format = "jpg";
    else if (fileType === "image/webp") format = "webp";
    if (file.size > 5 * 1024 * 1024) {
      return new Response(JSON.stringify({ error: "File too large (max 5MB)" }), {
        status: 413,
        headers: { "Content-Type": "application/json", ...corsNoStore() }
      });
    }
    const id = `pet_${Date.now()}_${Math.random().toString(36).substring(2, 8)}`;
    const storageKey = PET_SHOP_IMAGE_PREFIX + id + "." + format;
    const arrayBuffer = await file.arrayBuffer();
    const securityReject = rejectIfMalicious(new Uint8Array(arrayBuffer), fileType, file.name || `pet.${format}`);
    if (securityReject) return securityReject;
    await env.THUMBNAILS_BUCKET.put(storageKey, arrayBuffer, {
      httpMetadata: { contentType: fileType, cacheControl: NO_STORE_CACHE_CONTROL }
    });
    const catalog = await getR2Json(env.SYSTEM_BUCKET, PET_SHOP_CATALOG_KEY) || { items: [] };
    if (!Array.isArray(catalog.items)) catalog.items = [];
    const newItem = {
      id,
      name,
      creator: creator.slice(0, 32),
      format,
      fileSize: file.size,
      uploadedAt: (/* @__PURE__ */ new Date()).toISOString()
    };
    catalog.items.unshift(newItem);
    await putR2Json(env.SYSTEM_BUCKET, PET_SHOP_CATALOG_KEY, catalog);
    invalidatePetShop(request, env).catch(() => {
    });
    const origin = new URL(request.url).origin;
    const dlReq = cfCacheKey(new Request(`${origin}/api/pet-shop/download/${id}.${format}`));
    const dlResp = makeCacheable(new Response(arrayBuffer, {
      headers: { "Content-Type": fileType, "Access-Control-Allow-Origin": "*" }
    }), 604800);
    cfCachePut(dlReq, dlResp).catch(() => {
    });
    console.log(`[PetShop] ${creator} uploaded pet "${name}" (${id}.${format}, ${file.size} bytes)`);
    return new Response(JSON.stringify({ success: true, message: "Pet uploaded successfully", item: newItem }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("[PetShop] Upload error:", error);
    return new Response(JSON.stringify({ error: "Upload failed: " + error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handlePetShopUpload, "handlePetShopUpload");

// src/controllers/proxy.js
async function handleGDLevelProxy(request, env) {
  const url2 = new URL(request.url);
  const id = url2.pathname.replace("/api/level/", "").split("/")[0].trim();
  if (!id || !/^\d+$/.test(id)) {
    return new Response(JSON.stringify({ error: "Invalid level ID" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const cacheKey = `cache/level/${id}.json`;
  try {
    const cached = await env.SYSTEM_BUCKET.get(cacheKey, { skipMeta: true });
    if (cached) {
      const meta = cached.customMetadata || {};
      const cachedAt = parseInt(meta.cachedAt || "0");
      if (Date.now() - cachedAt < 36e5) {
        const text = await cached.text();
        return new Response(text, {
          status: 200,
          headers: { "Content-Type": "application/json", "X-Cache": "HIT", ...corsHeaders() }
        });
      }
    }
  } catch (_) {
  }
  try {
    const gdRes = await fetchWithRetry(`https://gdbrowser.com/api/level/${id}`, {
      headers: { "User-Agent": "PaimonThumbnails/1.0" },
      cf: { cacheTtl: 3600 }
    }, { label: `GDLevel:${id}` });
    if (!gdRes.ok) {
      return new Response(JSON.stringify({ error: "Level not found", status: gdRes.status }), {
        status: gdRes.status,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const data = await gdRes.json();
    if (!data || typeof data !== "object" || !data.name) {
      return new Response(JSON.stringify({ error: "Level not found" }), {
        status: 404,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const jsonText = JSON.stringify(data);
    try {
      await env.SYSTEM_BUCKET.put(cacheKey, jsonText, {
        httpMetadata: { contentType: "application/json" },
        customMetadata: { cachedAt: String(Date.now()) }
      });
    } catch (_) {
    }
    return new Response(jsonText, {
      status: 200,
      headers: { "Content-Type": "application/json", "X-Cache": "MISS", ...corsHeaders() }
    });
  } catch (e) {
    console.error("[GDProxy] Error fetching level", id, e);
    return new Response(JSON.stringify({ error: "Proxy error" }), {
      status: 502,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleGDLevelProxy, "handleGDLevelProxy");
async function handleGDProfileProxy(request, env) {
  const url2 = new URL(request.url);
  const username = url2.pathname.replace("/api/gd/profile/", "").split("/")[0].trim();
  if (!username || !/^[a-zA-Z0-9_-]{1,32}$/.test(username)) {
    return new Response(JSON.stringify({ error: "Invalid username" }), {
      status: 400,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const cacheKey = `cache/profile/${username.toLowerCase()}.json`;
  try {
    const cached = await env.SYSTEM_BUCKET.get(cacheKey, { skipMeta: true });
    if (cached) {
      const meta = cached.customMetadata || {};
      const cachedAt = parseInt(meta.cachedAt || "0");
      if (Date.now() - cachedAt < 36e5) {
        return new Response(await cached.text(), {
          status: 200,
          headers: { "Content-Type": "application/json", "X-Cache": "HIT", ...corsHeaders() }
        });
      }
    }
  } catch (_) {
  }
  try {
    const gdRes = await fetchWithRetry(`https://gdbrowser.com/api/profile/${encodeURIComponent(username)}`, {
      headers: { "User-Agent": "PaimonThumbnails/1.0" },
      cf: { cacheTtl: 3600 }
    }, { label: `GDProfile:${username}` });
    if (!gdRes.ok) {
      return new Response(JSON.stringify({ error: "Profile not found" }), {
        status: gdRes.status,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    const data = await gdRes.json();
    const jsonText = JSON.stringify(data);
    try {
      await env.SYSTEM_BUCKET.put(cacheKey, jsonText, {
        httpMetadata: { contentType: "application/json" },
        customMetadata: { cachedAt: String(Date.now()) }
      });
    } catch (_) {
    }
    return new Response(jsonText, {
      status: 200,
      headers: { "Content-Type": "application/json", "X-Cache": "MISS", ...corsHeaders() }
    });
  } catch (e) {
    console.error("[GDProxy] Profile proxy error:", e);
    return new Response(JSON.stringify({ error: "Proxy error" }), {
      status: 502,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleGDProfileProxy, "handleGDProfileProxy");

// src/services/mod-release.js
var GITHUB_REPO = "https://github.com/FlozWerDev/Paimbnails";
var MOD_VERSION = "1.0.1";
var MOD_VERSION_LABEL = `v${MOD_VERSION}`;
var MOD_RELEASE_TAG = "V1.0.1";
var MOD_CHANGELOG = "\u2022 Nuevo: Animacion de flash al cargar miniaturas\n\u2022 Correccion: Mejoras en la carga de imagenes";
var MOD_RELEASE_URL = `${GITHUB_REPO}/releases/tag/${MOD_RELEASE_TAG}`;
function getGitHubReleaseAssetUrl(assetName) {
  return `${GITHUB_REPO}/releases/download/${MOD_RELEASE_TAG}/${assetName}`;
}
__name(getGitHubReleaseAssetUrl, "getGitHubReleaseAssetUrl");
function getModDownloadUrl(request) {
  const url2 = new URL(request.url);
  return `${url2.origin}/downloads/paimon.level_thumbnails.geode`;
}
__name(getModDownloadUrl, "getModDownloadUrl");

// src/controllers/mod-system.js
async function handleVersionCheck(request) {
  try {
    console.log("[VersionCheck] Request from:", request.headers.get("user-agent"));
    return new Response(JSON.stringify({
      version: MOD_VERSION,
      tag: MOD_RELEASE_TAG,
      downloadUrl: getModDownloadUrl(request),
      changelog: MOD_CHANGELOG
    }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    console.error("[VersionCheck] Error:", error);
    return new Response(JSON.stringify({ error: "Internal server error", message: error.message || "Unknown error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleVersionCheck, "handleVersionCheck");
async function handleModDownload(request, env) {
  try {
    const object = await env.SYSTEM_BUCKET.get("mod-releases/paimon.level_thumbnails.geode", { skipMeta: true });
    if (!object) {
      console.error("[ModDownload] File not found in R2");
      return new Response(JSON.stringify({ error: "Mod file not found" }), {
        status: 404,
        headers: { "Content-Type": "application/json", ...corsHeaders() }
      });
    }
    console.log("[ModDownload] Serving mod file from R2");
    return new Response(object.body, {
      status: 200,
      headers: {
        "Content-Type": "application/octet-stream",
        "Content-Disposition": 'attachment; filename="paimon.level_thumbnails.geode"',
        ...corsHeaders()
      }
    });
  } catch (error) {
    console.error("[ModDownload] Error:", error);
    return new Response(JSON.stringify({ error: "Internal server error", message: error.message || "Unknown error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleModDownload, "handleModDownload");

// src/controllers/bot.js
async function handleGetBotConfig(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const key = "data/bot/config.json";
  let data = memCache.get("bot_config");
  if (data === void 0) {
    data = await getR2Json(env.SYSTEM_BUCKET, key);
    memCache.set("bot_config", data, 3e5);
  }
  return new Response(JSON.stringify(data || {}), {
    headers: { "Content-Type": "application/json", ...corsHeaders() }
  });
}
__name(handleGetBotConfig, "handleGetBotConfig");
async function handleSetBotConfig(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  if (!await verifyAdminFromRequest(request, env)) {
    return new Response(JSON.stringify({ error: "Admin required" }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const config = await request.json();
    const key = "data/bot/config.json";
    await putR2Json(env.SYSTEM_BUCKET, key, config);
    invalidateBotConfig(request, env).catch(() => {
    });
    const origin = new URL(request.url).origin;
    const wtReq = cfCacheKey(new Request(`${origin}/api/bot/config`));
    const wtResp = makeCacheable(
      new Response(JSON.stringify(config), {
        headers: {
          "Content-Type": "application/json",
          "Access-Control-Allow-Origin": "*"
        }
      }),
      300
    );
    cfCachePut(wtReq, wtResp).catch(() => {
    });
    memCache.set("bot_config", config, 3e5);
    return new Response(JSON.stringify({ success: true }), {
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("[Bot] handleSetBotConfig error:", error);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleSetBotConfig, "handleSetBotConfig");
async function handleGetLatestUploads(request, env) {
  try {
    const memKey = "latest_uploads";
    let data = memCache.get(memKey);
    if (data === void 0) {
      data = await getR2Json(
        env.SYSTEM_BUCKET,
        "data/system/latest_uploads.json"
      );
      memCache.set(memKey, data, 6e4);
    }
    return new Response(JSON.stringify({ uploads: data || [] }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    console.error("[Bot] handleGetLatestUploads error:", error);
    return new Response(JSON.stringify({ error: "Internal error" }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleGetLatestUploads, "handleGetLatestUploads");
async function handleGalleryList(request, env) {
  return new Response(
    JSON.stringify({
      error: "Gallery is under maintenance",
      maintenance: true,
      thumbnails: [],
      total: 0
    }),
    {
      status: 503,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    }
  );
}
__name(handleGalleryList, "handleGalleryList");

// src/controllers/migration.js
async function handleBackfillContributors(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  if (!await verifyAdminFromRequest(request, env)) {
    return new Response(JSON.stringify({ error: "Admin required" }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json().catch(() => ({}));
    const limit2 = Math.max(1, Math.min(parseInt(body.limit || "200", 10), 2e3));
    const dryRun = Boolean(body.dryRun);
    const acceptedKeys = await listR2Keys(env.SYSTEM_BUCKET, "data/history/accepted/");
    const acceptedMap = /* @__PURE__ */ new Map();
    for (const key of acceptedKeys) {
      const data = await getR2Json(env.SYSTEM_BUCKET, key);
      if (!data) continue;
      const levelId = (data.levelId ?? "").toString();
      if (!levelId) continue;
      const uploadedBy = data.originalSubmission?.submittedBy || data.submittedBy || void 0;
      const acceptedBy = data.acceptedBy || void 0;
      const acceptedAt = data.acceptedAt ? Date.parse(data.acceptedAt) : void 0;
      const prev = acceptedMap.get(levelId) || {};
      acceptedMap.set(levelId, { ...prev, uploadedBy, acceptedBy, acceptedAt });
    }
    const uploadKeys = await listR2Keys(env.SYSTEM_BUCKET, "data/history/uploads/");
    for (const key of uploadKeys) {
      const data = await getR2Json(env.SYSTEM_BUCKET, key);
      if (!data) continue;
      const levelId = (data.levelId ?? "").toString();
      if (!levelId) continue;
      const uploadedBy = data.uploadedBy || void 0;
      if (!acceptedMap.has(levelId)) {
        acceptedMap.set(levelId, { uploadedBy });
      } else {
        const prev = acceptedMap.get(levelId) || {};
        acceptedMap.set(levelId, { ...prev, uploadedBy: prev.uploadedBy || uploadedBy });
      }
    }
    const listMain = await env.THUMBNAILS_BUCKET.list({ prefix: "/thumbnails/", limit: 2e3 });
    const objects = listMain.objects.filter((o) => o.key.endsWith(".webp") || o.key.endsWith(".gif"));
    const summary = { scanned: 0, updated: 0, skipped: 0, missingInfo: 0, errors: [] };
    for (const obj of objects.slice(0, limit2)) {
      summary.scanned++;
      const key = obj.key;
      const basename = key.substring(key.lastIndexOf("/") + 1);
      const levelId = basename.replace(/\.(webp|gif)$/i, "");
      const info = acceptedMap.get(levelId);
      if (!info || !info.uploadedBy && !info.acceptedBy) {
        summary.missingInfo++;
        continue;
      }
      let head = null;
      try {
        head = await env.THUMBNAILS_BUCKET.head(key, { skipMeta: true });
      } catch (_) {
      }
      const meta = head?.customMetadata || {};
      const alreadyHas = meta.uploadedBy && (meta.acceptedBy || !info.acceptedBy);
      if (alreadyHas) {
        summary.skipped++;
        continue;
      }
      if (dryRun) {
        summary.updated++;
        continue;
      }
      try {
        const object = await env.THUMBNAILS_BUCKET.get(key, { skipMeta: true });
        if (!object) {
          summary.skipped++;
          continue;
        }
        const buf = await object.arrayBuffer();
        await env.THUMBNAILS_BUCKET.put(key, buf, {
          httpMetadata: {
            contentType: key.endsWith(".gif") ? "image/gif" : "image/webp",
            cacheControl: NO_STORE_CACHE_CONTROL
          },
          customMetadata: {
            ...meta,
            uploadedBy: meta.uploadedBy || info.uploadedBy || "Unknown",
            acceptedBy: meta.acceptedBy || info.acceptedBy || void 0,
            acceptedAt: meta.acceptedAt || (info.acceptedAt ? new Date(info.acceptedAt).toISOString() : void 0),
            status: meta.status || (info.acceptedBy ? "accepted" : meta.status || "uploaded")
          }
        });
        summary.updated++;
      } catch (e) {
        summary.errors.push({ key, error: e.message });
      }
    }
    return new Response(JSON.stringify({ success: true, ...summary }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Backfill contributors error:", error);
    return new Response(JSON.stringify({ error: "Backfill failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleBackfillContributors, "handleBackfillContributors");
async function handleMigrateLegacy(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  if (!await verifyAdminFromRequest(request, env)) {
    return new Response(JSON.stringify({ error: "Admin required" }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const body = await request.json().catch(() => ({}));
    const admin = await requireAdmin(request, env, body);
    if (!admin.authorized) {
      console.log(`[Security] Migrate legacy blocked: ${body.username || "(none)"} - ${admin.error}`);
      return forbiddenResponse(admin.error || "Admin privileges required");
    }
  } catch {
    return forbiddenResponse("Admin auth required with Mod Code");
  }
  try {
    const versionManager = new VersionManager(env.SYSTEM_BUCKET);
    const currentMap = await versionManager.getMap();
    let updatedCount = 0;
    let scannedCount = 0;
    const scanAndUpdate = /* @__PURE__ */ __name(async (prefix, format) => {
      let truncated2 = true;
      let cursor2 = void 0;
      while (truncated2) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix, cursor: cursor2, limit: 1e3 });
        truncated2 = list.truncated;
        cursor2 = list.cursor;
        for (const obj of list.objects) {
          scannedCount++;
          const key = obj.key;
          const filename = key.split("/").pop();
          if (filename.includes("_")) continue;
          const levelId = filename.replace(/\.(webp|png|gif)$/, "");
          if (!currentMap[levelId]) {
            currentMap[levelId] = { version: "legacy", format };
            updatedCount++;
          }
        }
      }
    }, "scanAndUpdate");
    await scanAndUpdate("thumbnails/", "webp");
    await scanAndUpdate("thumbnails/gif/", "gif");
    let truncated = true;
    let cursor = void 0;
    while (truncated) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: "thumbnails/", cursor, limit: 1e3 });
      truncated = list.truncated;
      cursor = list.cursor;
      for (const obj of list.objects) {
        if (!obj.key.endsWith(".png")) continue;
        const filename = obj.key.split("/").pop();
        if (filename.includes("_")) continue;
        const levelId = filename.replace(".png", "");
        if (!currentMap[levelId]) {
          currentMap[levelId] = { version: "legacy", format: "png" };
          updatedCount++;
        }
      }
    }
    if (updatedCount > 0) {
      await putR2Json(env.SYSTEM_BUCKET, versionManager.cacheKey, currentMap);
    }
    return new Response(JSON.stringify({
      success: true,
      scanned: scannedCount,
      migrated: updatedCount,
      message: `Migrated ${updatedCount} legacy thumbnails to VersionManager`
    }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Migration error:", error);
    return new Response(JSON.stringify({ error: "Migration failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleMigrateLegacy, "handleMigrateLegacy");
async function handleMigrateIds(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  if (!await verifyAdminFromRequest(request, env)) {
    return new Response(JSON.stringify({ error: "Admin required" }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const map = await vm.getMap();
    let levelsFixed = 0;
    let entriesFixed = 0;
    for (const [levelId, entry] of Object.entries(map)) {
      if (!Array.isArray(entry)) continue;
      let changed = false;
      const fixed = entry.map((v, i) => {
        if (!v || typeof v !== "object") return v;
        if (v.id === "1" || v.id === 1) {
          changed = true;
          entriesFixed++;
          const newId = v.version && v.version !== "legacy" ? v.version : String(Date.now() + i);
          return { ...v, id: newId };
        }
        return v;
      });
      if (changed) {
        map[levelId] = fixed;
        levelsFixed++;
      }
    }
    if (levelsFixed > 0) {
      await putR2Json(env.SYSTEM_BUCKET, vm.cacheKey, map);
      memCache.invalidate("versions.json");
    }
    return new Response(JSON.stringify({
      success: true,
      levelsFixed,
      entriesFixed,
      message: `Migrated ${entriesFixed} entries across ${levelsFixed} levels from id:"1" to version-based IDs`
    }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Migrate IDs error:", error);
    return new Response(JSON.stringify({ error: "Migration failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleMigrateIds, "handleMigrateIds");

// src/controllers/search.js
function normalizeSearchEntry(entry, index = 0) {
  if (!entry) return null;
  if (typeof entry === "string") {
    return {
      id: "legacy",
      position: 1,
      version: entry,
      format: "webp",
      uploadedBy: "Unknown"
    };
  }
  return {
    ...entry,
    id: entry.id || `${index + 1}`,
    position: typeof entry.position === "number" ? entry.position : index + 1,
    format: entry.format || "webp"
  };
}
__name(normalizeSearchEntry, "normalizeSearchEntry");
function normalizeSearchVersions(entry) {
  const list = Array.isArray(entry) ? entry : entry ? [entry] : [];
  return list.map((value, index) => normalizeSearchEntry(value, index)).filter(Boolean).sort((a, b) => (a.position || 0) - (b.position || 0));
}
__name(normalizeSearchVersions, "normalizeSearchVersions");
async function handleSearch(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
  const url2 = new URL(request.url);
  const creator = (url2.searchParams.get("creator") || "").toLowerCase().trim();
  const minRating = parseFloat(url2.searchParams.get("minRating") || "0");
  const format = (url2.searchParams.get("format") || "").toLowerCase();
  const page2 = Math.max(0, parseInt(url2.searchParams.get("page") || "0"));
  const limit2 = Math.min(50, Math.max(1, parseInt(url2.searchParams.get("limit") || "20")));
  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const allVersions = await vm.getMap();
    let ratingMap = null;
    if (minRating > 0) {
      const cacheKey = "search_rating_map";
      ratingMap = memCache.get(cacheKey);
      if (!ratingMap) {
        const topThumbs = await getR2Json(env.SYSTEM_BUCKET, "data/system/top_thumbnails.json") || [];
        ratingMap = new Map(topThumbs.map((t) => [String(t.levelId), t.rating]));
        memCache.set(cacheKey, ratingMap, 2 * 6e4);
      }
    }
    let results = [];
    for (const [levelId, entry] of Object.entries(allVersions)) {
      const versions = normalizeSearchVersions(entry);
      if (versions.length === 0) continue;
      const latest = versions[0];
      if (creator && !(latest.uploadedBy || "").toLowerCase().includes(creator)) continue;
      if (format && latest.format !== format) continue;
      if (minRating > 0) {
        const rating = ratingMap?.get(String(levelId)) || 0;
        if (rating < minRating) continue;
      }
      results.push({
        levelId: parseInt(levelId),
        format: latest.format,
        uploadedBy: latest.uploadedBy || "Unknown",
        uploadedAt: latest.uploadedAt,
        versionCount: versions.length,
        rating: ratingMap ? ratingMap.get(String(levelId)) || 0 : void 0
      });
    }
    results.sort((a, b) => {
      const ta = a.uploadedAt ? new Date(a.uploadedAt).getTime() : 0;
      const tb = b.uploadedAt ? new Date(b.uploadedAt).getTime() : 0;
      return tb - ta;
    });
    const total = results.length;
    const start = page2 * limit2;
    const paged = results.slice(start, start + limit2);
    return new Response(JSON.stringify({
      success: true,
      total,
      page: page2,
      limit: limit2,
      results: paged
    }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  } catch (error) {
    console.error("Search error:", error);
    return new Response(JSON.stringify({ error: "Search failed", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsHeaders() }
    });
  }
}
__name(handleSearch, "handleSearch");

// src/controllers/audit.js
async function handleAuditLogs(request, env) {
  if (!await verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: "Unauthorized" }), {
      status: 401,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  if (!await verifyAdminFromRequest(request, env)) {
    return new Response(JSON.stringify({ error: "Admin required" }), {
      status: 403,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
  const url2 = new URL(request.url);
  const action = url2.searchParams.get("action") || "";
  const from = url2.searchParams.get("from") || "";
  const to = url2.searchParams.get("to") || "";
  const limit2 = Math.min(parseInt(url2.searchParams.get("limit") || "50"), 100);
  const cursor = url2.searchParams.get("cursor") || "";
  try {
    let prefix = "data/audit/";
    if (action) prefix += `${action}/`;
    const listOpts = { prefix, limit: limit2 + 10 };
    if (cursor) listOpts.cursor = cursor;
    const listing = await env.SYSTEM_BUCKET.list(listOpts);
    const objects = listing.objects || [];
    const entries = [];
    for (const obj of objects) {
      const parts = obj.key.split("/");
      const dateStr = parts.length >= 4 ? parts[3] : "";
      if (from && dateStr < from) continue;
      if (to && dateStr > to) continue;
      const entry = await getR2Json(env.SYSTEM_BUCKET, obj.key);
      if (entry) {
        entries.push(entry);
      }
      if (entries.length >= limit2) break;
    }
    entries.sort((a, b) => (b.timestamp || 0) - (a.timestamp || 0));
    return new Response(JSON.stringify({
      entries,
      count: entries.length,
      truncated: listing.truncated || false,
      cursor: listing.truncated ? listing.cursor : null
    }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  } catch (error) {
    console.error("Audit log read error:", error);
    return new Response(JSON.stringify({ error: "Failed to read audit logs", details: error.message }), {
      status: 500,
      headers: { "Content-Type": "application/json", ...corsNoStore() }
    });
  }
}
__name(handleAuditLogs, "handleAuditLogs");

// src/middleware/cache-first.js
function deriveCacheTags(pathname) {
  const tags = [];
  const tMatch = pathname.match(/^\/t\/(\d+)/);
  if (tMatch) tags.push(`thumbnail-${tMatch[1]}`);
  const listMatch = pathname.match(/^\/api\/thumbnails\/(list|info)/);
  if (listMatch) {
    tags.push("thumbnails-meta");
  }
  const profileMatch = pathname.match(
    /^\/(profileimgs|profiles|profilebackground|backgrounds)\/(\d+)/
  );
  if (profileMatch) tags.push(`profile-${profileMatch[2]}`);
  if (pathname.startsWith("/api/manifest")) tags.push("manifest");
  if (pathname.startsWith("/api/latest-uploads")) tags.push("latest-uploads");
  if (pathname.startsWith("/api/top-thumbnails")) tags.push("top-thumbnails");
  if (pathname.startsWith("/api/leaderboard")) tags.push("leaderboard");
  if (pathname.startsWith("/api/top-creators")) tags.push("top-creators");
  if (pathname.startsWith("/api/daily")) tags.push("featured");
  if (pathname.startsWith("/api/weekly")) tags.push("featured");
  if (pathname.startsWith("/api/gallery")) tags.push("gallery");
  if (pathname.startsWith("/api/search")) tags.push("search");
  if (pathname.startsWith("/api/queue")) tags.push("queue");
  return tags.length > 0 ? tags : void 0;
}
__name(deriveCacheTags, "deriveCacheTags");
var ROUTE_CACHE_TTL = {
  // ── Thumbnails (binary assets — long TTL, purged instantly via CDN Purge API on upload/delete) ──
  "/t/": 604800,
  // 7 days (purged on upload/delete via CDN prefix purge)
  "/api/download/": 604800,
  // 7 days (purged on upload/delete via CDN prefix purge)
  // ── Thumbnail metadata (purged on upload/delete/accept) ──
  "/api/manifest": 21600,
  // 6 hours (purged on upload/delete + epoch bump). Long TTL because the
  // server explicitly bumps the manifest epoch on writes, so stale clients
  // get fresh data within seconds of an upload via Cache-Tag purge.
  "/api/thumbnails/list": 600,
  // 10 min (purged on upload/delete)
  "/api/thumbnails/info": 600,
  // 10 min (purged on upload/delete)
  "/api/exists": 600,
  // 10 min (purged on upload/delete)
  // ── Discovery / leaderboards (invalidated on vote/upload/featured, rebuilt every 2h by cron) ──
  "/api/top-creators": 7200,
  // 2 hours
  "/api/top-thumbnails": 7200,
  // 2 hours
  "/api/leaderboard": 900,
  // 15 min
  "/api/latest-uploads": 300,
  // 5 min
  "/api/gallery/list": 600,
  // 10 min
  "/api/search": 600,
  // 10 min
  "/api/discovery": 300,
  // 5 min (combines top-creators + top-thumbnails + latest-uploads)
  // ── Featured (purged on set-daily/set-weekly) ──
  "/api/daily/current": 3600,
  // 1 hour (purged on set-daily)
  "/api/weekly/current": 3600,
  // 1 hour (purged on set-weekly)
  "/api/featured/history": 3600,
  // 1 hour (purged on set-daily/weekly)
  // ── Profile assets (binary — long TTL, invalidated on upload via CDN prefix purge) ──
  "/profiles/": 2592e3,
  // 30 days
  "/profileimgs/": 2592e3,
  // 30 days
  "/backgrounds/": 2592e3,
  // 30 days
  "/profilebackground/": 2592e3,
  // 30 days
  "/profile-music/": 2592e3,
  // 30 days
  // ── Profile metadata (invalidated on profile update) ──
  "/api/profiles/config/": 1800,
  // 30 min
  "/api/profile/bundle/": 1800,
  // 30 min
  "/api/profile/batch-bundle": 600,
  // 10 min (batch of multiple profiles)
  "/api/profile/stats/": 7200,
  // 2 hours (rebuilt by cron)
  "/api/profile/bgkind/": 600,
  // 10 min (lightweight kind check, invalidated on bg upload / config change)
  "/api/profile/badge/": 1800,
  // 30 min (invalidated on badge set/delete)
  "/api/profile-music/": 1800,
  // 30 min (invalidated on upload/delete)
  // ── Ratings (invalidated on vote) ──
  "/api/v2/ratings/": 600,
  // 10 min
  "/api/profile-ratings/": 600,
  // 10 min
  // ── Moderators / admin reads ──
  "/api/moderators": 600,
  // 10 min
  "/api/moderator/check": 600,
  // 10 min
  "/api/admin/banlist": 300,
  // 5 min
  "/api/admin/moderators": 600,
  // 10 min
  // ── Queue (moderators need fresher data) ──
  "/api/queue/": 120,
  // 2 min
  // ── GD proxy (already caches in BunnyCDN, short CF layer) ──
  "/api/level/": 3600,
  // 1 hour
  "/api/gd/profile/": 3600,
  // 1 hour
  // ── Bot config ──
  "/api/bot/config": 300,
  // 5 min
  // ── Suggestions / Updates (binary downloads) ──
  "/suggestions/": 604800,
  // 7 days
  "/updates/": 604800,
  // 7 days
  // ── Pet shop ──
  "/api/pet-shop/list": 1800,
  // 30 min
  "/api/pet-shop/download/": 2592e3,
  // 30 days
  // ── Static pages ──
  "/": 3600,
  // 1 hour
  "/donate": 3600,
  "/guidelines": 3600,
  // ── Health ──
  "/health": 120
  // 2 min — healthcheck doesn't need to be fresher than this
};
function getTtlForPath(path) {
  if (ROUTE_CACHE_TTL[path] !== void 0) return ROUTE_CACHE_TTL[path];
  let bestMatch = void 0;
  let bestLen = 0;
  for (const [pattern, ttl] of Object.entries(ROUTE_CACHE_TTL)) {
    if (pattern.endsWith("/") && path.startsWith(pattern) && pattern.length > bestLen) {
      bestMatch = ttl;
      bestLen = pattern.length;
    }
  }
  return bestMatch;
}
__name(getTtlForPath, "getTtlForPath");

// src/services/worker-quota.js
var FREE_DAILY_LIMIT = 1e5;
var DEFAULT_THRESHOLD = 8e4;
var FLUSH_EVERY = 500;
var DIRECT_PATHS = [
  "/t/",
  "/backgrounds/",
  "/profilebackground/",
  "/profiles/",
  "/profileimgs/",
  "/profile-music/",
  "/suggestions/",
  "/updates/",
  "/api/pet-shop/download/",
  "/api/download/"
];
var _dayKey = "";
var _count = 0;
var _flushedCount = 0;
var _directMode = false;
var _initialized = false;
var _initPromise = null;
function todayKey2() {
  return (/* @__PURE__ */ new Date()).toISOString().split("T")[0];
}
__name(todayKey2, "todayKey");
function resolveThreshold(env) {
  const raw = parseInt(env?.WORKER_QUOTA_THRESHOLD || "");
  return !isNaN(raw) && raw > 0 ? raw : DEFAULT_THRESHOLD;
}
__name(resolveThreshold, "resolveThreshold");
async function _initialize(sysBucket) {
  const day = todayKey2();
  _dayKey = day;
  try {
    const obj = await sysBucket.get("data/system/worker-quota.json", { skipMeta: true });
    if (obj) {
      const data = JSON.parse(await obj.text());
      if (data && data.day === day) {
        _flushedCount = Math.max(0, data.count || 0);
        _count = _flushedCount;
        // Inherit direct mode flag from peer isolates so a hot isolate that
        // crossed the threshold propagates the routing decision to cold ones.
        if (data.directMode === true) _directMode = true;
      }
    }
  } catch (_) {
  }
  _initialized = true;
}
__name(_initialize, "_initialize");
async function _ensureInitialized(sysBucket) {
  if (_initialized) return;
  if (_initPromise) return _initPromise;
  _initPromise = _initialize(sysBucket).finally(() => {
    _initPromise = null;
  });
  return _initPromise;
}
__name(_ensureInitialized, "_ensureInitialized");
async function _flush(sysBucket) {
  // Merge with whatever the peer isolates have already persisted so the
  // global counter converges upward instead of being clobbered by the last
  // writer. Without this, two isolates each at 30k requests would each
  // flush "30k" and the file would forever read ~30k even though the real
  // total is 60k — that's the root cause of why direct mode never trips.
  try {
    let persistedCount = 0;
    let persistedDirect = false;
    let persistedDay = _dayKey;
    try {
      const obj = await sysBucket.get("data/system/worker-quota.json", { skipMeta: true });
      if (obj) {
        const data = JSON.parse(await obj.text());
        if (data) {
          persistedDay = data.day || _dayKey;
          if (persistedDay === _dayKey) {
            persistedCount = Math.max(0, data.count || 0);
            persistedDirect = data.directMode === true;
          }
        }
      }
    } catch (_) {
      // ignore — fall through with persistedCount=0
    }
    // Adopt peer state so this isolate sees the global picture.
    if (persistedCount > _count) _count = persistedCount;
    if (persistedDirect && !_directMode) _directMode = true;
    const merged = _count;
    const json = JSON.stringify({
      day: _dayKey,
      count: merged,
      directMode: _directMode,
      updatedAt: (/* @__PURE__ */ new Date()).toISOString()
    });
    await sysBucket.put("data/system/worker-quota.json", json, {
      httpMetadata: { contentType: "application/json" },
      skipMeta: true
    });
    _flushedCount = merged;
  } catch (_) {
  }
}
__name(_flush, "_flush");
async function trackRequest(sysBucket, env) {
  await _ensureInitialized(sysBucket);
  const day = todayKey2();
  if (day !== _dayKey) {
    _dayKey = day;
    _count = 0;
    _flushedCount = 0;
    _directMode = false;
  }
  _count++;
  const threshold = resolveThreshold(env);
  if (!_directMode && _count >= threshold) {
    _directMode = true;
    console.warn(`[WorkerQuota] ${_count}/${FREE_DAILY_LIMIT} requests today \u2014 enabling CDN direct mode (threshold=${threshold}).`);
    _flush(sysBucket).catch(() => {
    });
  } else if (_count - _flushedCount >= FLUSH_EVERY) {
    _flush(sysBucket).catch(() => {
    });
  }
}
__name(trackRequest, "trackRequest");
function buildDirectResponse(path, cdnPullZoneUrl) {
  if (!_directMode || !cdnPullZoneUrl) return null;
  if (!DIRECT_PATHS.some((p) => path.startsWith(p))) return null;
  let cdnPath = path;
  if (path.startsWith("/t/")) {
    cdnPath = "/thumbnails/" + path.slice(3);
  }
  return new Response(null, {
    status: 307,
    // Temporary — day resets at midnight and Worker quota renews
    headers: {
      "Location": cdnPullZoneUrl + cdnPath,
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Expose-Headers": "Location",
      "Cache-Control": "public, max-age=3600",
      "X-Quota-Direct": "1"
      // diagnostic header
    }
  });
}
__name(buildDirectResponse, "buildDirectResponse");
function getQuotaStats(env) {
  const threshold = resolveThreshold(env);
  return {
    enabled: env?.WORKER_QUOTA_ENABLED === "true",
    day: _dayKey || todayKey2(),
    requests: _count,
    limit: FREE_DAILY_LIMIT,
    threshold,
    percentUsed: _count > 0 ? Math.round(_count / FREE_DAILY_LIMIT * 100) : 0,
    directMode: _directMode
  };
}
__name(getQuotaStats, "getQuotaStats");
function setDirectMode(enabled) {
  _directMode = Boolean(enabled);
}
__name(setDirectMode, "setDirectMode");
// Public read-only accessor used by route handlers (handleManifest,
// handleInit, etc.) to decide whether to serve client URLs that go through
// the Worker (Bunny Storage proxy, costs Worker requests but no CDN egress)
// or directly to the CDN Pull Zone (paid bandwidth but 0 Worker requests).
//
// Default behaviour: route via Worker. Only flip to CDN once trackRequest()
// detects we're approaching the daily Worker quota.
function isDirectModeActive() {
  return _directMode === true;
}
__name(isDirectModeActive, "isDirectModeActive");

// src/pages/home.js
var homeHtml = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <link rel="icon" type="image/png" href="https://api.flozwer.org/favicon.png">
    <title>Paimbnails \u2014 Thumbnails for Geometry Dash</title>
    <meta name="description" content="Paimbnails transforms Geometry Dash into a fully visual experience \u2014 thumbnails, effects, audio, emotes, pets, profiles and more.">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700;800;900&family=General+Sans:wght@300;400;500;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-deep: #06020f;
            --bg-surface: #0d0618;
            --card-bg: rgba(124, 58, 237, 0.04);
            --card-bg-hover: rgba(124, 58, 237, 0.08);
            --card-border: rgba(139, 92, 246, 0.12);
            --card-border-hover: rgba(168, 85, 247, 0.35);
            --primary: #7c3aed;
            --primary-hover: #6d28d9;
            --primary-glow: rgba(124, 58, 237, 0.5);
            --primary-soft: rgba(124, 58, 237, 0.1);
            --accent: #c4b5fd;
            --accent-warm: #f59e0b;
            --accent-warm-soft: rgba(245, 158, 11, 0.12);
            --text-main: #f5f3ff;
            --text-muted: #a78bfa;
            --text-dim: #6d5a9e;
            --success: #10b981;
            --error: #ef4444;
            --gold: #fbbf24;
            --gradient-hero: linear-gradient(135deg, #7c3aed 0%, #a855f7 50%, #c084fc 100%);
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }
        html { scroll-behavior: smooth; }

        body {
            font-family: 'General Sans', -apple-system, sans-serif;
            background-color: var(--bg-deep);
            color: var(--text-main);
            min-height: 100vh;
            overflow-x: hidden;
            position: relative;
            line-height: 1.6;
        }

        h1, h2, h3, h4, h5, h6 {
            font-family: 'Outfit', sans-serif;
        }

        /* ── Ambient background ── */
        .ambient-bg {
            position: fixed;
            inset: 0;
            z-index: -3;
            overflow: hidden;
            pointer-events: none;
        }
        .ambient-orb {
            position: absolute;
            border-radius: 50%;
            filter: blur(80px);
            opacity: 0.4;
            animation: orbFloat 20s ease-in-out infinite;
        }
        .orb-1 {
            width: 600px; height: 600px;
            background: radial-gradient(circle, #7c3aed 0%, transparent 70%);
            top: -10%; left: -5%;
            animation-delay: 0s;
        }
        .orb-2 {
            width: 500px; height: 500px;
            background: radial-gradient(circle, #a855f7 0%, transparent 70%);
            top: 30%; right: -10%;
            animation-delay: -7s;
            animation-duration: 25s;
        }
        .orb-3 {
            width: 400px; height: 400px;
            background: radial-gradient(circle, #6d28d9 0%, transparent 70%);
            bottom: 10%; left: 20%;
            animation-delay: -14s;
            animation-duration: 30s;
        }
        .orb-4 {
            width: 300px; height: 300px;
            background: radial-gradient(circle, #f59e0b 0%, transparent 70%);
            top: 60%; right: 25%;
            opacity: 0.15;
            animation-delay: -5s;
            animation-duration: 22s;
        }
        @keyframes orbFloat {
            0%, 100% { transform: translate(0, 0) scale(1); }
            25% { transform: translate(30px, -40px) scale(1.05); }
            50% { transform: translate(-20px, 20px) scale(0.95); }
            75% { transform: translate(15px, 35px) scale(1.02); }
        }

        /* ── Noise texture overlay ── */
        .noise-overlay {
            position: fixed;
            inset: 0;
            z-index: -1;
            opacity: 0.03;
            pointer-events: none;
            background-image: url("data:image/svg+xml,%3Csvg viewBox='0 0 256 256' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.85' numOctaves='4' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)'/%3E%3C/svg%3E");
        }

        /* ── Background carousel ── */
        #background-carousel {
            position: fixed;
            inset: 0;
            z-index: -2;
            overflow: hidden;
            display: flex;
            flex-direction: column;
            justify-content: center;
            gap: 28px;
            opacity: 0.25;
            pointer-events: none;
            transform: skewY(-4deg) scale(1.3);
            filter: blur(2px) saturate(1.2);
        }
        .carousel-row {
            display: flex;
            gap: 22px;
            width: max-content;
            will-change: transform;
        }
        .carousel-item {
            width: 260px;
            height: 146px;
            border-radius: 14px;
            background-size: cover;
            background-position: center;
            background-color: rgba(124, 58, 237, 0.05);
            box-shadow: 0 8px 32px rgba(124, 58, 237, 0.15);
            flex-shrink: 0;
        }
        .scroll-left { animation: scrollLeft 80s linear infinite; }
        .scroll-right { animation: scrollRight 80s linear infinite; }
        @keyframes scrollLeft { 0% { transform: translateX(0); } 100% { transform: translateX(-50%); } }
        @keyframes scrollRight { 0% { transform: translateX(-50%); } 100% { transform: translateX(0); } }

        /* ── Navigation ── */
        .nav {
            position: sticky;
            top: 0;
            z-index: 50;
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 18px 40px;
            background: rgba(6, 2, 15, 0.75);
            backdrop-filter: blur(20px) saturate(1.5);
            -webkit-backdrop-filter: blur(20px) saturate(1.5);
            border-bottom: 1px solid rgba(124, 58, 237, 0.1);
            transition: all 0.3s ease;
        }
        .nav.scrolled {
            padding: 12px 40px;
            background: rgba(6, 2, 15, 0.92);
            border-bottom-color: rgba(124, 58, 237, 0.2);
        }
        .nav-brand {
            display: flex;
            align-items: center;
            gap: 12px;
            font-family: 'Outfit', sans-serif;
            font-weight: 800;
            font-size: 1.15rem;
            letter-spacing: -0.02em;
            color: var(--text-main);
            text-decoration: none;
        }
        .nav-logo {
            width: 32px;
            height: 32px;
            border-radius: 10px;
            background: var(--gradient-hero);
            box-shadow: 0 0 20px var(--primary-glow), inset 0 1px 0 rgba(255,255,255,0.2);
            position: relative;
            overflow: hidden;
        }
        .nav-logo::after {
            content: '';
            position: absolute;
            inset: 0;
            background: linear-gradient(135deg, rgba(255,255,255,0.3) 0%, transparent 50%);
            border-radius: inherit;
        }
        .nav-links { display: flex; gap: 4px; align-items: center; }
        .nav-link {
            padding: 9px 16px;
            color: var(--text-muted);
            text-decoration: none;
            font-size: 0.88rem;
            font-weight: 500;
            border-radius: 10px;
            transition: all 0.25s cubic-bezier(0.4, 0, 0.2, 1);
            position: relative;
        }
        .nav-link:hover {
            color: var(--text-main);
            background: var(--primary-soft);
        }
        .nav-link::after {
            content: '';
            position: absolute;
            bottom: 4px;
            left: 50%;
            width: 0;
            height: 2px;
            background: var(--primary);
            border-radius: 1px;
            transition: all 0.25s ease;
            transform: translateX(-50%);
        }
        .nav-link:hover::after { width: 20px; }
        .nav-cta {
            padding: 9px 20px;
            background: var(--primary);
            color: white !important;
            border-radius: 10px;
            font-weight: 600;
            box-shadow: 0 4px 16px var(--primary-glow);
            transition: all 0.25s ease;
        }
        .nav-cta:hover {
            background: var(--primary-hover) !important;
            transform: translateY(-1px);
            box-shadow: 0 8px 24px var(--primary-glow);
        }

        /* ── Layout ── */
        .container {
            max-width: 1240px;
            width: 100%;
            margin: 0 auto;
            padding: 64px 40px 100px;
        }

        /* ── Hero ── */
        .hero {
            text-align: center;
            max-width: 820px;
            margin: 0 auto 90px;
            position: relative;
        }
        .hero-badge {
            display: inline-flex;
            align-items: center;
            padding: 7px 16px;
            background: var(--primary-soft);
            border: 1px solid rgba(124, 58, 237, 0.25);
            border-radius: 999px;
            color: var(--accent);
            font-size: 0.8rem;
            font-weight: 500;
            margin-bottom: 28px;
            opacity: 0;
            animation: fadeSlideDown 0.6s ease forwards 0.2s;
        }
        .hero-badge::before {
            content: '';
            width: 7px; height: 7px;
            background: var(--primary);
            border-radius: 50%;
            margin-right: 10px;
            box-shadow: 0 0 12px var(--primary), 0 0 4px var(--primary);
            animation: pulse 2s ease-in-out infinite;
        }
        h1 {
            font-size: clamp(2.8rem, 7vw, 4.5rem);
            font-weight: 900;
            letter-spacing: -0.04em;
            line-height: 1.02;
            margin-bottom: 22px;
            background: linear-gradient(135deg, #ffffff 0%, #c4b5fd 40%, #a855f7 70%, #7c3aed 100%);
            -webkit-background-clip: text;
            background-clip: text;
            -webkit-text-fill-color: transparent;
            opacity: 0;
            animation: fadeSlideUp 0.8s cubic-bezier(0.16, 1, 0.3, 1) forwards 0.35s;
        }
        .hero-subtitle {
            font-size: 1.15rem;
            color: var(--text-muted);
            margin-bottom: 38px;
            max-width: 580px;
            margin-left: auto;
            margin-right: auto;
            opacity: 0;
            animation: fadeSlideUp 0.7s ease forwards 0.55s;
        }
        .hero-ctas {
            display: flex;
            gap: 14px;
            justify-content: center;
            flex-wrap: wrap;
            opacity: 0;
            animation: fadeSlideUp 0.7s ease forwards 0.7s;
        }
        .hero-glow {
            position: absolute;
            top: -60px;
            left: 50%;
            transform: translateX(-50%);
            width: 600px;
            height: 400px;
            background: radial-gradient(ellipse, rgba(124, 58, 237, 0.2) 0%, transparent 70%);
            pointer-events: none;
            z-index: -1;
        }

        /* ── Buttons ── */
        .btn {
            display: inline-flex;
            align-items: center;
            gap: 8px;
            padding: 14px 28px;
            border-radius: 14px;
            font-family: 'Outfit', sans-serif;
            font-size: 0.95rem;
            font-weight: 600;
            text-decoration: none;
            cursor: pointer;
            transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
            border: 1px solid transparent;
            position: relative;
            overflow: hidden;
        }
        .btn::before {
            content: '';
            position: absolute;
            inset: 0;
            background: linear-gradient(135deg, rgba(255,255,255,0.1) 0%, transparent 50%);
            opacity: 0;
            transition: opacity 0.3s;
        }
        .btn:hover::before { opacity: 1; }
        .btn-primary {
            background: var(--primary);
            color: white;
            border-color: rgba(168, 85, 247, 0.3);
            box-shadow: 0 4px 20px var(--primary-glow), inset 0 1px 0 rgba(255,255,255,0.1);
        }
        .btn-primary:hover {
            background: var(--primary-hover);
            transform: translateY(-3px);
            box-shadow: 0 20px 40px -10px var(--primary-glow), inset 0 1px 0 rgba(255,255,255,0.15);
        }
        .btn-ghost {
            background: rgba(124, 58, 237, 0.06);
            color: var(--text-main);
            border-color: var(--card-border);
            backdrop-filter: blur(8px);
        }
        .btn-ghost:hover {
            background: var(--card-bg-hover);
            border-color: var(--card-border-hover);
            transform: translateY(-2px);
        }

        /* ── Stats ── */
        .stats {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 16px;
            margin-bottom: 80px;
        }
        .stat {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 18px;
            padding: 22px 24px;
            transition: all 0.35s cubic-bezier(0.4, 0, 0.2, 1);
            position: relative;
            overflow: hidden;
        }
        .stat::before {
            content: '';
            position: absolute;
            top: 0; left: 0; right: 0;
            height: 2px;
            background: var(--gradient-hero);
            opacity: 0;
            transition: opacity 0.3s;
        }
        .stat:hover::before { opacity: 1; }
        .stat:hover {
            border-color: var(--card-border-hover);
            transform: translateY(-4px);
            box-shadow: 0 20px 40px -15px rgba(124, 58, 237, 0.2);
        }
        .stat-label {
            font-size: 0.72rem;
            color: var(--text-dim);
            text-transform: uppercase;
            letter-spacing: 0.1em;
            font-weight: 600;
            margin-bottom: 8px;
        }
        .stat-value {
            font-family: 'Outfit', sans-serif;
            font-size: 2rem;
            font-weight: 800;
            letter-spacing: -0.03em;
            color: var(--text-main);
        }
        .stat-value small {
            font-size: 0.75rem;
            color: var(--text-muted);
            font-weight: 500;
            margin-left: 4px;
        }

        /* ── Sections ── */
        .section {
            margin-bottom: 90px;
            opacity: 0;
            transform: translateY(30px);
            transition: all 0.8s cubic-bezier(0.16, 1, 0.3, 1);
        }
        .section.visible {
            opacity: 1;
            transform: translateY(0);
        }
        .section-header {
            display: flex;
            align-items: baseline;
            justify-content: space-between;
            gap: 12px;
            margin-bottom: 28px;
            flex-wrap: wrap;
        }
        .section-title {
            font-size: 1.6rem;
            font-weight: 800;
            letter-spacing: -0.02em;
            display: flex;
            align-items: center;
            gap: 12px;
        }
        .section-title .dot {
            width: 10px; height: 10px;
            border-radius: 50%;
            background: var(--primary);
            box-shadow: 0 0 14px var(--primary), 0 0 4px var(--primary);
            animation: pulse 2.5s ease-in-out infinite;
        }
        .section-sub {
            font-size: 0.9rem;
            color: var(--text-dim);
            margin-top: 4px;
        }
        .section-link {
            color: var(--accent);
            text-decoration: none;
            font-size: 0.88rem;
            font-weight: 500;
            padding: 8px 14px;
            border-radius: 10px;
            border: 1px solid transparent;
            transition: all 0.25s;
        }
        .section-link:hover {
            background: var(--primary-soft);
            border-color: var(--card-border);
        }

        /* ── Featured ── */
        .featured-row {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(340px, 1fr));
            gap: 22px;
        }
        .featured-card {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 22px;
            overflow: hidden;
            cursor: pointer;
            transition: all 0.4s cubic-bezier(0.4, 0, 0.2, 1);
            position: relative;
        }
        .featured-card:hover {
            border-color: var(--card-border-hover);
            transform: translateY(-6px) scale(1.01);
            box-shadow: 0 30px 60px -20px rgba(124, 58, 237, 0.3);
        }
        .featured-thumb {
            position: relative;
            width: 100%;
            aspect-ratio: 16 / 9;
            background: var(--bg-surface);
            background-size: cover;
            background-position: center;
            overflow: hidden;
        }
        .featured-thumb::after {
            content: '';
            position: absolute;
            inset: 0;
            background: linear-gradient(to top, var(--bg-deep) 0%, transparent 60%);
        }
        .featured-label {
            position: absolute;
            top: 16px;
            left: 16px;
            z-index: 2;
            padding: 6px 14px;
            background: rgba(6, 2, 15, 0.7);
            backdrop-filter: blur(10px);
            border: 1px solid var(--card-border);
            border-radius: 999px;
            font-size: 0.72rem;
            font-weight: 700;
            font-family: 'Outfit', sans-serif;
            color: var(--accent);
            letter-spacing: 0.08em;
            text-transform: uppercase;
        }
        .featured-body {
            position: absolute;
            left: 20px; right: 20px; bottom: 18px;
            z-index: 2;
        }
        .featured-levelid {
            font-family: 'Outfit', sans-serif;
            font-size: 1.1rem;
            font-weight: 700;
            margin-bottom: 4px;
        }
        .featured-meta {
            font-size: 0.82rem;
            color: var(--text-muted);
        }
        .featured-empty {
            padding: 50px 20px;
            text-align: center;
            color: var(--text-dim);
            font-size: 0.9rem;
        }

        /* ── Gallery grid ── */
        .gallery {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
            gap: 16px;
        }
        .thumb {
            position: relative;
            aspect-ratio: 16 / 9;
            background: var(--bg-surface);
            background-size: cover;
            background-position: center;
            border: 1px solid var(--card-border);
            border-radius: 16px;
            overflow: hidden;
            cursor: zoom-in;
            transition: all 0.35s cubic-bezier(0.4, 0, 0.2, 1);
        }
        .thumb:hover {
            transform: translateY(-5px) scale(1.02);
            border-color: rgba(168, 85, 247, 0.6);
            box-shadow: 0 24px 48px -16px rgba(124, 58, 237, 0.4);
        }
        .thumb-rating {
            position: absolute;
            top: 10px;
            right: 10px;
            z-index: 2;
            padding: 4px 10px;
            background: rgba(6, 2, 15, 0.8);
            backdrop-filter: blur(8px);
            border: 1px solid var(--card-border);
            border-radius: 999px;
            font-size: 0.72rem;
            font-weight: 700;
            font-family: 'Outfit', sans-serif;
            color: var(--gold);
        }
        .thumb-overlay {
            position: absolute;
            inset: 0;
            background: linear-gradient(to top, rgba(6, 2, 15, 0.95) 0%, transparent 60%);
            opacity: 0;
            transition: opacity 0.3s;
            display: flex;
            flex-direction: column;
            justify-content: flex-end;
            padding: 14px 16px;
        }
        .thumb:hover .thumb-overlay { opacity: 1; }
        .thumb-id {
            font-family: 'Outfit', sans-serif;
            font-size: 0.9rem;
            font-weight: 700;
            margin-bottom: 2px;
        }
        .thumb-meta { font-size: 0.74rem; color: var(--text-muted); }

        /* ── Contributors ── */
        .contributors {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
            gap: 14px;
        }
        .contrib {
            display: flex;
            align-items: center;
            gap: 14px;
            padding: 16px 18px;
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 16px;
            text-decoration: none;
            color: inherit;
            transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
        }
        .contrib:hover {
            border-color: var(--card-border-hover);
            background: var(--card-bg-hover);
            transform: translateY(-3px);
            box-shadow: 0 16px 32px -12px rgba(124, 58, 237, 0.2);
        }
        .contrib-avatar {
            width: 44px; height: 44px;
            border-radius: 12px;
            background: var(--gradient-hero);
            display: flex;
            align-items: center;
            justify-content: center;
            font-family: 'Outfit', sans-serif;
            font-weight: 800;
            font-size: 1.1rem;
            color: white;
            flex-shrink: 0;
            box-shadow: 0 4px 12px var(--primary-glow);
        }
        .contrib-info { min-width: 0; flex: 1; }
        .contrib-rank {
            font-size: 0.68rem;
            color: var(--text-dim);
            font-weight: 700;
            letter-spacing: 0.08em;
            font-family: 'Outfit', sans-serif;
        }
        .contrib-name {
            font-size: 0.94rem;
            font-weight: 600;
            color: var(--text-main);
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }
        .contrib-count { font-size: 0.78rem; color: var(--text-muted); }

        /* ── Features bento grid ── */
        .features {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            grid-template-rows: auto auto;
            gap: 18px;
        }
        .feat {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 20px;
            padding: 28px;
            transition: all 0.4s cubic-bezier(0.4, 0, 0.2, 1);
            position: relative;
            overflow: hidden;
        }
        .feat::before {
            content: '';
            position: absolute;
            top: -50%; left: -50%;
            width: 200%; height: 200%;
            background: conic-gradient(from 0deg, transparent, rgba(124, 58, 237, 0.05), transparent 30%);
            animation: featRotate 8s linear infinite;
            opacity: 0;
            transition: opacity 0.4s;
        }
        .feat:hover::before { opacity: 1; }
        @keyframes featRotate {
            100% { transform: rotate(360deg); }
        }
        .feat:hover {
            background: var(--card-bg-hover);
            border-color: var(--card-border-hover);
            transform: translateY(-4px);
            box-shadow: 0 24px 48px -16px rgba(124, 58, 237, 0.15);
        }
        .feat:nth-child(1) { grid-column: span 2; }
        .feat-icon {
            width: 44px; height: 44px;
            background: var(--primary-soft);
            border: 1px solid var(--card-border);
            border-radius: 12px;
            display: flex;
            align-items: center;
            justify-content: center;
            color: var(--accent);
            margin-bottom: 16px;
            position: relative;
            z-index: 1;
        }
        .feat h3 {
            font-size: 1.1rem;
            font-weight: 700;
            margin-bottom: 8px;
            position: relative;
            z-index: 1;
        }
        .feat p {
            color: var(--text-muted);
            font-size: 0.88rem;
            line-height: 1.6;
            position: relative;
            z-index: 1;
        }

        /* ── Moderators ── */
        .moderators-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(360px, 1fr));
            gap: 18px;
        }
        .mod-card {
            background: var(--bg-surface);
            border-radius: 18px;
            overflow: hidden;
            position: relative;
            height: 140px;
            display: flex;
            box-shadow: 0 12px 40px -12px rgba(0,0,0,0.5);
            border: 1px solid var(--card-border);
            transition: all 0.4s cubic-bezier(0.4, 0, 0.2, 1);
        }
        .mod-card:hover {
            transform: translateY(-4px);
            border-color: var(--card-border-hover);
            box-shadow: 0 24px 48px -16px rgba(124, 58, 237, 0.25);
        }
        .mod-bg-left {
            position: absolute;
            left: 0; top: 0; bottom: 0;
            width: 40%;
            background: linear-gradient(135deg, #7c3aed, #a855f7);
            clip-path: polygon(0 0, 100% 0, 80% 100%, 0% 100%);
            z-index: 1;
        }
        .mod-bg-right {
            position: absolute;
            right: 0; top: 0; bottom: 0;
            width: 70%;
            background: linear-gradient(to right, #6d28d9, #7c3aed);
            background-image: radial-gradient(circle at 80% 50%, rgba(255,255,255,0.08) 0%, transparent 50%);
            z-index: 0;
        }
        .mod-content {
            position: relative;
            z-index: 2;
            display: flex;
            width: 100%;
            align-items: center;
            padding: 0 24px;
        }
        .mod-icon-container {
            width: 80px;
            display: flex;
            justify-content: center;
            margin-right: 20px;
        }
        .mod-icon {
            width: 64px; height: 64px;
            filter: drop-shadow(0 4px 12px rgba(0,0,0,0.4));
        }
        .mod-info { flex: 1; display: flex; flex-direction: column; justify-content: center; }
        .mod-name {
            font-family: 'Outfit', sans-serif;
            font-size: 1.4rem;
            font-weight: 900;
            color: var(--gold);
            text-shadow: 2px 2px 0px rgba(0,0,0,0.5);
            margin-bottom: 8px;
            text-transform: uppercase;
            letter-spacing: 1.5px;
        }
        .mod-stats { display: flex; gap: 10px; flex-wrap: wrap; }
        .mod-stat {
            display: flex;
            align-items: center;
            gap: 4px;
            color: #fff;
            font-size: 0.82rem;
            font-weight: 700;
            text-shadow: 1px 1px 2px rgba(0,0,0,0.8);
        }
        .stat-icon { width: 16px; height: 16px; filter: drop-shadow(1px 1px 1px rgba(0,0,0,0.5)); }

        /* ── Viewer ── */
        .viewer {
            position: fixed;
            inset: 0;
            z-index: 100;
            background: rgba(6, 2, 15, 0.94);
            backdrop-filter: blur(16px);
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 40px;
            opacity: 0;
            pointer-events: none;
            transition: opacity 0.35s cubic-bezier(0.4, 0, 0.2, 1);
        }
        .viewer.open { opacity: 1; pointer-events: auto; }
        .viewer-inner {
            max-width: 100%;
            max-height: 100%;
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 18px;
            transform: scale(0.92) translateY(20px);
            transition: transform 0.4s cubic-bezier(0.16, 1, 0.3, 1);
        }
        .viewer.open .viewer-inner { transform: scale(1) translateY(0); }
        .viewer img {
            max-width: min(1200px, 92vw);
            max-height: 78vh;
            width: auto;
            height: auto;
            border-radius: 18px;
            box-shadow: 0 40px 80px rgba(0, 0, 0, 0.7), 0 0 60px rgba(124, 58, 237, 0.15);
        }
        .viewer-meta {
            display: flex;
            align-items: center;
            gap: 16px;
            color: var(--text-muted);
            font-size: 0.92rem;
            flex-wrap: wrap;
            justify-content: center;
        }
        .viewer-meta a {
            color: var(--accent);
            text-decoration: none;
            padding: 8px 16px;
            border: 1px solid rgba(124, 58, 237, 0.3);
            border-radius: 999px;
            transition: all 0.25s;
        }
        .viewer-meta a:hover {
            background: var(--primary-soft);
            border-color: var(--primary);
        }
        .viewer-close {
            position: absolute;
            top: 24px; right: 24px;
            width: 44px; height: 44px;
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            color: var(--text-main);
            border-radius: 12px;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 1.3rem;
            transition: all 0.25s;
        }
        .viewer-close:hover {
            background: var(--primary);
            border-color: var(--primary);
            transform: rotate(90deg);
        }

        /* ── Footer ── */
        footer {
            margin-top: 60px;
            padding: 36px 0;
            border-top: 1px solid var(--card-border);
            text-align: center;
            color: var(--text-dim);
            font-size: 0.85rem;
        }
        footer .links {
            display: flex;
            gap: 24px;
            justify-content: center;
            margin-top: 12px;
            flex-wrap: wrap;
        }
        footer a {
            color: var(--text-muted);
            text-decoration: none;
            transition: color 0.25s;
        }
        footer a:hover { color: var(--text-main); }

        /* ── Skeleton ── */
        .skeleton {
            border-radius: 16px;
            background: linear-gradient(100deg, rgba(124, 58, 237, 0.03) 30%, rgba(124, 58, 237, 0.08) 50%, rgba(124, 58, 237, 0.03) 70%);
            background-size: 200% 100%;
            animation: shimmer 1.6s ease-in-out infinite;
        }
        .skeleton-thumb { aspect-ratio: 16 / 9; }
        .skeleton-row { height: 68px; }

        /* ── State ── */
        .state {
            text-align: center;
            padding: 50px 24px;
            color: var(--text-muted);
            grid-column: 1 / -1;
        }

        /* ── Animations ── */
        @keyframes fadeSlideUp {
            from { opacity: 0; transform: translateY(24px); }
            to { opacity: 1; transform: translateY(0); }
        }
        @keyframes fadeSlideDown {
            from { opacity: 0; transform: translateY(-12px); }
            to { opacity: 1; transform: translateY(0); }
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; transform: scale(1); }
            50% { opacity: 0.5; transform: scale(0.85); }
        }
        @keyframes shimmer {
            0% { background-position: 200% 0; }
            100% { background-position: -200% 0; }
        }

        /* ── Scroll reveal stagger ── */
        .section:nth-child(1) { transition-delay: 0s; }
        .section:nth-child(2) { transition-delay: 0.05s; }
        .section:nth-child(3) { transition-delay: 0.1s; }
        .section:nth-child(4) { transition-delay: 0.15s; }
        .section:nth-child(5) { transition-delay: 0.2s; }

        /* ── Responsive ── */
        @media (max-width: 900px) {
            .stats { grid-template-columns: repeat(2, 1fr); }
            .features { grid-template-columns: 1fr; }
            .feat:nth-child(1) { grid-column: span 1; }
        }
        @media (max-width: 640px) {
            .nav { padding: 14px 18px; }
            .nav.scrolled { padding: 10px 18px; }
            .nav-links .nav-link:not(.nav-cta) { display: none; }
            .container { padding: 40px 20px 60px; }
            .section { margin-bottom: 60px; }
            .hero { margin-bottom: 60px; }
            .stats { grid-template-columns: 1fr 1fr; gap: 10px; }
            .stat { padding: 16px 18px; }
            .stat-value { font-size: 1.6rem; }
            .moderators-grid { grid-template-columns: 1fr; }
            .viewer { padding: 16px; }
            .viewer-close { top: 12px; right: 12px; }
            .featured-row { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
    <div class="ambient-bg" aria-hidden="true">
        <div class="ambient-orb orb-1"></div>
        <div class="ambient-orb orb-2"></div>
        <div class="ambient-orb orb-3"></div>
        <div class="ambient-orb orb-4"></div>
    </div>
    <div class="noise-overlay" aria-hidden="true"></div>
    <div id="background-carousel" aria-hidden="true"></div>

    <nav class="nav" id="main-nav">
        <a href="/paimbnails" class="nav-brand">
            <span class="nav-logo"></span>
            <span>Paimbnails</span>
        </a>
        <div class="nav-links">
            <a href="#gallery" class="nav-link">Gallery</a>
            <a href="/paimbnails/guidelines" class="nav-link">Guidelines</a>
            <a href="https://discord.com/invite/NzUfZmC3mm" target="_blank" rel="noopener" class="nav-link">Discord</a>
            <a href="/paimbnails/donate" class="nav-link">Donate</a>
            <a href="https://api.flozwer.org/download" class="nav-link nav-cta">Download</a>
        </div>
    </nav>

    <div class="container">
        <header class="hero">
            <div class="hero-glow"></div>
            <span class="hero-badge">v1.0.1 \xB7 Geode v5 \xB7 GD 2.2081</span>
            <h1>Thumbnails, reimagined for Geometry Dash.</h1>
            <p class="hero-subtitle">
                Community-crafted level previews, visual effects, emotes, pets and profiles \u2014
                seamlessly woven into the game you love.
            </p>
            <div class="hero-ctas">
                <a href="https://api.flozwer.org/download" class="btn btn-primary">
                    <svg width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
                    Download the mod
                </a>
                <a href="#gallery" class="btn btn-ghost">
                    <svg width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" viewBox="0 0 24 24"><rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>
                    Browse gallery
                </a>
            </div>
        </header>

        <section class="stats" id="stats">
            <div class="stat">
                <div class="stat-label">Top thumbnails</div>
                <div class="stat-value" id="stat-total">\u2014</div>
            </div>
            <div class="stat">
                <div class="stat-label">Contributors</div>
                <div class="stat-value" id="stat-contrib">\u2014</div>
            </div>
            <div class="stat">
                <div class="stat-label">Avg. rating</div>
                <div class="stat-value" id="stat-rating">\u2014<small>/5</small></div>
            </div>
            <div class="stat">
                <div class="stat-label">Total votes</div>
                <div class="stat-value" id="stat-votes">\u2014</div>
            </div>
        </section>

        <section class="section">
            <div class="section-header">
                <div>
                    <h2 class="section-title"><span class="dot"></span>Featured</h2>
                    <div class="section-sub">Hand-picked daily &amp; weekly levels</div>
                </div>
            </div>
            <div class="featured-row">
                <div class="featured-card" id="daily-card">
                    <div class="featured-empty">Loading daily\u2026</div>
                </div>
                <div class="featured-card" id="weekly-card">
                    <div class="featured-empty">Loading weekly\u2026</div>
                </div>
            </div>
        </section>

        <section class="section" id="gallery">
            <div class="section-header">
                <div>
                    <h2 class="section-title"><span class="dot"></span>Top-rated thumbnails</h2>
                    <div class="section-sub">Ranked by community votes</div>
                </div>
                <a href="#gallery" class="section-link" id="refresh-link">Refresh \u21BB</a>
            </div>
            <div id="gallery-container" class="gallery">
                <div class="skeleton skeleton-thumb"></div>
                <div class="skeleton skeleton-thumb"></div>
                <div class="skeleton skeleton-thumb"></div>
                <div class="skeleton skeleton-thumb"></div>
                <div class="skeleton skeleton-thumb"></div>
                <div class="skeleton skeleton-thumb"></div>
                <div class="skeleton skeleton-thumb"></div>
                <div class="skeleton skeleton-thumb"></div>
            </div>
        </section>

        <section class="section">
            <div class="section-header">
                <div>
                    <h2 class="section-title"><span class="dot"></span>Top contributors</h2>
                    <div class="section-sub">Creators powering the gallery</div>
                </div>
            </div>
            <div id="contrib-container" class="contributors">
                <div class="skeleton skeleton-row"></div>
                <div class="skeleton skeleton-row"></div>
                <div class="skeleton skeleton-row"></div>
                <div class="skeleton skeleton-row"></div>
                <div class="skeleton skeleton-row"></div>
                <div class="skeleton skeleton-row"></div>
            </div>
        </section>

        <section class="section">
            <div class="section-header">
                <div>
                    <h2 class="section-title"><span class="dot"></span>Everything in one mod</h2>
                    <div class="section-sub">A unified visual layer for Geometry Dash</div>
                </div>
            </div>
            <div class="features">
                <div class="feat">
                    <div class="feat-icon">
                        <svg width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" viewBox="0 0 24 24"><rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>
                    </div>
                    <h3>Level thumbnails</h3>
                    <p>Instant previews on every cell \u2014 search, lists, gauntlets, dailies and official levels. The gallery grows daily with community contributions.</p>
                </div>
                <div class="feat">
                    <div class="feat-icon">
                        <svg width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" viewBox="0 0 24 24"><path d="M12 3l3 6 6 1-4.5 4 1 6-5.5-3-5.5 3 1-6L3 10l6-1z"/></svg>
                    </div>
                    <h3>17 visual effects</h3>
                    <p>Blur, bloom, glitch, CRT, rain, matrix and more \u2014 stack up to 4 for unique looks.</p>
                </div>
                <div class="feat">
                    <div class="feat-icon">
                        <svg width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" viewBox="0 0 24 24"><path d="M3 12a9 9 0 0 1 18 0 9 9 0 0 1-18 0z"/><path d="M9 9l6 6M15 9l-6 6"/></svg>
                    </div>
                    <h3>30+ transitions</h3>
                    <p>Fades, slides, flips, zooms, page curls, radial wipes \u2014 fully scriptable.</p>
                </div>
                <div class="feat">
                    <div class="feat-icon">
                        <svg width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" viewBox="0 0 24 24"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>
                    </div>
                    <h3>Dynamic audio</h3>
                    <p>Song preview while browsing, profile music, reactive beat detection on the leaderboard.</p>
                </div>
                <div class="feat">
                    <div class="feat-icon">
                        <svg width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" viewBox="0 0 24 24"><circle cx="12" cy="8" r="4"/><path d="M4 21a8 8 0 0 1 16 0"/></svg>
                    </div>
                    <h3>Profile features</h3>
                    <p>Rate profiles, leave reviews, animated GIF avatars for VIPs, moderators and admins.</p>
                </div>
                <div class="feat">
                    <div class="feat-icon">
                        <svg width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" viewBox="0 0 24 24"><path d="M12 3l9 4-9 4-9-4 9-4z"/><path d="M3 11l9 4 9-4"/><path d="M3 15l9 4 9-4"/></svg>
                    </div>
                    <h3>Pets &amp; emotes</h3>
                    <p>Adopt custom pets, react with emotes in comments \u2014 all with multi-layer caching for speed.</p>
                </div>
            </div>
        </section>

        <section class="section">
            <div class="section-header">
                <div>
                    <h2 class="section-title"><span class="dot"></span>Special thanks</h2>
                    <div class="section-sub">The moderators &amp; admins keeping the gallery safe</div>
                </div>
            </div>
            <div class="moderators-grid" id="moderatorsGrid">
                <div class="skeleton skeleton-row"></div>
                <div class="skeleton skeleton-row"></div>
                <div class="skeleton skeleton-row"></div>
                <div class="skeleton skeleton-row"></div>
            </div>
        </section>

        <footer>
            <p>Paimbnails \xB7 Built for Geometry Dash 2.2081 on Geode v5 \xB7 Not affiliated with RobTop Games or HoYoverse.</p>
            <div class="links">
                <a href="https://api.flozwer.org/download">Download</a>
                <a href="/paimbnails/guidelines">Guidelines</a>
                <a href="/paimbnails/donate">Donate</a>
                <a href="https://discord.com/invite/NzUfZmC3mm" target="_blank" rel="noopener">Discord</a>
                <a href="https://github.com/Fl0zWer/Paimbnails" target="_blank" rel="noopener">GitHub</a>
            </div>
        </footer>
    </div>

    <div class="viewer" id="viewer" aria-hidden="true">
        <button class="viewer-close" id="viewer-close" aria-label="Close">\u2715</button>
        <div class="viewer-inner">
            <img id="viewer-img" alt="">
            <div class="viewer-meta" id="viewer-meta"></div>
        </div>
    </div>

    <script>
        const API = 'https://api.flozwer.org';
        const THUMB_URL = (id, ext) => \`\${API}/api/download/thumbnails/\${id}.\${ext || 'webp'}\`;
        const GDB_URL   = (id) => \`https://gdbrowser.com/\${id}\`;

        const $ = (sel) => document.querySelector(sel);
        const esc = (s) => String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({
            '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
        }[c]));
        const initial = (s) => (String(s || 'U').trim()[0] || 'U').toUpperCase();
        const fmt = (n) => (n == null ? '\u2014' : Number(n).toLocaleString('en-US'));
        function shuffle(list) {
            const items = [...list];
            for (let i = items.length - 1; i > 0; i--) {
                const j = Math.floor(Math.random() * (i + 1));
                [items[i], items[j]] = [items[j], items[i]];
            }
            return items;
        }

        async function getJSON(path) {
            const r = await fetch(API + path, { headers: { 'Accept': 'application/json' } });
            if (!r.ok) throw new Error(\`\${path} \u2192 \${r.status}\`);
            return r.json();
        }

        // ── Nav scroll effect ──
        const nav = document.getElementById('main-nav');
        let lastScroll = 0;
        window.addEventListener('scroll', () => {
            const y = window.scrollY;
            nav.classList.toggle('scrolled', y > 60);
            lastScroll = y;
        }, { passive: true });

        // ── Scroll reveal (IntersectionObserver) ──
        const revealObserver = new IntersectionObserver((entries) => {
            entries.forEach(entry => {
                if (entry.isIntersecting) {
                    entry.target.classList.add('visible');
                    revealObserver.unobserve(entry.target);
                }
            });
        }, { threshold: 0.08, rootMargin: '0px 0px -40px 0px' });
        document.querySelectorAll('.section').forEach(el => revealObserver.observe(el));

        // ── Background carousel ──
        function initCarousel(thumbnails = []) {
            const container = document.getElementById('background-carousel');
            const ids = shuffle([...new Set(thumbnails.map((item) => String(item.levelId || '')).filter(Boolean))]);
            container.innerHTML = '';
            if (!ids.length) return;
            const rowCount = 5;
            for (let i = 0; i < rowCount; i++) {
                const row = document.createElement('div');
                row.classList.add('carousel-row', i % 2 === 0 ? 'scroll-right' : 'scroll-left');
                row.style.animationDuration = \`\${80 + Math.random() * 50}s\`;
                const items = shuffle(ids).slice(0, Math.min(18, ids.length));
                const build = () => items.forEach((id) => {
                    const d = document.createElement('div');
                    d.classList.add('carousel-item');
                    d.style.backgroundImage = \`url('\${THUMB_URL(id)}')\`;
                    row.appendChild(d);
                });
                build(); build(); build();
                container.appendChild(row);
            }
        }

        // ── Live data ──
        async function loadFeatured(kind, selector) {
            const el = $(selector);
            try {
                const data = await getJSON(\`/api/\${kind}/current\`);
                const d = data && data.data;
                if (!d || !d.levelID) {
                    el.innerHTML = \`<div class="featured-empty">No \${kind} level set.</div>\`;
                    return;
                }
                const id = d.levelID, who = d.setBy || 'Unknown';
                const until = d.expiresAt ? new Date(d.expiresAt) : null;
                const label = kind === 'daily' ? 'Daily' : 'Weekly';
                el.innerHTML = \`
                    <div class="featured-thumb" style="background-image:url('\${THUMB_URL(id)}')"></div>
                    <span class="featured-label">\${label}</span>
                    <div class="featured-body">
                        <div class="featured-levelid">Level \${esc(id)}</div>
                        <div class="featured-meta">
                            Set by <strong style="color:var(--text-main)">\${esc(who)}</strong>
                            \${until ? \`\xB7 until \${esc(until.toLocaleDateString())}\` : ''}
                        </div>
                    </div>\`;
                el.onclick = () => openViewer(id, who, \`\${label} pick\`);
            } catch (e) {
                console.error(e);
                el.innerHTML = \`<div class="featured-empty">No \${kind} level set.</div>\`;
            }
        }

        async function loadTopThumbnails() {
            const container = $('#gallery-container');
            try {
                const data = await getJSON('/api/top-thumbnails?page=0&limit=100');
                const list = Array.isArray(data.thumbnails) ? data.thumbnails : [];
                updateStatsFromThumbs(list, data.total);
                initCarousel(list);
                if (!list.length) {
                    container.innerHTML = \`<div class="state">The gallery is still warming up. Check back soon.</div>\`;
                    return;
                }
                container.innerHTML = list.slice(0, 12).map(renderThumb).join('');
            } catch (e) {
                console.error(e);
                initCarousel([]);
                container.innerHTML = \`<div class="state">Couldn't load the top thumbnails right now.</div>\`;
            }
        }

        function renderThumb(t) {
            const id = t.levelId;
            const who = t.uploadedBy || 'Unknown';
            const rating = t.rating ? t.rating.toFixed(2) : null;
            const votes = t.count || 0;
            return \`
                <div class="thumb"
                     data-id="\${esc(id)}"
                     data-uploader="\${esc(who)}"
                     style="background-image:url('\${THUMB_URL(id)}')">
                    \${rating ? \`<div class="thumb-rating">\u2605 \${esc(rating)}</div>\` : ''}
                    <div class="thumb-overlay">
                        <div class="thumb-id">Level \${esc(id)}</div>
                        <div class="thumb-meta">by \${esc(who)}\${votes ? \` \xB7 \${fmt(votes)} votes\` : ''}</div>
                    </div>
                </div>\`;
        }

        function updateStatsFromThumbs(list, total) {
            const contrib = new Set(list.map((t) => (t.uploadedBy || '').toLowerCase()).filter(Boolean)).size;
            const withRating = list.filter((t) => typeof t.rating === 'number');
            const avg = withRating.length ? withRating.reduce((s, t) => s + t.rating, 0) / withRating.length : 0;
            const votes = list.reduce((s, t) => s + (t.count || 0), 0);
            $('#stat-total').textContent   = fmt(total || list.length);
            $('#stat-contrib').textContent = fmt(contrib);
            $('#stat-rating').innerHTML    = (avg ? avg.toFixed(2) : '\u2014') + '<small>/5</small>';
            $('#stat-votes').textContent   = fmt(votes);
        }

        async function loadContributors() {
            const el = $('#contrib-container');
            try {
                const data = await getJSON('/api/top-creators?page=0&limit=6');
                const creators = Array.isArray(data.creators) ? data.creators : [];
                if (!creators.length) {
                    el.innerHTML = \`<div class="state">No contributors yet.</div>\`;
                    return;
                }
                el.innerHTML = creators.map((c, i) => renderContributor(c, i + 1)).join('');
            } catch (e) {
                console.error(e);
                el.innerHTML = \`<div class="state">Couldn't load contributors.</div>\`;
            }
        }

        function renderContributor(c, rank) {
            const name = c.username || 'Unknown';
            const uploads = c.uploadCount || 0;
            const rating = c.avgRating ? \` \xB7 \${c.avgRating.toFixed(2)}\u2605\` : '';
            const href = c.accountID ? \`https://gdbrowser.com/u/\${c.accountID}\` : '#';
            return \`
                <a class="contrib" href="\${esc(href)}" target="_blank" rel="noopener">
                    <div class="contrib-avatar">\${esc(initial(name))}</div>
                    <div class="contrib-info">
                        <div class="contrib-rank">#\${rank}</div>
                        <div class="contrib-name">\${esc(name)}</div>
                        <div class="contrib-count">\${fmt(uploads)} upload\${uploads === 1 ? '' : 's'}\${rating}</div>
                    </div>
                </a>\`;
        }

        async function loadModerators() {
            const grid = document.getElementById('moderatorsGrid');
            try {
                const res = await fetch(API + '/api/moderators');
                if (!res.ok) throw new Error('Failed to load moderators');
                const data = await res.json();
                const moderators = data.moderators || [];
                if (!moderators.length) {
                    grid.innerHTML = '<div class="state">No moderators yet.</div>';
                    return;
                }
                grid.innerHTML = '';
                const rgbToHex = (rgb) => {
                    if (!rgb) return null;
                    const h = (c) => {
                        const s = c.toString(16);
                        return s.length === 1 ? '0' + s : s;
                    };
                    return h(rgb.r) + h(rgb.g) + h(rgb.b);
                };
                for (const modData of moderators) {
                    const username = typeof modData === 'string' ? modData : modData.username;
                    try {
                        const gdRes = await fetch(\`https://gdbrowser.com/api/profile/\${username}\`);
                        if (!gdRes.ok) continue;
                        const user = await gdRes.json();
                        const col1 = rgbToHex(user.col1RGB) || user.col1;
                        const col2 = rgbToHex(user.col2RGB) || user.col2;
                        const iconUrl = \`https://gdbrowser.com/icon/\${user.username}?icon=\${user.icon}&col1=\${col1}&col2=\${col2}&glow=\${user.glow ? 1 : 0}\`;
                        const card = document.createElement('div');
                        card.className = 'mod-card';
                        card.innerHTML = \`
                            <div class="mod-bg-left"></div>
                            <div class="mod-bg-right"></div>
                            <div class="mod-content">
                                <div class="mod-icon-container">
                                    <img src="\${iconUrl}" alt="\${esc(user.username)}" class="mod-icon" loading="lazy">
                                </div>
                                <div class="mod-info">
                                    <div class="mod-name">\${esc(user.username)}</div>
                                    <div class="mod-stats">
                                        <div class="mod-stat" title="Stars"><img src="https://gdbrowser.com/assets/star.png" class="stat-icon">\${user.stars}</div>
                                        <div class="mod-stat" title="Moons"><img src="https://gdbrowser.com/assets/moon.png" class="stat-icon">\${user.moons}</div>
                                        <div class="mod-stat" title="Diamonds"><img src="https://gdbrowser.com/assets/diamond.png" class="stat-icon">\${user.diamonds}</div>
                                        <div class="mod-stat" title="User coins"><img src="https://gdbrowser.com/assets/silvercoin.png" class="stat-icon">\${user.userCoins}</div>
                                        <div class="mod-stat" title="Demons"><img src="https://gdbrowser.com/assets/demon.png" class="stat-icon">\${user.demons}</div>
                                        <div class="mod-stat" title="Creator Points"><img src="https://gdbrowser.com/assets/cp.png" class="stat-icon">\${user.cp}</div>
                                    </div>
                                </div>
                            </div>\`;
                        grid.appendChild(card);
                    } catch (e) {
                        console.error(\`Failed to load data for \${username}\`, e);
                    }
                }
            } catch (error) {
                console.error(error);
                grid.innerHTML = '<div class="state">Failed to load moderators.</div>';
            }
        }

        // ── Viewer ──
        const viewer = $('#viewer');
        const viewerImg = $('#viewer-img');
        const viewerMeta = $('#viewer-meta');
        function openViewer(id, uploader, extra) {
            viewerImg.src = THUMB_URL(id);
            viewerImg.alt = \`Level \${id}\`;
            viewerMeta.innerHTML = \`
                <span>Level <strong style="color:var(--text-main)">\${esc(id)}</strong>\${uploader ? \` \xB7 by \${esc(uploader)}\` : ''}\${extra ? \` \xB7 \${esc(extra)}\` : ''}</span>
                <a href="\${GDB_URL(id)}" target="_blank" rel="noopener">Open on GDBrowser \u2197</a>\`;
            viewer.classList.add('open');
            viewer.setAttribute('aria-hidden', 'false');
            document.body.style.overflow = 'hidden';
        }
        function closeViewer() {
            viewer.classList.remove('open');
            viewer.setAttribute('aria-hidden', 'true');
            document.body.style.overflow = '';
        }
        $('#viewer-close').addEventListener('click', closeViewer);
        viewer.addEventListener('click', (e) => { if (e.target === viewer) closeViewer(); });
        document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeViewer(); });

        $('#gallery-container').addEventListener('click', (e) => {
            const card = e.target.closest('.thumb');
            if (!card) return;
            openViewer(card.dataset.id, card.dataset.uploader);
        });

        $('#refresh-link').addEventListener('click', (e) => {
            e.preventDefault();
            $('#gallery-container').innerHTML = Array(8).fill('<div class="skeleton skeleton-thumb"></div>').join('');
            loadTopThumbnails();
        });

        // ── Init ──
        loadFeatured('daily',  '#daily-card');
        loadFeatured('weekly', '#weekly-card');
        loadTopThumbnails();
        loadContributors();
        loadModerators();
    <\/script>
</body>
</html>`;

// src/pages/guidelines.js
var guidelinesHtml = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <link rel="icon" type="image/png" href="https://api.flozwer.org/favicon.png">
    <title>Paimon Thumbnails - Guidelines</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;800&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #020203;
            --card-bg: rgba(255, 255, 255, 0.03);
            --card-border: rgba(255, 255, 255, 0.08);
            --primary: #2563eb;
            --primary-hover: #1d4ed8;
            --text-main: #ffffff;
            --text-muted: #a1a1aa;
            --success: #22c55e;
            --error: #ef4444;
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }

        body {
            font-family: 'Inter', sans-serif;
            background-color: var(--bg-color);
            color: var(--text-main);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            padding: 40px 20px;
            line-height: 1.6;
        }

        .container {
            max-width: 800px;
            width: 100%;
            z-index: 1;
        }

        h1 {
            font-size: 3rem;
            font-weight: 800;
            margin-bottom: 1rem;
            background: linear-gradient(135deg, #fff 0%, #93c5fd 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            text-align: center;
        }

        .subtitle {
            text-align: center;
            color: var(--text-muted);
            margin-bottom: 3rem;
            font-size: 1.1rem;
        }

        .card {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 16px;
            padding: 2rem;
            margin-bottom: 2rem;
            backdrop-filter: blur(10px);
        }

        h2 {
            font-size: 1.5rem;
            margin-bottom: 1.5rem;
            color: var(--primary);
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }

        .rule-list {
            list-style: none;
            display: flex;
            flex-direction: column;
            gap: 1rem;
        }

        .rule-item {
            display: flex;
            gap: 1rem;
            align-items: flex-start;
        }

        .icon {
            flex-shrink: 0;
            width: 24px;
            height: 24px;
            display: flex;
            align-items: center;
            justify-content: center;
            border-radius: 50%;
            font-weight: bold;
        }

        .icon.check {
            background: rgba(34, 197, 94, 0.1);
            color: var(--success);
        }

        .icon.cross {
            background: rgba(239, 68, 68, 0.1);
            color: var(--error);
        }

        .rule-content h3 {
            font-size: 1.1rem;
            margin-bottom: 0.25rem;
            font-weight: 600;
        }

        .rule-content p {
            color: var(--text-muted);
            font-size: 0.95rem;
        }

        .btn {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            padding: 12px 24px;
            background: var(--primary);
            color: white;
            text-decoration: none;
            border-radius: 12px;
            font-weight: 600;
            transition: all 0.2s;
            border: none;
            cursor: pointer;
            font-size: 1rem;
        }

        .btn:hover {
            background: var(--primary-hover);
            transform: translateY(-1px);
        }

        .btn-secondary {
            background: rgba(255, 255, 255, 0.1);
        }

        .btn-secondary:hover {
            background: rgba(255, 255, 255, 0.15);
        }

        .footer {
            margin-top: 4rem;
            text-align: center;
            color: var(--text-muted);
            font-size: 0.9rem;
        }

        /* Background Gradient */
        .bg-gradient {
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: radial-gradient(circle at 50% 0%, rgba(139, 92, 246, 0.15), transparent 70%);
            z-index: -1;
            pointer-events: none;
        }

    </style>
</head>
<body>
    <div class="bg-gradient"></div>

    <div class="container">
        <h1>Thumbnail Guidelines</h1>
        <p class="subtitle">Follow these rules to ensure your thumbnails are approved quickly.</p>

        <div class="card">
            <h2>
                <span class="icon check">\u2713</span>
                What to Do
            </h2>
            <ul class="rule-list">
                <li class="rule-item">
                    <div class="icon check">\u2713</div>
                    <div class="rule-content">
                        <h3>Clean Gameplay</h3>
                        <p>Capture clear, unobstructed views of the level's gameplay or decoration.</p>
                    </div>
                </li>
                <li class="rule-item">
                    <div class="icon check">\u2713</div>
                    <div class="rule-content">
                        <h3>High Quality</h3>
                        <p>Ensure the image is crisp and not pixelated. Use the highest quality settings available.</p>
                    </div>
                </li>
                <li class="rule-item">
                    <div class="icon check">\u2713</div>
                    <div class="rule-content">
                        <h3>Representative</h3>
                        <p>Choose a frame that best represents the level's theme and style.</p>
                    </div>
                </li>
            </ul>
        </div>

        <div class="card">
            <h2>
                <span class="icon cross">\u2715</span>
                What to Avoid
            </h2>
            <ul class="rule-list">
                <li class="rule-item">
                    <div class="icon cross">\u2715</div>
                    <div class="rule-content">
                        <h3>No External UI</h3>
                        <p>Do not include any external overlays, FPS counters, cheat indicators, or menu buttons. The screenshot must be pure gameplay.</p>
                    </div>
                </li>
                <li class="rule-item">
                    <div class="icon cross">\u2715</div>
                    <div class="rule-content">
                        <h3>No Text Overlays</h3>
                        <p>Avoid adding text like "Verified by X" or level names unless they are part of the level's decoration.</p>
                    </div>
                </li>
                <li class="rule-item">
                    <div class="icon cross">\u2715</div>
                    <div class="rule-content">
                        <h3>No Edited Images</h3>
                        <p>Do not use Photoshop or other tools to heavily alter the colors or add effects that aren't in the level.</p>
                    </div>
                </li>
            </ul>
        </div>

        <div style="text-align: center; display: flex; gap: 1rem; justify-content: center;">
            <a href="/paimbnails" class="btn btn-secondary">Back to Home</a>
        </div>

        <div class="footer">
            <p>&copy; 2025 Paimon Thumbnails. All rights reserved.</p>
        </div>
    </div>
</body>
</html>`;

// src/pages/donate.js
var donateHtml = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <link rel="icon" type="image/png" href="https://api.flozwer.org/favicon.png">
    <title>Paimon Thumbnails - Donate</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;800&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #020203;
            --card-bg: rgba(255, 255, 255, 0.03);
            --card-border: rgba(255, 255, 255, 0.08);
            --primary: #2563eb;
            --primary-hover: #1d4ed8;
            --text-main: #ffffff;
            --text-muted: #a1a1aa;
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }

        body {
            font-family: 'Inter', sans-serif;
            background-color: var(--bg-color);
            color: var(--text-main);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            overflow-x: hidden;
            position: relative;
        }

        /* Ambient Background */
        .bg-overlay {
            position: fixed;
            inset: 0;
            z-index: -1;
            background: radial-gradient(circle at 50% 0%, rgba(37, 99, 235, 0.15) 0%, rgba(2,2,3,0.95) 100%);
            pointer-events: none;
        }

        header {
            width: 100%;
            padding: 24px 40px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            z-index: 10;
            position: sticky;
            top: 0;
            backdrop-filter: blur(12px);
            border-bottom: 1px solid var(--card-border);
            background: rgba(2, 2, 3, 0.6);
        }

        .logo {
            text-decoration: none;
        }

        .logo h1 {
            margin: 0;
            font-size: 1.5rem;
            font-weight: 800;
            background: linear-gradient(to bottom right, #fff, #a1a1aa);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            letter-spacing: -0.02em;
        }

        .container {
            max-width: 800px;
            width: 100%;
            padding: 60px 20px;
            z-index: 1;
            display: flex;
            flex-direction: column;
            align-items: center;
            flex-grow: 1;
            justify-content: center;
        }

        .donate-card {
            background: #09090b;
            border: 1px solid var(--card-border);
            border-radius: 20px;
            padding: 50px;
            text-align: center;
            width: 100%;
            box-shadow: 0 20px 40px rgba(0,0,0,0.5);
            position: relative;
            overflow: hidden;
            animation: fadeUp 0.8s ease-out;
        }
        
        .donate-card::before {
            content: '';
            position: absolute;
            top: 0; left: 0; right: 0;
            height: 4px;
            background: linear-gradient(90deg, #3b82f6, #2563eb, #ec4899);
        }

        .icon-container {
            width: 80px;
            height: 80px;
            background: rgba(37, 99, 235, 0.1);
            background: linear-gradient(135deg, rgba(37,99,235,0.2), rgba(236,72,153,0.2));
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            margin: 0 auto 24px;
            color: #ec4899;
            box-shadow: 0 0 30px rgba(236,72,153,0.2);
        }

        h2 {
            font-size: 2.5rem;
            margin-bottom: 16px;
            font-weight: 800;
            background: linear-gradient(to bottom right, #fff, #e2e8f0);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        p {
            font-size: 1.15rem;
            color: var(--text-muted);
            line-height: 1.6;
            margin-bottom: 30px;
        }

        .instruction-box {
            background: rgba(255,255,255,0.05);
            border: 1px dashed rgba(255,255,255,0.2);
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 40px;
        }

        .instruction-box p {
            margin: 0;
            font-size: 1rem;
            color: #e2e8f0;
        }

        .instruction-box strong {
            color: #facc15;
            font-weight: 700;
        }

        .donate-btn {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            gap: 12px;
            background: linear-gradient(135deg, #FF5E5B, #ec4899);
            color: white;
            text-decoration: none;
            padding: 18px 40px;
            border-radius: 30px;
            font-size: 1.25rem;
            font-weight: 700;
            transition: all 0.3s ease;
            box-shadow: 0 10px 25px rgba(255, 94, 91, 0.4);
        }

        .donate-btn:hover {
            transform: translateY(-3px);
            box-shadow: 0 15px 35px rgba(255, 94, 91, 0.6);
        }

        .back-link {
            margin-top: 30px;
            display: inline-block;
            color: var(--text-muted);
            text-decoration: none;
            font-size: 0.95rem;
            transition: color 0.2s;
        }

        .back-link:hover {
            color: white;
        }

        footer {
            margin-top: auto;
            padding: 40px;
            text-align: center;
            color: var(--text-muted);
            font-size: 0.9rem;
            width: 100%;
            border-top: 1px solid var(--card-border);
            background: rgba(2, 2, 3, 0.8);
            backdrop-filter: blur(12px);
        }

        @keyframes fadeUp {
            from { opacity: 0; transform: translateY(20px); }
            to { opacity: 1; transform: translateY(0); }
        }

        @media (max-width: 600px) {
            .donate-card { padding: 30px 20px; }
            h2 { font-size: 2rem; }
        }
    </style>
</head>
<body>
    <div class="bg-overlay"></div>

    <header>
        <a href="/paimbnails" class="logo">
            <h1>Paimon Thumbnails</h1>
        </a>
    </header>

    <div class="container">
        <div class="donate-card">
            <div class="icon-container">
                <svg width="40" height="40" fill="currentColor" viewBox="0 0 24 24">
                    <path d="M12 21.35l-1.45-1.32C5.4 15.36 2 12.28 2 8.5 2 5.42 4.42 3 7.5 3c1.74 0 3.41.81 4.5 2.09C13.09 3.81 14.76 3 16.5 3 19.58 3 22 5.42 22 8.5c0 3.78-3.4 6.86-8.55 11.54L12 21.35z"/>
                </svg>
            </div>
            
            <h2>Support the Project</h2>
            <p>Running the servers and adding new features takes time and resources. If you enjoy using Paimon Thumbnails, consider supporting us on Ko-fi to keep the project alive and thriving!</p>

            <div class="instruction-box">
                <p>\u26A0\uFE0F <strong>Important:</strong> Please include your <strong>Geometry Dash username</strong> in your Ko-fi donation message !</p>
            </div>

            <a href="https://ko-fi.com/flozwer" target="_blank" rel="noopener noreferrer" class="donate-btn">
                Donate on Ko-fi
                <svg width="24" height="24" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 6H6a2 2 0 00-2 2v10a2 2 0 002 2h10a2 2 0 002-2v-4M14 4h6m0 0v6m0-6L10 14"></path></svg>
            </a>

            <br>
            <a href="/paimbnails" class="back-link">\u2190 Back to Home</a>
        </div>
    </div>

    <footer>
        <p>&copy; 2025 Paimon Thumbnails. Not affiliated with HoYoverse or RobTop Games.</p>
    </footer>
</body>
</html>`;

// src/pages/download.js
var RELEASE_URL = MOD_RELEASE_URL;
var WINDOWS_DOWNLOAD_URL = getGitHubReleaseAssetUrl("Paimbnails-Installer.exe");
var MACOS_DOWNLOAD_URL = getGitHubReleaseAssetUrl("Paimbnails-macOS.zip");
var LINUX_DOWNLOAD_URL = getGitHubReleaseAssetUrl("Paimbnails-Linux.tar.gz");
var GEODE_DOWNLOAD_URL = getGitHubReleaseAssetUrl("flozwer.paimbnails2.geode");
var downloadHtml = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <link rel="icon" type="image/png" href="/favicon.png">
    <title>Download \u2014 Paimbnails</title>
    <meta name="description" content="Download Paimbnails for Geometry Dash">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #020203;
            --card-bg: rgba(255, 255, 255, 0.03);
            --card-bg-hover: rgba(255, 255, 255, 0.06);
            --card-border: rgba(255, 255, 255, 0.08);
            --card-border-hover: rgba(255, 255, 255, 0.15);
            --primary: #2563eb;
            --primary-hover: #1d4ed8;
            --primary-soft: rgba(37, 99, 235, 0.12);
            --accent: #bfdbfe;
            --text-main: #ffffff;
            --text-muted: #a1a1aa;
            --text-dim: #71717a;
            --success: #10b981;
            --windows: #0078d4;
            --macos: #ffffff;
            --linux: #e95420;
            --android: #3ddc84;
             --ios: #000000;
             --geode: #8b5cf6;
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }
        html { scroll-behavior: smooth; }

        body {
            font-family: 'Inter', sans-serif;
            background-color: var(--bg-color);
            color: var(--text-main);
            min-height: 100vh;
            overflow-x: hidden;
            position: relative;
            line-height: 1.5;
        }

        .bg-overlay {
            position: fixed;
            inset: 0;
            z-index: -1;
            background: radial-gradient(circle at 50% 0%, rgba(139, 92, 246, 0.18) 0%, transparent 55%),
                        radial-gradient(circle at center, rgba(2, 2, 3, 0.35) 0%, rgba(2, 2, 3, 0.96) 75%);
            pointer-events: none;
        }

        .nav {
            position: sticky;
            top: 0;
            z-index: 50;
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 16px 32px;
            background: rgba(2, 2, 3, 0.7);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border-bottom: 1px solid var(--card-border);
        }
        .nav-brand {
            display: flex;
            align-items: center;
            gap: 10px;
            font-weight: 700;
            font-size: 1.05rem;
            letter-spacing: -0.01em;
            color: var(--text-main);
            text-decoration: none;
        }
        .nav-logo {
            width: 28px;
            height: 28px;
            border-radius: 8px;
            background: linear-gradient(135deg, #2563eb, #3b82f6);
            box-shadow: 0 0 16px rgba(37, 99, 235, 0.35);
        }
        .nav-links { display: flex; gap: 6px; align-items: center; }
        .nav-link {
            padding: 8px 14px;
            color: var(--text-muted);
            text-decoration: none;
            font-size: 0.9rem;
            font-weight: 500;
            border-radius: 10px;
            transition: color 0.2s, background 0.2s;
        }
        .nav-link:hover { color: var(--text-main); background: var(--card-bg); }
        .nav-cta {
            padding: 8px 16px;
            background: var(--primary);
            color: white !important;
            border-radius: 10px;
            font-weight: 600;
        }
        .nav-cta:hover { background: var(--primary-hover) !important; }

        .container {
            max-width: 900px;
            width: 100%;
            margin: 0 auto;
            padding: 56px 32px 80px;
        }

        .page-header {
            text-align: center;
            margin-bottom: 48px;
        }
        .page-title {
            font-size: 2.5rem;
            font-weight: 800;
            letter-spacing: -0.03em;
            margin-bottom: 12px;
            background: linear-gradient(to bottom right, #fff, #bfdbfe);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .page-subtitle {
            color: var(--text-muted);
            font-size: 1.1rem;
        }

        .detected-platform {
            background: var(--primary-soft);
            border: 1px solid rgba(139, 92, 246, 0.3);
            border-radius: 12px;
            padding: 16px 24px;
            margin-bottom: 32px;
            display: flex;
            align-items: center;
            gap: 12px;
            font-size: 0.95rem;
        }
        .detected-platform.hidden { display: none; }
        .detected-platform-icon {
            font-size: 1.5rem;
        }
        .detected-platform-text {
            color: var(--text-main);
        }
        .detected-platform-text strong {
            color: var(--accent);
        }

        .platforms-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 20px;
        }

        .platform-card {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 16px;
            padding: 28px;
            transition: all 0.3s ease;
            position: relative;
            overflow: hidden;
        }
        .platform-card::before {
            content: '';
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            height: 3px;
            opacity: 0;
            transition: opacity 0.3s ease;
        }
        .platform-card:hover {
            background: var(--card-bg-hover);
            border-color: var(--card-border-hover);
            transform: translateY(-2px);
        }
        .platform-card:hover::before {
            opacity: 1;
        }
        .platform-card.detected {
            border-color: var(--primary);
            box-shadow: 0 0 30px rgba(139, 92, 246, 0.15);
        }
        .platform-card.detected::before {
            opacity: 1;
            background: var(--primary);
        }

        .platform-card[data-platform="windows"]::before { background: var(--windows); }
        .platform-card[data-platform="macos"]::before { background: var(--macos); }
        .platform-card[data-platform="linux"]::before { background: var(--linux); }
        .platform-card[data-platform="android"]::before { background: var(--android); }
        .platform-card[data-platform="ios"]::before { background: var(--ios); }
        .platform-card[data-platform="geode"]::before { background: var(--geode); }

        .platform-header {
            display: flex;
            align-items: center;
            gap: 14px;
            margin-bottom: 16px;
        }
        .platform-icon {
            width: 48px;
            height: 48px;
            border-radius: 12px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 1.5rem;
        }
        .platform-card[data-platform="windows"] .platform-icon { background: rgba(0, 120, 212, 0.15); }
        .platform-card[data-platform="macos"] .platform-icon { background: rgba(255, 255, 255, 0.1); }
        .platform-card[data-platform="linux"] .platform-icon { background: rgba(233, 84, 32, 0.15); }
        .platform-card[data-platform="android"] .platform-icon { background: rgba(61, 220, 132, 0.15); }
        .platform-card[data-platform="ios"] .platform-icon { background: rgba(255, 255, 255, 0.1); }
        .platform-card[data-platform="geode"] .platform-icon { background: rgba(139, 92, 246, 0.15); }

        .platform-info h3 {
            font-size: 1.2rem;
            font-weight: 700;
            margin-bottom: 4px;
        }
        .platform-info span {
            font-size: 0.85rem;
            color: var(--text-dim);
        }

        .platform-card.detected .platform-info h3 {
            color: var(--accent);
        }

        .download-options {
            display: flex;
            flex-direction: column;
            gap: 10px;
        }

        .download-btn {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 10px;
            padding: 14px 20px;
            border-radius: 10px;
            text-decoration: none;
            font-weight: 600;
            font-size: 0.95rem;
            transition: all 0.2s ease;
        }
        .download-btn.primary {
            background: var(--primary);
            color: white;
        }
        .download-btn.primary:hover {
            background: var(--primary-hover);
            transform: scale(1.02);
        }
        .download-btn.secondary {
            background: rgba(255, 255, 255, 0.05);
            color: var(--text-main);
            border: 1px solid var(--card-border);
        }
        .download-btn.secondary:hover {
            background: rgba(255, 255, 255, 0.1);
            border-color: var(--card-border-hover);
        }

        .platform-card.detected .download-btn.primary {
            animation: pulse 2s ease-in-out infinite;
        }

        @keyframes pulse {
            0%, 100% { box-shadow: 0 0 0 0 rgba(139, 92, 246, 0.4); }
            50% { box-shadow: 0 0 0 8px rgba(139, 92, 246, 0); }
        }

        .version-info {
            margin-top: 16px;
            padding-top: 16px;
            border-top: 1px solid var(--card-border);
            font-size: 0.8rem;
            color: var(--text-dim);
            display: flex;
            justify-content: space-between;
        }

        .all-platforms-note {
            text-align: center;
            margin-top: 32px;
            padding: 20px;
            background: rgba(255, 255, 255, 0.02);
            border-radius: 12px;
            color: var(--text-muted);
            font-size: 0.9rem;
        }

        .footer {
            margin-top: auto;
            padding: 32px;
            text-align: center;
            color: var(--text-dim);
            font-size: 0.85rem;
            border-top: 1px solid var(--card-border);
        }
        .footer a {
            color: var(--text-muted);
            text-decoration: none;
            transition: color 0.2s;
        }
        .footer a:hover {
            color: var(--text-main);
        }
    </style>
</head>
<body>
    <nav class="nav">
        <a href="https://flozwer.org/paimbnails" class="nav-brand">
            <div class="nav-logo"></div>
            <span>Paimbnails</span>
        </a>
        <div class="nav-links">
            <a href="https://flozwer.org/paimbnails" class="nav-link">Home</a>
            <a href="https://flozwer.org/paimbnails/guidelines" class="nav-link">Guidelines</a>
            <a href="https://flozwer.org/paimbnails/donate" class="nav-link">Donate</a>
            <a href="https://api.flozwer.org/download?manual=1" class="nav-link nav-cta">Download</a>
        </div>
    </nav>

    <div class="container">
        <header class="page-header">
            <h1 class="page-title">Download Paimbnails</h1>
            <p class="page-subtitle">Choose your platform to get started</p>
        </header>

        <div id="detected-platform" class="detected-platform hidden">
            <span class="detected-platform-icon" id="detected-icon"></span>
            <span class="detected-platform-text">We detected your device: <strong id="detected-name"></strong></span>
        </div>

        <div class="platforms-grid">
            <div class="platform-card" data-platform="windows">
                <div class="platform-header">
                    <div class="platform-icon">\u{1FA9F}</div>
                    <div class="platform-info">
                        <h3>Windows</h3>
                        <span>Windows 10 / 11</span>
                    </div>
                </div>
                <div class="download-options">
                    <a href="${WINDOWS_DOWNLOAD_URL}" target="_blank" class="download-btn primary">
                        \u{1F4E6} Download Installer (.exe)
                    </a>
                    <a href="${GEODE_DOWNLOAD_URL}" target="_blank" class="download-btn secondary">
                        .geode only
                    </a>
                </div>
                <div class="version-info">
                    <span>${MOD_VERSION_LABEL}</span>
                    <span>Geode 5.6.1</span>
                </div>
            </div>

            <div class="platform-card" data-platform="macos">
                <div class="platform-header">
                    <div class="platform-icon">\u{1F34E}</div>
                    <div class="platform-info">
                        <h3>macOS</h3>
                        <span>macOS 11+ (Apple Silicon & Intel)</span>
                    </div>
                </div>
                <div class="download-options">
                    <a href="${MACOS_DOWNLOAD_URL}" target="_blank" class="download-btn primary">
                        \u2B07\uFE0F Download macOS (.zip)
                    </a>
                    <a href="${GEODE_DOWNLOAD_URL}" target="_blank" class="download-btn secondary">
                        .geode only
                    </a>
                </div>
                <div class="version-info">
                    <span>${MOD_VERSION_LABEL}</span>
                    <span>Geode 5.6.1</span>
                </div>
            </div>

            <div class="platform-card" data-platform="linux">
                <div class="platform-header">
                    <div class="platform-icon">\u{1F427}</div>
                    <div class="platform-info">
                        <h3>Linux</h3>
                        <span>Ubuntu / Debian / Fedora / Arch</span>
                    </div>
                </div>
                <div class="download-options">
                    <a href="${LINUX_DOWNLOAD_URL}" target="_blank" class="download-btn primary">
                        \u2B07\uFE0F Download Linux (.tar.gz)
                    </a>
                    <a href="${GEODE_DOWNLOAD_URL}" target="_blank" class="download-btn secondary">
                        .geode only
                    </a>
                </div>
                <div class="version-info">
                    <span>${MOD_VERSION_LABEL}</span>
                    <span>Geode 5.6.1</span>
                </div>
            </div>

            <div class="platform-card" data-platform="android">
                <div class="platform-header">
                    <div class="platform-icon">\u{1F916}</div>
                    <div class="platform-info">
                        <h3>Android</h3>
                        <span>Android 8.0+</span>
                    </div>
                </div>
                <div class="download-options">
                    <a href="${GEODE_DOWNLOAD_URL}" target="_blank" class="download-btn primary">
                        \u2B07\uFE0F Download .geode
                    </a>
                    <a href="${RELEASE_URL}" target="_blank" class="download-btn secondary">
                        \u{1F4CB} View release ${MOD_VERSION_LABEL}
                    </a>
                </div>
                <div class="version-info">
                    <span>${MOD_VERSION_LABEL}</span>
                    <span>armeabi-v7a / arm64-v8a</span>
                </div>
            </div>

            <div class="platform-card" data-platform="ios">
                <div class="platform-header">
                    <div class="platform-icon">\u{1F4F1}</div>
                    <div class="platform-info">
                        <h3>iOS / iPadOS</h3>
                        <span>iOS 14+</span>
                    </div>
                </div>
                <div class="download-options">
                    <a href="${GEODE_DOWNLOAD_URL}" target="_blank" class="download-btn primary">
                        \u2B07\uFE0F Download .geode
                    </a>
                    <a href="${RELEASE_URL}" target="_blank" class="download-btn secondary">
                        \u{1F4CB} View release ${MOD_VERSION_LABEL}
                    </a>
                </div>
                <div class="version-info">
                    <span>${MOD_VERSION_LABEL}</span>
                    <span>Requires AltStore</span>
                </div>
            </div>

            <div class="platform-card" data-platform="geode">
                <div class="platform-header">
                    <div class="platform-icon">\u{1F9E9}</div>
                    <div class="platform-info">
                        <h3>Only the .geode file</h3>
                        <span>For users who just want the mod file</span>
                    </div>
                </div>
                <div class="download-options">
                    <a href="${GEODE_DOWNLOAD_URL}" target="_blank" class="download-btn primary">
                        \u2B07\uFE0F Download .geode only
                    </a>
                    <a href="${RELEASE_URL}" target="_blank" class="download-btn secondary">
                        \u{1F4CB} View release ${MOD_VERSION_LABEL}
                    </a>
                </div>
                <div class="version-info">
                    <span>${MOD_VERSION_LABEL}</span>
                    <span>Windows / macOS / Android / iOS</span>
                </div>
            </div>
        </div>

        <div class="all-platforms-note">
            \u{1F4A1} Need help? Check our <a href="https://flozwer.org/paimbnails/guidelines">guidelines</a> or join our <a href="https://discord.gg/5N5vpSfZwY" target="_blank">Discord</a>.
        </div>
    </div>

    <footer class="footer">
        <p>\xA9 2024 Paimbnails. All rights reserved. | <a href="https://github.com/FlozWerDev/Paimbnails">Source</a> | <a href="https://discord.gg/5N5vpSfZwY">Discord</a></p>
    </footer>

    <script>
        (function() {
            const platformData = {
                windows: { name: 'Windows', icon: '\u{1FA9F}', card: 'windows' },
                macos: { name: 'macOS', icon: '\u{1F34E}', card: 'macos' },
                linux: { name: 'Linux', icon: '\u{1F427}', card: 'linux' },
                android: { name: 'Android', icon: '\u{1F916}', card: 'android' },
                ios: { name: 'iOS / iPadOS', icon: '\u{1F4F1}', card: 'ios' }
            };

            function detectPlatform() {
                const ua = navigator.userAgent;

                if (/Windows/i.test(ua)) return 'windows';
                if (/(iPhone|iPad|iPod)/i.test(ua)) return 'ios';
                if (/Android/i.test(ua)) return 'android';
                if (/(Macintosh|Mac OS X)/i.test(ua)) return 'macos';
                if (/Linux/i.test(ua) && !/Android/i.test(ua)) return 'linux';

                return null;
            }

            const query = new URLSearchParams(window.location.search);
            const platform = detectPlatform();
            const autoDownloadMap = {
                windows: '${WINDOWS_DOWNLOAD_URL}',
                macos: '${MACOS_DOWNLOAD_URL}',
                linux: '${LINUX_DOWNLOAD_URL}',
                android: '${GEODE_DOWNLOAD_URL}',
                ios: '${GEODE_DOWNLOAD_URL}'
            };

            if (platform && !query.has('manual') && !query.has('noauto')) {
                const target = autoDownloadMap[platform];
                if (target) {
                    window.location.replace(target);
                    return;
                }
            }
            
            if (platform && platformData[platform]) {
                const data = platformData[platform];
                
                document.getElementById('detected-platform').classList.remove('hidden');
                document.getElementById('detected-icon').textContent = data.icon;
                document.getElementById('detected-name').textContent = data.name;
                
                const card = document.querySelector('[data-platform="' + data.card + '"]');
                if (card) {
                    card.classList.add('detected');
                    card.scrollIntoView({ behavior: 'smooth', block: 'center' });
                }
            }

            document.querySelectorAll('.download-btn').forEach(btn => {
                btn.addEventListener('click', function(e) {
                    const platform = this.dataset.platform;
                    const type = this.dataset.type;
                    console.log('Download requested:', platform, type);
                });
            });
        })();
    <\/script>
</body>
</html>`;
function detectDownloadPlatform(userAgent = "") {
  if (/Windows/i.test(userAgent)) return "windows";
  if (/(iPhone|iPad|iPod)/i.test(userAgent)) return "ios";
  if (/Android/i.test(userAgent)) return "android";
  if (/(Macintosh|Mac OS X)/i.test(userAgent)) return "macos";
  if (/Linux/i.test(userAgent) && !/Android/i.test(userAgent)) return "linux";
  return null;
}
__name(detectDownloadPlatform, "detectDownloadPlatform");
function getPlatformDownloadUrl(platform) {
  switch (platform) {
    case "windows":
      return WINDOWS_DOWNLOAD_URL;
    case "macos":
      return MACOS_DOWNLOAD_URL;
    case "linux":
      return LINUX_DOWNLOAD_URL;
    case "android":
    case "ios":
      return GEODE_DOWNLOAD_URL;
    default:
      return null;
  }
}
__name(getPlatformDownloadUrl, "getPlatformDownloadUrl");

// src/router.js
async function routeRequest(request, env, ctx, preVerifiedApiKey = false) {
  await initManifestEpoch(env.SYSTEM_BUCKET);
  const url2 = new URL(request.url);
  const path = url2.pathname.startsWith("/paimbnails") ? url2.pathname.slice("/paimbnails".length) || "/" : url2.pathname;
  const method = request.method;
  let _cfCacheReq = null;
  let _cfCacheTtl = 0;
  if (method === "GET") {
    const cc = request.headers.get("Cache-Control") || "";
    const skipCache = cc.includes("no-cache") || cc.includes("no-store");
    const isSelfRequest = url2.searchParams.get("self") === "1";
    const isPendingRequest = url2.searchParams.get("pending") === "1";
    // Don't read cache for routes that require authentication unless API key
    // is verified for THIS request — otherwise an authenticated client warms
    // the edge cache and unauthenticated clients get the same response.
    const needsAuth = requiresApiKey(path);
    const authedForCache = needsAuth ? (preVerifiedApiKey === true || await verifyApiKey(request, env)) : true;
    // Routes that should never be edge-cached because they expose privileged data
    const NO_CACHE_PREFIXES = [
      "/api/admin/",
      "/api/queue/",
      "/api/moderator/check",
      "/api/discord/",
      "/api/whitelist",
      "/api/feedback/list",
      "/api/history/",
      "/api/debug/"
    ];
    const isPrivileged = NO_CACHE_PREFIXES.some((p) => path.startsWith(p) || path === p);
    if (!skipCache && !isSelfRequest && !isPendingRequest && authedForCache && !isPrivileged) {
      const ttl = getTtlForPath(path);
      if (ttl !== void 0) {
        if (path === "/api/manifest") {
          const manifestUrl = new URL(request.url);
          manifestUrl.searchParams.set("_epoch", String(getManifestEpoch()));
          for (const p of ["_ts", "t", "self", "pending"])
            manifestUrl.searchParams.delete(p);
          manifestUrl.searchParams.sort();
          _cfCacheReq = new Request(manifestUrl.toString(), {
            method: "GET"
          });
        } else {
          _cfCacheReq = cfCacheKey(request);
        }
        _cfCacheTtl = ttl;
        const cached = await cfCacheMatch(_cfCacheReq);
        if (cached) {
          const resp = new Response(cached.body, cached);
          resp.headers.set("X-Cache", "HIT");
          resp.headers.delete("Vary");
          return resp;
        }
      }
    }
  }
  const response = await _dispatchRoute(request, env, ctx, url2, path, method, preVerifiedApiKey);
  if (_cfCacheReq && response.status >= 200 && response.status < 400) {
    const cacheTags = deriveCacheTags(path);
    const cacheable = makeCacheable(response.clone(), _cfCacheTtl, {
      cacheTag: cacheTags
    });
    cacheable.headers.set("X-Cache", "HIT");
    response.headers.set(
      "Cache-Control",
      cacheable.headers.get("Cache-Control")
    );
    response.headers.delete("Vary");
    response.headers.delete("Pragma");
    response.headers.delete("Expires");
    if (cacheable.headers.has("Cache-Tag")) {
      response.headers.set("Cache-Tag", cacheable.headers.get("Cache-Tag"));
    }
    response.headers.set("X-Cache", "HIT");
    await cfCachePut(_cfCacheReq, cacheable);
  }
  return response;
}
__name(routeRequest, "routeRequest");
// Note: prefixes for routes that were unrouted in the dispatcher are kept
// here when they are ALSO real routes (so requiresApiKey still gates the
// 404). Pure removals (admin-only convenience routes that no longer exist)
// are dropped to keep the auth check fast.
var API_KEY_REQUIRED_PREFIXES = [
  "/api/thumbnails/",
  "/api/manifest",
  "/api/exists",
  "/api/init",
  "/api/discovery",
  "/api/profile/bundle/",
  "/api/profile/batch-bundle",
  "/api/profile/stats/",
  "/api/profile/bgkind/",
  "/api/profiles/config/",
  "/api/profile/badge",
  "/api/v2/ratings/",
  "/api/profile-ratings/",
  "/api/moderator/check",
  "/api/pet-shop/",
  "/api/report/",
  "/api/feedback/submit",
  "/api/queue/",
  "/api/whitelist",
  "/api/debug/",
  "/api/admin/",
  "/profilebackground/batch-check",
  "/mod/upload",
  "/api/profiles/upload",
  "/api/profileimgs/upload",
  "/api/suggestions/upload",
  "/api/updates/upload",
  "/api/backgrounds/upload",
  "/api/profile-music/",
  "/api/profile-music/upload",
  "/api/profile-music/delete",
  "/pending_profilebackground/",
  "/pending_backgrounds/"
];
function requiresApiKey(path) {
  return API_KEY_REQUIRED_PREFIXES.some((p) => path.startsWith(p) || path === p);
}
__name(requiresApiKey, "requiresApiKey");
async function _dispatchRoute(request, env, ctx, url2, path, method, preVerifiedApiKey = false) {
  if (method !== "OPTIONS" && requiresApiKey(path)) {
    const isAuthed = preVerifiedApiKey === true || await verifyApiKey(request, env);
    if (!isAuthed) {
      return new Response(
        JSON.stringify({ error: "Unauthorized", code: "AUTH_FAILED" }),
        {
          status: 401,
          headers: { "Content-Type": "application/json", ...corsNoStore(null, env) }
        }
      );
    }
  }
  if (path.startsWith("/assets/")) {
    const key = path.substring(1);
    const object = await env.SYSTEM_BUCKET.get(key, { skipMeta: true });
    if (!object) return new Response("Not found", { status: 404 });
    return new Response(object.body, {
      headers: {
        "Content-Type": object.httpMetadata?.contentType || "application/octet-stream",
        ...corsHeaders(null, env)
      }
    });
  }
  if (path === "/mod/upload" && method === "POST")
    return handleUpload(request, env, ctx);
  if (path === "/mod/upload-gif" && method === "POST")
    return handleUploadGIF(request, env, ctx);
  if (path === "/mod/upload-video" && method === "POST")
    return handleUploadVideo(request, env, ctx);
  if (path === "/api/thumbnails/list" && method === "GET")
    return handleListThumbnails(request, env);
  if (path === "/api/thumbnails/list-batch" && method === "POST")
    return handleListThumbnailsBatch(request, env);
  if (path === "/api/thumbnails/info" && method === "GET")
    return handleGetThumbnailInfo(request, env);
  if (path === "/api/manifest" && method === "GET")
    return handleManifest(request, env);
  if (path === "/api/thumbnails/batch" && method === "POST")
    return handleBatchThumbnails(request, env);
  if (path === "/api/profilebackground/batch" && method === "POST")
    return handleBatchProfileBackgrounds(request, env);
  if (path === "/api/profileimgs/batch" && method === "POST")
    return handleBatchProfileImgs(request, env);
  if (path === "/api/profiles/upload" && method === "POST")
    return handleUpload(request, env, ctx);
  if (path === "/api/profileimgs/upload" && method === "POST")
    return handleUpload(request, env, ctx);
  if (path.startsWith("/api/download/"))
    return handleDownload(request, env, ctx);
  // Anonymous (no X-API-Key) GETs to profile/background assets:
  //   • Direct mode ON  → 302 to CDN Pull Zone (paid bandwidth, 0 Worker reqs).
  //   • Direct mode OFF → fall through to the serve* handlers below, which
  //     proxy from Bunny Storage with the AccessKey ("bunny por contraseña").
  // The previous fallback redirect to BUNNY_STORAGE_URL is removed: that
  // hostname requires the AccessKey, so an unauthenticated client following
  // the 302 would always get 401.
  if (
    method === "GET" &&
    !request.headers.get("X-API-Key") &&
    isDirectModeActive() &&
    env.CDN_PULL_ZONE_URL
  ) {
    const cdnPullRedirectPaths = [
      "/backgrounds/",
      "/profilebackground/",
      "/profiles/",
      "/profileimgs/",
      "/profile-music/"
    ];
    if (cdnPullRedirectPaths.some((p) => path.startsWith(p))) {
      const redirectUrl = `${env.CDN_PULL_ZONE_URL}${path}`;
      return new Response(null, {
        status: 302,
        headers: {
          "Location": redirectUrl,
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Expose-Headers": "Location",
          "Cache-Control": "public, max-age=3600",
          "X-Quota-Direct": "1"
        }
      });
    }
  }
  if (path.startsWith("/t/")) return handleDirectThumbnail(request, env, ctx);
  if (path === "/api/exists" && method === "GET")
    return handleExists(request, env, ctx);
  if (path.startsWith("/api/thumbnails/delete/") && method === "POST")
    return handleDeleteThumbnail(request, env, ctx);
  if (path.startsWith("/api/thumbnails/reorder/") && method === "POST")
    return handleReorderThumbnails(request, env, ctx);
  // /api/search — not used by the C++ client. Route removed.
  if (path.startsWith("/api/profile/bundle/") && method === "GET")
    return handleProfileBundle(request, env);
  if (path === "/api/profile/batch-bundle" && method === "POST")
    return handleBatchProfileBundle(request, env);
  if (path.startsWith("/api/profile/stats/") && method === "GET")
    return handleGetProfileStats(request, env);
  if (path.startsWith("/api/profile/bgkind/") && method === "GET")
    return handleGetProfileBgKind(request, env);
  if (path === "/api/profiles/config/upload" && method === "POST")
    return handleUploadProfileConfig(request, env);
  if (path.startsWith("/api/profiles/config/") && method === "GET")
    return handleGetProfileConfig(request, env);
  if (path === "/profilebackground/batch-check" && method === "POST")
    return handleBatchCheckProfiles(request, env);
  if (path === "/api/backgrounds/upload" && method === "POST")
    return handleUploadBackground(request, env, ctx);
  if (path === "/api/backgrounds/upload-gif" && method === "POST")
    return handleUploadBackgroundGIF(request, env, ctx);
  if (path === "/api/backgrounds/upload-video" && method === "POST")
    return handleUploadBackgroundVideo(request, env, ctx);
  if (path.startsWith("/backgrounds/") && method === "GET")
    return handleServeBackground(request, env);
  if (path.startsWith("/profilebackground/") && method === "GET")
    return handleServeBackground(request, env);
  if (path.startsWith("/profiles/")) return handleServeProfile(request, env);
  if (path.startsWith("/profileimgs/"))
    return handleServeProfileImg(request, env);
  if ((path.startsWith("/pending_profilebackground/") || path.startsWith("/pending_backgrounds/") || path.startsWith("/pending_thumbnails/")) && method === "GET") {
    if (!await verifyApiKey(request, env))
      return new Response("Unauthorized", {
        status: 401,
        headers: corsNoStore(null, env)
      });
    const pendingFilename = (() => {
      try { return decodeURIComponent(path.slice(1)); }
      catch { return null; }
    })();
    if (!pendingFilename || pendingFilename.includes("..") || pendingFilename.includes("\\") || pendingFilename.includes("//")) {
      return new Response("Invalid path", {
        status: 400,
        headers: corsNoStore(null, env)
      });
    }
    let expectedPrefix;
    if (path.startsWith("/pending_profilebackground/")) {
      expectedPrefix = "pending_profilebackground/";
    } else if (path.startsWith("/pending_backgrounds/")) {
      expectedPrefix = "pending_backgrounds/";
    } else {
      expectedPrefix = "pending_thumbnails/";
    }
    if (!pendingFilename.startsWith(expectedPrefix)) {
      return new Response("Invalid path", {
        status: 400,
        headers: corsNoStore(null, env)
      });
    }
    const obj = await env.THUMBNAILS_BUCKET.get(pendingFilename, {
      skipMeta: true
    });
    if (!obj)
      return new Response("Not found", {
        status: 404,
        headers: corsNoStore(null, env)
      });
    const headers = new Headers();
    obj.writeHttpMetadata(headers);
    headers.set("Access-Control-Allow-Origin", "*");
    headers.set("Cache-Control", "no-store, no-cache, must-revalidate");
    return new Response(obj.body, { headers });
  }
  if (path === "/api/profile/badge" && method === "POST")
    return handleSetCustomBadge(request, env);
  if (path === "/api/profile/badge/delete" && method === "POST")
    return handleDeleteCustomBadge(request, env);
  if (path === "/api/profile/badge/batch" && method === "GET")
    return handleGetCustomBadgeBatch(request, env);
  if (path.startsWith("/api/profile/badge/") && method === "GET")
    return handleGetCustomBadge(request, env);
  if (path === "/api/whitelist" && method === "GET")
    return handleGetWhitelist(request, env);
  if (path === "/api/whitelist/add" && method === "POST")
    return handleAddWhitelist(request, env, ctx);
  if (path === "/api/whitelist/remove" && method === "POST")
    return handleRemoveWhitelist(request, env, ctx);
  if (path === "/api/profile-music/upload" && method === "POST")
    return handleUploadProfileMusic(request, env, ctx);
  if (path === "/api/profile-music/delete" && method === "POST")
    return handleDeleteProfileMusic(request, env);
  if (path.match(/^\/api\/profile-music\/\d+\/audio$/) && method === "GET")
    return handleGetProfileMusicAudio(request, env);
  if (path.startsWith("/api/profile-music/") && method === "GET")
    return handleGetProfileMusic(request, env);
  if (path.startsWith("/profile-music/") && path.endsWith(".mp3") && method === "GET")
    return handleServeProfileMusic(request, env);
  if (path === "/api/suggestions/upload" && method === "POST")
    return handleUploadSuggestion(request, env, ctx);
  if (path === "/api/updates/upload" && method === "POST")
    return handleUploadUpdate(request, env, ctx);
  if (path.startsWith("/suggestions/"))
    return handleDownloadSuggestion(request, env);
  if (path.startsWith("/updates/")) return handleDownloadUpdate(request, env);
  if (path === "/api/mod/version" && method === "GET")
    return handleVersionCheck(request);
  if (path === "/downloads/paimon.level_thumbnails.geode" && method === "GET")
    return handleModDownload(request, env);
  if (path === "/api/daily/set" && method === "POST")
    return handleSetDailyLevel(request, env);
  if (path === "/api/daily/current" && method === "GET")
    return handleGetDailyLevel(request, env);
  if (path === "/api/weekly/set" && method === "POST")
    return handleSetWeeklyLevel(request, env);
  if (path === "/api/weekly/current" && method === "GET")
    return handleGetWeeklyLevel(request, env);
  // /api/leaderboard removed — not used by the C++ client (data is bundled
  // inside /api/discovery). Keep handleGetLeaderboard around in case the
  // admin web UI still calls it, but the dispatcher no longer routes here.
  if (path === "/api/featured/history" && method === "GET")
    return handleGetDailyWeeklyHistory(request, env);
  if (path === "/api/top-creators" && method === "GET")
    return handleGetTopCreators(request, env);
  if (path === "/api/top-thumbnails" && method === "GET")
    return handleGetTopThumbnails(request, env);
  if (path === "/api/profile-ratings/vote" && method === "POST")
    return handleProfileRatingVote(request, env);
  if (path.startsWith("/api/profile-ratings/") && method === "GET")
    return handleGetProfileRating(request, env);
  if (path === "/api/v2/ratings/vote" && method === "POST")
    return handleVoteV2(request, env);
  if (path.startsWith("/api/v2/ratings/") && method === "GET")
    return handleGetRatingV2(request, env);
  // /api/queue/batch-accept and /api/queue/batch-reject removed —
  // the client only uses the per-id endpoints.
  if (path === "/api/queue/summary" && method === "GET")
    return handleQueueSummary(request, env);
  if (path.startsWith("/api/queue/accept/") && method === "POST")
    return handleAcceptQueue(request, env, ctx);
  if (path.startsWith("/api/queue/claim/") && method === "POST")
    return handleClaimQueue(request, env);
  if (path.startsWith("/api/queue/reject/") && method === "POST")
    return handleRejectQueue(request, env);
  if (path.startsWith("/api/queue/") && method === "GET")
    return handleGetQueue(request, env);
  if (path === "/api/report/submit" && method === "POST")
    return handleSubmitReport(request, env);
  if (path === "/api/report/user" && method === "POST")
    return handleSubmitUserReport(request, env);
  if (path === "/api/feedback/submit" && method === "POST")
    return handleFeedbackSubmit(request, env);
  // /api/feedback/list removed — only the admin web page consumes this and
  // it can be re-enabled by restoring the route if needed.
  // /api/history/{uploads,accepted,rejected} removed — never called from
  // the client. handleGetHistory function is preserved for future use.
  if (path === "/api/moderator/check" && method === "GET")
    return handleModeratorCheck(request, env);
  if (path === "/api/init" && method === "POST")
    return handleInit(request, env);
  if (path === "/api/v2/ratings/batch" && method === "POST")
    return handleBatchRatings(request, env);
  if (path === "/api/discovery" && method === "GET")
    return handleDiscovery(request, env);
  if (path === "/api/admin/set-daily" && method === "POST")
    return handleSetDaily(request, env);
  if (path === "/api/admin/banlist" && method === "GET")
    return handleGetBanList(request, env);
  if (path === "/api/banned" && method === "GET")
    return handleCheckBanned(request, env);
  if (path === "/api/admin/ban" && method === "POST")
    return handleBanUser(request, env);
  if (path === "/api/admin/unban" && method === "POST")
    return handleUnbanUser(request, env);
  if (path === "/api/admin/add-moderator" && method === "POST")
    return handleAddModerator(request, env);
  if (path === "/api/admin/remove-moderator" && method === "POST")
    return handleRemoveModerator(request, env);
  // Generic role management (mod/vip/helper/idea).
  if (path === "/api/admin/add-role" && method === "POST")
    return handleAddRole(request, env);
  if (path === "/api/admin/remove-role" && method === "POST")
    return handleRemoveRole(request, env);
  if (path === "/api/admin/role-members" && method === "GET")
    return handleRoleMembers(request, env);
  // /api/admin/add-vip /api/admin/remove-vip removed — handlers preserved.
  if (path === "/api/admin/moderators" && method === "GET")
    return handleListModerators(request, env);
  // /api/admin/backfill-contributors and /api/admin/migrate-* are one-shot
  // scripts; routes removed to avoid accidental triggering. Re-enable by
  // restoring the route lines if you need to run them.
  if (path === "/api/admin/audit" && method === "GET")
    return handleAuditLogs(request, env);
  if (path === "/api/admin/quota" && method === "GET") {
    if (!await verifyApiKey(request, env))
      return new Response("Unauthorized", { status: 401 });
    return new Response(JSON.stringify(getQuotaStats(env)), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore(null, env) }
    });
  }
  if (path === "/api/admin/quota/direct" && method === "POST") {
    if (!await verifyApiKey(request, env))
      return new Response("Unauthorized", { status: 401 });
    if (!await verifyAdminFromRequest(request, env))
      return new Response("Admin required", { status: 403 });
    const body = await request.json().catch(() => ({}));
    const enabled = Boolean(body.enabled);
    setDirectMode(enabled);
    return new Response(JSON.stringify({ directMode: enabled, message: `Direct mode ${enabled ? "enabled" : "disabled"} for this isolate` }), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore(null, env) }
    });
  }
  // /api/discord/link* — Discord linking is handled by the standalone bot
  // service. Routes removed (handlers preserved).
  if (path === "/api/moderators" && method === "GET") {
    const MEM_KEY2 = "moderators_detailed_list";
    let detailedMods = memCache.get(MEM_KEY2);
    if (!detailedMods) {
      const moderators = await getModerators(env.SYSTEM_BUCKET);
      let allMods = [.../* @__PURE__ */ new Set([...ADMIN_USERS, ...moderators])];
      const hiddenUsers = ["viprin", "robtop"];
      allMods = allMods.filter(
        (user) => !hiddenUsers.includes(user.toLowerCase())
      );
      detailedMods = await Promise.all(
        allMods.map(async (username) => {
          const usernameLower = username.toLowerCase();
          let accountID = 0;
          try {
            const authData = await getR2Json(
              env.SYSTEM_BUCKET,
              `data/auth/${usernameLower}.json`
            );
            accountID = parseInt(authData?.accountID || "0", 10) || 0;
          } catch (_) {
            accountID = 0;
          }
          return {
            username,
            role: ADMIN_USERS.includes(usernameLower) ? "admin" : "mod",
            accountID
          };
        })
      );
      memCache.set(MEM_KEY2, detailedMods, 5 * 6e4);
    }
    return new Response(JSON.stringify({ moderators: detailedMods }), {
      status: 200,
      headers: {
        "Content-Type": "application/json",
        ...corsHeaders(null, env)
      }
    });
  }
  // /api/bot/config — bot lives on a separate service (PaimbnailsBot-main),
  // it shouldn't talk to the Worker for config. Routes removed.
  // /api/latest-uploads, /api/gallery/list — included inside /api/discovery,
  // not used directly by the C++ client. Routes removed (handlers preserved).
  if (path === "/api/pet-shop/list" && method === "GET")
    return handlePetShopList(request, env);
  if (path.startsWith("/api/pet-shop/download/") && method === "GET")
    return handlePetShopDownload(request, env);
  if (path === "/api/pet-shop/upload" && method === "POST")
    return handlePetShopUpload(request, env);
  // /api/level/ /api/gd/profile/ — GD Browser proxies. The C++ client talks
  // directly to GD's servers, the bot can hit gdbrowser.com on its own.
  // Routes removed to free up Worker requests.
  if (path === "/api/debug/bunny-test" && method === "GET") {
    if (!await verifyApiKey(request, env))
      return new Response("Unauthorized", { status: 401 });
    if (!await verifyAdminFromRequest(request, env))
      return new Response("Admin required", { status: 403 });
    const testUrl = `${env.BUNNY_ENDPOINT || "https://storage.bunnycdn.com"}/${env.BUNNY_ZONE_NAME || "paimbnails"}/`;
    const secretKey = env.BUNNY_SECRET_KEY;
    const diag = {
      secretDefined: !!secretKey,
      secretLength: secretKey?.length || 0,
      zoneName: env.BUNNY_ZONE_NAME,
      endpoint: env.BUNNY_ENDPOINT,
      testUrl
    };
    try {
      const listRes = await fetch(testUrl, {
        method: "GET",
        headers: { AccessKey: secretKey }
      });
      diag.listStatus = listRes.status;
      diag.listStatusText = listRes.statusText;
      const listBody = await listRes.text();
      diag.listBodyPreview = listBody.substring(0, 500);
      const headUrl = `${testUrl}thumbnails/__bunny_test_404__.webp`;
      const headRes = await fetch(headUrl, {
        method: "HEAD",
        headers: { AccessKey: secretKey }
      });
      diag.headStatus = headRes.status;
      diag.headStatusText = headRes.statusText;
    } catch (e) {
      diag.fetchError = e.message;
    }
    return new Response(JSON.stringify(diag, null, 2), {
      status: 200,
      headers: { "Content-Type": "application/json", ...corsNoStore(null, env) }
    });
  }
  if (path === "/api/debug/bunny-raw" && method === "GET") {
    if (!await verifyApiKey(request, env))
      return new Response("Unauthorized", { status: 401 });
    if (!await verifyAdminFromRequest(request, env))
      return new Response("Admin required", { status: 403 });
    const prefix = url2.searchParams.get("prefix") || "";
    const bunnyUrl = `${env.THUMBNAILS_BUCKET.baseUrl}/${prefix}`;
    const res = await fetch(bunnyUrl, {
      method: "GET",
      headers: { AccessKey: env.THUMBNAILS_BUCKET.apiKey }
    });
    return new Response(await res.text(), {
      status: res.status,
      headers: { "Content-Type": "application/json" }
    });
  }
  if (path === "/api/debug/r2-list" && method === "GET") {
    if (!await verifyApiKey(request, env))
      return new Response("Unauthorized", { status: 401 });
    if (!await verifyAdminFromRequest(request, env))
      return new Response("Admin required", { status: 403 });
    try {
      const prefix = url2.searchParams.get("prefix") || "";
      const listed = await env.THUMBNAILS_BUCKET.list({ limit: 100, prefix });
      return new Response(
        JSON.stringify(
          {
            objects: listed.objects.map((obj) => ({
              key: obj.key,
              size: obj.size,
              uploaded: obj.uploaded
            }))
          },
          null,
          2
        ),
        {
          status: 200,
          headers: {
            "Content-Type": "application/json",
            ...corsNoStore(null, env)
          }
        }
      );
    } catch (error) {
      return new Response(JSON.stringify({ error: error.message }), {
        status: 500,
        headers: {
          "Content-Type": "application/json",
          ...corsNoStore(null, env)
        }
      });
    }
  }
  if (path === "/" || path === "/index.html") {
    return new Response(homeHtml, {
      status: 200,
      headers: {
        "Content-Type": "text/html; charset=utf-8",
        ...htmlSecurityHeaders(),
        ...corsHeaders(null, env)
      }
    });
  }
  if (path === "/donate" || path === "/donate.html") {
    return new Response(donateHtml, {
      status: 200,
      headers: {
        "Content-Type": "text/html; charset=utf-8",
        ...htmlSecurityHeaders(),
        ...corsHeaders(null, env)
      }
    });
  }
  if (path === "/download" || path === "/download.html") {
    const url3 = new URL(request.url);
    const wantsManualPage = url3.searchParams.has("manual") || url3.searchParams.has("noauto");
    if (!wantsManualPage) {
      const platform = detectDownloadPlatform(request.headers.get("User-Agent") || "");
      const target = getPlatformDownloadUrl(platform);
      if (target) {
        const location = /^https?:\/\//i.test(target) ? target : `${url3.origin}${target}`;
        return new Response(null, {
          status: 302,
          headers: {
            Location: location,
            "Cache-Control": "no-store",
            ...corsHeaders(null, env)
          }
        });
      }
    }
    return new Response(downloadHtml, {
      status: 200,
      headers: {
        "Content-Type": "text/html; charset=utf-8",
        ...htmlSecurityHeaders(),
        ...corsHeaders(null, env)
      }
    });
  }
  if (path === "/guidelines" || path === "/guidelines.html") {
    return new Response(guidelinesHtml, {
      status: 200,
      headers: {
        "Content-Type": "text/html; charset=utf-8",
        ...htmlSecurityHeaders(),
        ...corsHeaders(null, env)
      }
    });
  }
  if (path === "/feedback-admin" || path === "/feedback-admin.html") {
    const adminHtml = await env.THUMBNAILS_BUCKET.get(
      "public/feedback-admin.html",
      { skipMeta: true }
    );
    if (adminHtml) {
      return new Response(await adminHtml.text(), {
        status: 200,
        headers: {
          "Content-Type": "text/html; charset=utf-8",
          ...htmlSecurityHeaders(),
          ...corsHeaders(null, env)
        }
      });
    }
    return new Response("Admin page not found", { status: 404 });
  }
  if (path === "/download/paimon.level_thumbnails.geode") {
    return new Response(null, {
      status: 302,
      headers: {
        Location: getGitHubReleaseAssetUrl("flozwer.paimbnails2.geode"),
        "Cache-Control": "no-store",
        ...corsHeaders(null, env)
      }
    });
  }
  if (path === "/favicon.png") {
    const faviconUrl = env.CDN_PULL_ZONE_URL ? `${env.CDN_PULL_ZONE_URL}/system/paim_Paimon.png` : null;
    if (faviconUrl) {
      return Response.redirect(faviconUrl, 302);
    }
    return new Response("Not found", { status: 404 });
  }
  if (path === "/health") {
    const HEALTH_CACHE_KEY = "_health_response";
    const cachedHealth = memCache.get(HEALTH_CACHE_KEY);
    if (cachedHealth) {
      return new Response(cachedHealth.body, {
        status: cachedHealth.status,
        headers: { "Content-Type": "application/json", "X-Cache": "HIT", ...corsHeaders(null, env) }
      });
    }
    const storageCheck = /* @__PURE__ */ __name(async (label, bucket) => {
      try {
        const start = Date.now();
        await bucket.head("__healthcheck__");
        return { status: "ok", latencyMs: Date.now() - start };
      } catch {
        return { status: "error", latencyMs: -1 };
      }
    }, "storageCheck");
    const [thumbsHealth, sysHealth] = await Promise.all([
      storageCheck("thumbnails", env.THUMBNAILS_BUCKET),
      storageCheck("system", env.SYSTEM_BUCKET)
    ]);
    const requiredSecrets = ["API_KEY", "BUNNY_SECRET_KEY", "BUNNY_ACCESS_KEY"];
    const missingSecrets = requiredSecrets.filter((k) => !env[k]);
    const hasConfigIssues = missingSecrets.length > 0;
    const storageHealthy = thumbsHealth.status === "ok" && sysHealth.status === "ok";
    const body = JSON.stringify({
      status: hasConfigIssues || !storageHealthy ? "degraded" : "ok",
      timestamp: (/* @__PURE__ */ new Date()).toISOString(),
      version: "2.3.5"
    });
    const httpStatus = hasConfigIssues || !storageHealthy ? 503 : 200;
    memCache.set(HEALTH_CACHE_KEY, { body, status: httpStatus }, 6e4);
    return new Response(body, {
      status: httpStatus,
      headers: {
        "Content-Type": "application/json",
        ...corsHeaders(null, env)
      }
    });
  }
  if (path === "/api/admin/migrate-bunny" && method === "POST") {
    if (!await verifyApiKey(request, env))
      return new Response("Unauthorized", { status: 401 });
    if (!await verifyAdminFromRequest(request, env))
      return new Response("Admin required", { status: 403 });
    const cursor = url2.searchParams.get("cursor");
    const target = url2.searchParams.get("target");
    const BUNNY_ZONE = env.BUNNY_ZONE_NAME || "paimbnails";
    const BUNNY_KEY = env.BUNNY_SECRET_KEY;
    let bucket, bunnyFolder;
    if (target === "thumbnails") {
      bucket = env.THUMBNAILS_BUCKET;
      bunnyFolder = "thumbnails";
    } else if (target === "system") {
      bucket = env.SYSTEM_BUCKET;
      bunnyFolder = "system";
    } else return new Response("Invalid target", { status: 400 });
    const list = await bucket.list({ limit: 10, cursor: cursor || void 0 });
    const results = [];
    for (const obj of list.objects) {
      try {
        const r2Obj = await bucket.get(obj.key);
        if (r2Obj) {
          const cleanKey = obj.key.startsWith("/") ? obj.key.substring(1) : obj.key;
          const bunnyPath = `${bunnyFolder}/${cleanKey}`;
          const bunnyUrl = `https://storage.bunnycdn.com/${BUNNY_ZONE}/${bunnyPath}`;
          const uploadResp = await fetch(bunnyUrl, {
            method: "PUT",
            headers: {
              AccessKey: BUNNY_KEY,
              "Content-Type": r2Obj.httpMetadata?.contentType || "application/octet-stream"
            },
            body: r2Obj.body
          });
          results.push({
            key: obj.key,
            success: uploadResp.ok,
            status: uploadResp.status
          });
        }
      } catch (e) {
        results.push({ key: obj.key, error: e.message });
      }
    }
    return new Response(
      JSON.stringify({
        cursor: list.truncated ? list.cursor : null,
        results,
        done: !list.truncated
      }),
      {
        headers: { "Content-Type": "application/json" }
      }
    );
  }
  return new Response(JSON.stringify({ error: "Not found" }), {
    status: 404,
    headers: { "Content-Type": "application/json", ...corsHeaders(null, env) }
  });
}
__name(_dispatchRoute, "_dispatchRoute");

// src/index.js
var REQUIRED_SECRETS = ["API_KEY", "BUNNY_SECRET_KEY", "BUNNY_ACCESS_KEY"];
var _missingSecretsLogged = /* @__PURE__ */ new Set();
function checkRequiredSecrets(env) {
  const missing = REQUIRED_SECRETS.filter((k) => !env[k]);
  if (missing.length > 0 && !_missingSecretsLogged.has(missing.join(","))) {
    _missingSecretsLogged.add(missing.join(","));
    console.error(`[CONFIG] CRITICAL: Missing required secrets: ${missing.join(", ")}. Run: ${missing.map((k) => `wrangler secret put ${k}`).join("; ")}`);
  }
  return missing;
}
__name(checkRequiredSecrets, "checkRequiredSecrets");
var index_default = {
  async fetch(request, env, ctx) {
    const missingSecrets = checkRequiredSecrets(env);
    const oversized = oversizedRequestGuard(request, env);
    if (oversized) return enforceCorsPolicy(oversized, request, env);
    env.THUMBNAILS_BUCKET = new BunnyBucket(
      env.BUNNY_ACCESS_KEY,
      env.BUNNY_SECRET_KEY,
      env.BUNNY_ENDPOINT || "https://storage.bunnycdn.com",
      env.BUNNY_ZONE_NAME || "paimbnails",
      "thumbnails"
    );
    env.SYSTEM_BUCKET = new BunnyBucket(
      env.BUNNY_ACCESS_KEY,
      env.BUNNY_SECRET_KEY,
      env.BUNNY_ENDPOINT || "https://storage.bunnycdn.com",
      env.BUNNY_ZONE_NAME || "paimbnails",
      "system"
    );
    if (env.WORKER_QUOTA_ENABLED === "true") {
      const url2 = new URL(request.url);
      await trackRequest(env.SYSTEM_BUCKET, env);
      if (request.method === "GET") {
        const direct = buildDirectResponse(url2.pathname, env.CDN_PULL_ZONE_URL);
        if (direct) return enforceCorsPolicy(direct, request, env);
      }
    }
    if (request.method === "OPTIONS") {
      return handleOptions(request, env);
    }
    const [hasApiKey] = await Promise.all([
      verifyApiKey(request, env),
      loadAdminUsers(env.SYSTEM_BUCKET)
    ]);
    const rateTier = hasApiKey ? "authenticated" : "anonymous";
    const rateLimited = rateLimitGuard(request, rateTier);
    if (rateLimited) return enforceCorsPolicy(rateLimited, request, env);
    try {
      return enforceCorsPolicy(await routeRequest(request, env, ctx, hasApiKey), request, env);
    } catch (error) {
      let status = 500;
      let body = { error: "Internal server error", code: "INTERNAL_ERROR" };
      let extraHeaders = {};
      if (error instanceof StorageError) {
        status = 502;
        body = {
          error: "Storage service temporarily unavailable",
          code: "STORAGE_ERROR",
          retryable: true
        };
        extraHeaders["Retry-After"] = "5";
      } else if (error instanceof AppError) {
        status = error.status;
        body = error.toJSON();
        if (error.retryable) extraHeaders["Retry-After"] = "5";
      }
      console.error(`[${status}] ${error.code || "UNKNOWN"}:`, error.message);
      return enforceCorsPolicy(new Response(JSON.stringify(body), {
        status,
        headers: { "Content-Type": "application/json", ...corsNoStore(null, env), ...extraHeaders }
      }), request, env);
    }
  },
  async scheduled(event, env, ctx) {
    // Cron handler — invoked when [triggers].crons is configured in
    // wrangler.toml. Currently NOT enabled. If you re-enable it, this only
    // refreshes the creator leaderboard cache (the previous self-warmup of
    // 13 endpoints was burning Worker quota for marginal cache gains and
    // was incompatible with the slim route table).
    env.THUMBNAILS_BUCKET = new BunnyBucket(
      env.BUNNY_ACCESS_KEY,
      env.BUNNY_SECRET_KEY,
      env.BUNNY_ENDPOINT || "https://storage.bunnycdn.com",
      env.BUNNY_ZONE_NAME || "paimbnails",
      "thumbnails"
    );
    env.SYSTEM_BUCKET = new BunnyBucket(
      env.BUNNY_ACCESS_KEY,
      env.BUNNY_SECRET_KEY,
      env.BUNNY_ENDPOINT || "https://storage.bunnycdn.com",
      env.BUNNY_ZONE_NAME || "paimbnails",
      "system"
    );
    try {
      await rebuildCreatorLeaderboard(env);
      console.log("[Cron] Creator leaderboard rebuilt");
    } catch (e) {
      console.error("[Cron] rebuildCreatorLeaderboard failed:", e?.message);
    }
  }
};
export {
  index_default as default
};
//# sourceMappingURL=index.js.map
