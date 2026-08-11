import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const workerSource = await readFile(new URL("../cloudflare-server/paimon-thumbnails-server.js", import.meta.url), "utf8");
const testableSource = workerSource.replace(
  "  handleModAuthComplete,",
  "  handleModAuthComplete,\n  handleAcceptQueue,\n  handleUpload,\n  handleUploadGIF,\n  handleUploadSuggestion,"
);
const worker = await import(`data:text/javascript;base64,${Buffer.from(testableSource).toString("base64")}`);

const {
  buildModAuthRecord,
  handleAcceptQueue,
  handleUpload,
  handleUploadGIF,
  handleUploadSuggestion,
  issueModAuthChallenge
} = worker;

const png = Uint8Array.from(Buffer.from(
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=",
  "base64"
));
const gif = Uint8Array.from(Buffer.from(
  "R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw==",
  "base64"
));

class MemoryBucket {
  constructor(json = {}) {
    this.json = new Map(Object.entries(json).map(([key, value]) => [key, JSON.stringify(value)]));
    this.files = new Map();
  }

  async get(key) {
    if (this.json.has(key)) {
      const value = this.json.get(key);
      return { text: async () => value };
    }

    const file = this.files.get(key);
    if (!file) return null;
    return {
      arrayBuffer: async () => new Uint8Array(file.bytes).buffer,
      customMetadata: file.options.customMetadata || {},
      httpMetadata: file.options.httpMetadata || {},
      text: async () => new TextDecoder().decode(file.bytes)
    };
  }

  async head(key) {
    return this.files.has(key) ? {} : null;
  }

  async list({ prefix = "" } = {}) {
    return {
      objects: [...this.files.keys()]
        .filter((key) => key.startsWith(prefix))
        .map((key) => ({ key }))
    };
  }

  async put(key, body, options = {}) {
    if (typeof body === "string") {
      this.json.set(key, body);
      return;
    }

    const bytes = body instanceof Uint8Array
      ? new Uint8Array(body)
      : body instanceof ArrayBuffer
        ? new Uint8Array(body)
        : new Uint8Array(await body.arrayBuffer());
    this.files.set(key, { bytes, options });
  }

  async delete(key) {
    this.json.delete(key);
    this.files.delete(key);
  }

  jsonValue(key) {
    const value = this.json.get(key);
    return value === undefined ? undefined : JSON.parse(value);
  }

  fileKeys() {
    return [...this.files.keys()];
  }
}

const env = {
  API_KEY: "test-api-key",
  MAX_UPLOAD_SIZE: "52428800",
  MOD_AUTH_SECRET: "A".repeat(43),
  SYSTEM_BUCKET: new MemoryBucket({
    "data/banlist.json": { banned: [] },
    "data/helpers.json": { helpers: [] },
    "data/ideas.json": { ideas: [] },
    "data/moderators.json": { moderators: ["testmod"] },
    "data/system/admins.json": [],
    "data/system/versions.json": {},
    "data/vips.json": { vips: [] }
  }),
  THUMBNAILS_BUCKET: new MemoryBucket()
};

const originalFetch = globalThis.fetch;
const originalCaches = globalThis.caches;
globalThis.caches = {
  default: {
    async delete() {},
    async match() { return null; },
    async put() {}
  }
};
globalThis.fetch = async (url) => {
  const target = String(url);
  if (target.startsWith("https://gdbrowser.com/api/profile/testmod") ||
      target.startsWith("https://gdbrowser.com/api/profile/player")) {
    return new Response(JSON.stringify({ accountID: target.includes("/player") ? 5151 : 4242 }), {
      headers: { "Content-Type": "application/json" },
      status: 200
    });
  }
  throw new Error(`Unexpected network request in thumbnail upload test: ${url}`);
};

const authChallenge = await issueModAuthChallenge(env, "testmod", 4242);
const { credential, record } = await buildModAuthRecord(env, authChallenge.token);
await env.SYSTEM_BUCKET.put("data/auth/testmod.json", JSON.stringify(record));

async function upload({
  accountID = 4242,
  bytes = png,
  filename = "thumbnail.png",
  levelID,
  levelMeta,
  modCode,
  path = "/thumbnails",
  type = "image/png",
  url = "https://api.flozwer.org/mod/upload",
  username = "testmod"
}) {
  const form = new FormData();
  form.set("image", new Blob([bytes], { type }), filename);
  form.set("levelId", String(levelID));
  form.set("username", username);
  form.set("accountID", String(accountID));
  form.set("path", path);
  if (levelMeta !== undefined) form.set("levelMeta", JSON.stringify(levelMeta));

  const prepared = new Request(url, { body: form, method: "POST" });
  const body = await prepared.arrayBuffer();
  const headers = new Headers(prepared.headers);
  headers.set("Content-Length", String(body.byteLength));
  headers.set("X-API-Key", env.API_KEY);
  if (modCode !== undefined) headers.set("X-Mod-Code", modCode);

  const waiters = [];
  const request = new Request(url, {
    body,
    headers,
    method: "POST"
  });
  const handler = url.endsWith("/mod/upload-gif")
    ? handleUploadGIF
    : url.includes("/api/suggestions/upload")
      ? handleUploadSuggestion
      : handleUpload;
  const response = await handler(request, env, {
    waitUntil(promise) {
      waiters.push(Promise.resolve(promise));
    }
  });
  await Promise.all(waiters);
  return { body: await response.json(), response };
}

try {
  const missingCode = await upload({ levelID: 7001, modCode: undefined });
  assert.equal(missingCode.response.status, 200);
  assert.equal(missingCode.body.moderatorUpload, false);
  assert.equal(missingCode.body.pendingVerification, true);
  assert.equal(missingCode.body.inQueue, true);
  assert.match(missingCode.body.message, /verification/i);
  assert.ok(env.THUMBNAILS_BUCKET.fileKeys().some((key) => key.startsWith("pending_thumbnails/7001_")));
  assert.equal(env.THUMBNAILS_BUCKET.fileKeys().some((key) => key.startsWith("thumbnails/7001_")), false);

  const invalidCode = await upload({ levelID: 7002, modCode: "invalid-mod-code" });
  assert.equal(invalidCode.response.status, 200);
  assert.equal(invalidCode.body.moderatorUpload, false);
  assert.equal(invalidCode.body.pendingVerification, true);
  assert.equal(invalidCode.body.modCodeMismatch, true);

  const directPng = await upload({ levelID: 7003, modCode: credential });
  assert.equal(directPng.response.status, 200);
  assert.equal(directPng.body.moderatorUpload, true);
  assert.equal(directPng.body.pendingVerification, false);
  assert.equal(directPng.body.inQueue, false);
  assert.match(directPng.body.message, /published directly/i);
  assert.ok(env.THUMBNAILS_BUCKET.fileKeys().some((key) => key.startsWith("thumbnails/7003_")));
  assert.equal(env.THUMBNAILS_BUCKET.fileKeys().some((key) => key.startsWith("pending_thumbnails/7003_")), false);

  const directGif = await upload({
    bytes: gif,
    filename: "thumbnail.gif",
    levelID: 7004,
    modCode: credential,
    path: "/thumbnails/gif",
    type: "image/gif",
    url: "https://api.flozwer.org/mod/upload-gif"
  });
  assert.equal(directGif.response.status, 200);
  assert.equal(directGif.body.moderatorUpload, true);
  assert.equal(directGif.body.pendingVerification, false);
  assert.ok(env.THUMBNAILS_BUCKET.fileKeys().some((key) => key.startsWith("thumbnails/gif/7004_")));

  const suggestion = await upload({
    accountID: 0,
    levelID: 7005,
    path: "/suggestions",
    url: "https://api.flozwer.org/api/suggestions/upload",
    username: "player",
    levelMeta: {
      levelName: "Pending Test",
      creatorName: "Creator",
      stars: 7,
      levelLength: 3
    }
  });
  assert.equal(suggestion.response.status, 200);
  assert.equal(suggestion.body.message, "Suggestion uploaded successfully");
  assert.match(suggestion.body.filename, /^suggestions\/7005_/);
  assert.ok(env.THUMBNAILS_BUCKET.fileKeys().some((key) => key.startsWith("suggestions/7005_")));
  assert.deepEqual(
    env.SYSTEM_BUCKET.jsonValue("data/queue/suggestions/7005.json").map((item) => item.category),
    ["verify"]
  );
  assert.equal(
    env.SYSTEM_BUCKET.jsonValue("data/levelmeta/7005.json").levelName,
    "Pending Test"
  );

  const pendingUploads = [];
  for (let index = 0; index < 6; index += 1) {
    pendingUploads.push(await upload({
      accountID: 5151,
      levelID: 7010,
      username: "player",
      levelMeta: index === 0 ? {
        levelName: "Seven Pending",
        creatorName: "Player",
        stars: 9,
        levelLength: 4
      } : undefined
    }));
  }
  pendingUploads.push(await upload({
    accountID: 5151,
    bytes: gif,
    filename: "seventh.gif",
    levelID: 7010,
    path: "/thumbnails/gif",
    type: "image/gif",
    url: "https://api.flozwer.org/mod/upload-gif",
    username: "player"
  }));
  assert.equal(pendingUploads.every(({ response }) => response.status === 200), true);

  const queue = env.SYSTEM_BUCKET.jsonValue("data/queue/thumbnails/7010.json");
  assert.equal(queue.length, 7);
  assert.equal(new Set(queue.map((item) => item.filename)).size, 7);
  assert.equal(
    env.THUMBNAILS_BUCKET.fileKeys().filter((key) => key.startsWith("pending_thumbnails/7010_")).length,
    7
  );

  const eighth = await upload({ accountID: 5151, levelID: 7010, username: "player" });
  assert.equal(eighth.response.status, 409);
  assert.match(eighth.body.error, /max 7/i);
  assert.equal(env.SYSTEM_BUCKET.jsonValue("data/queue/thumbnails/7010.json").length, 7);

  const selectedGif = queue.find((item) => item.format === "gif");
  assert.ok(selectedGif);
  const acceptRequest = new Request("https://api.flozwer.org/api/queue/accept/7010", {
    body: JSON.stringify({
      accountID: 4242,
      category: "verify",
      levelId: 7010,
      targetFilename: selectedGif.filename,
      username: "testmod"
    }),
    headers: {
      "Content-Type": "application/json",
      "X-API-Key": env.API_KEY,
      "X-Mod-Code": credential
    },
    method: "POST"
  });
  const acceptWaiters = [];
  const accepted = await handleAcceptQueue(acceptRequest, env, {
    waitUntil(promise) {
      acceptWaiters.push(Promise.resolve(promise));
    }
  });
  await Promise.all(acceptWaiters);
  assert.equal(accepted.status, 200);
  assert.ok(env.THUMBNAILS_BUCKET.fileKeys().some(
    (key) => key.startsWith("thumbnails/7010_") && key.endsWith(".gif")
  ));
  assert.equal(
    env.THUMBNAILS_BUCKET.fileKeys().some((key) => key.startsWith("pending_thumbnails/7010_")),
    false
  );
  assert.equal(env.SYSTEM_BUCKET.jsonValue("data/queue/thumbnails/7010.json"), undefined);

  const concurrent = await Promise.all(
    Array.from({ length: 8 }, () => upload({
      accountID: 5151,
      levelID: 7011,
      username: "player"
    }))
  );
  assert.equal(concurrent.filter(({ response }) => response.status === 200).length, 7);
  assert.equal(concurrent.filter(({ response }) => response.status === 409).length, 1);
  assert.equal(env.SYSTEM_BUCKET.jsonValue("data/queue/thumbnails/7011.json").length, 7);
  assert.equal(
    env.THUMBNAILS_BUCKET.fileKeys().filter((key) => key.startsWith("pending_thumbnails/7011_")).length,
    7
  );

  console.log("thumbnail upload routing tests passed");
} finally {
  globalThis.fetch = originalFetch;
  if (originalCaches === undefined) delete globalThis.caches;
  else globalThis.caches = originalCaches;
}
