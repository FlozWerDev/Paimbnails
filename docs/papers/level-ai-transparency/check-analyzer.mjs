import assert from 'node:assert/strict';
import { webcrypto, createHash } from 'node:crypto';
import { gzipSync, deflateSync } from 'node:zlib';
import { readFileSync } from 'node:fs';

if (!globalThis.crypto) Object.defineProperty(globalThis, 'crypto', { value: webcrypto });
await import('./web/analyzer.js');
const audit = globalThis.PaimbnailsLevelAudit;
const results = [];
async function test(name, action) {
  await action();
  results.push(name);
}
const raw = 'kS38,1;1,1,2,0,3,0;1,1,2,30,3,0;1,1,2,60,3,0;';
const marked = 'kS38,1;1,211,2,0.000,3,0.000,21,100,64,1,67,1,121,1,134,1,128,0.2000,129,0.2000,6,2160.000;';

await test('Regular manual-style grid remains indeterminate', async () => {
  const report = await audit.analyze(raw);
  assert.equal(report.status, 'indeterminate');
  assert.equal(report.aiProbability, null);
  assert.equal(report.attributedModel, null);
  assert.equal(report.policyAction, 'none');
  assert.equal(report.metrics.objectCount, 3);
  assert.equal(report.metrics.gridAlignment.find(entry => entry.step === 30).fraction, 1);
  assert.equal(report.importerWatermark.compatible, false);
});
await test('Importer mark never proves AI or authenticates origin', async () => {
  const report = await audit.analyze(marked);
  assert.equal(report.importerWatermark.compatible, true);
  assert.equal(report.importerWatermark.flaggedRotationMarks, 1);
  assert.equal(report.importerWatermark.authenticated, false);
  assert.equal(report.importerWatermark.provesAI, false);
  assert.equal(report.status, 'indeterminate');
});
await test('Rotation normalization removes observed mark without proving human origin', async () => {
  const report = await audit.analyze(marked.replace('2160.000', '0.000'));
  assert.equal(report.importerWatermark.compatible, false);
  assert.equal(report.status, 'indeterminate');
});
await test('Unflagged rotation needs three matching observations', async () => {
  const object = '1,211,21,100,6,2160;';
  assert.equal((await audit.analyze(object.repeat(2))).importerWatermark.compatible, false);
  assert.equal((await audit.analyze(object.repeat(3))).importerWatermark.compatible, true);
});
await test('Geometric pairs require both ratio variants and sufficient pairs', async () => {
  function pair(ratio, y) { return `1,211,2,0,3,${y},21,100,128,${ratio},129,1;1,211,2,15,3,${y},21,100,128,${1 - ratio},129,1;`; }
  const both = pair(0.381966, 0) + pair(0.438447, 30) + pair(0.381966, 60);
  assert.equal((await audit.analyze(both)).importerWatermark.compatible, true);
  assert.equal((await audit.analyze(pair(0.381966, 0).repeat(3))).importerWatermark.compatible, false);
  assert.equal((await audit.analyze(both.replace('2,15,', '2,16,'))).importerWatermark.compatible, false);
});
await test('Gzip and zlib preserve the decoded exact digest', async () => {
  const plain = await audit.analyze(raw);
  for (const [format, bytes] of [['base64-gzip', gzipSync(raw)], ['base64-zlib', deflateSync(raw)]]) {
    const report = await audit.analyze(bytes.toString('base64url'));
    assert.equal(report.input.format, format);
    assert.equal(report.input.decodedSha256, plain.input.decodedSha256);
    assert.notEqual(report.input.rawSha256, plain.input.rawSha256);
    assert.equal(report.metrics.objectCount, 3);
  }
});
await test('Exact digest and multiset comparison have different invariants', async () => {
  const first = await audit.analyze(raw);
  const second = await audit.analyze('kS38,1;3,0,2,60,1,1;3,0,1,1,2,0;2,30,3,0,1,1;');
  assert.notEqual(first.input.decodedSha256, second.input.decodedSha256);
  assert.equal(first.input.propertyMultisetSha256, second.input.propertyMultisetSha256);
  assert.equal(first.input.rawSha256, createHash('sha256').update(raw).digest('hex'));
  const third = await audit.analyze(raw.replace('2,30,', '2,30.0,'));
  assert.notEqual(first.input.propertyMultisetSha256, third.input.propertyMultisetSha256);
});
await test('Multiset hashing preserves duplicate objects', async () => {
  const a = await audit.analyze('1,1,2,0,3,0;');
  const b = await audit.analyze('1,1,2,0,3,0;1,1,2,0,3,0;');
  assert.notEqual(a.input.propertyMultisetSha256, b.input.propertyMultisetSha256);
});
await test('Decoded exact digest preserves outer whitespace before parsing', async () => {
  const padded = '\n' + raw + '\n';
  const a = await audit.analyze(padded);
  const b = await audit.analyze(raw);
  assert.equal(a.input.rawSha256, a.input.decodedSha256);
  assert.notEqual(a.input.decodedSha256, b.input.decodedSha256);
  assert.equal(a.input.propertyMultisetSha256, b.input.propertyMultisetSha256);
});
await test('Unknown fields remain in syntactic identity', async () => {
  const a = await audit.analyze('1,1,99999,alpha;');
  const b = await audit.analyze('1,1,99999,beta;');
  assert.notEqual(a.input.propertyMultisetSha256, b.input.propertyMultisetSha256);
});
await test('Missing direct membership is descriptive and trigger coverage is limited', async () => {
  const report = await audit.analyze('1,211,57,100;1,1007,51,100;1,1268,51,999;1,9999,51,888;');
  assert.deepEqual(report.metrics.groups.targetsWithoutDirectMembership, [999]);
  assert.equal(report.metrics.groups.supportedTriggerCount, 2);
  assert.equal(report.status, 'indeterminate');
});
await test('Lexical decimal precision stays distinct from numeric equality', async () => {
  const report = await audit.analyze('1,211,2,30.000,3,15.0,128,1.0000,129,1;');
  assert.deepEqual(report.metrics.lexicalDecimalPrecision.x, [{ value: 3, count: 1 }]);
  assert.deepEqual(report.metrics.lexicalDecimalPrecision.scaleX, [{ value: 4, count: 1 }]);
});
await test('Object previews are bounded', async () => {
  const report = await audit.analyze('1,1,2,0,3,0;'.repeat(10001));
  assert.ok(report.preview.length <= 2500);
  assert.equal(report.metrics.objectCount, 10001);
});
for (const [name, value] of [
  ['empty input', ''], ['header only', 'kS38,1;'], ['odd token list', '1,211,2;'],
  ['duplicate keys', '1,211,1,1;'], ['non-finite numbers', '1,211,2,NaN;'],
  ['numeric overflow', '1,211,2,1e999;'], ['invalid group', '1,211,57,12.bad;'],
  ['zero group', '1,211,57,0;'], ['unsafe object identifier', '1,9007199254740993;'],
  ['empty record', '1,211;;'], ['prototype property', '1,211,__proto__,attack;'],
  ['unknown container', '{"levelString":"1,211;"}'], ['non-compressed base64', Buffer.from('1,211;').toString('base64')],
  ['broken compressed stream', 'H4sIAAAA'], ['empty value', '1,211,2,;']
]) await test('Reject ' + name, async () => { await assert.rejects(() => audit.analyze(value)); });
await test('Reject too many property pairs before object interpretation', async () => {
  await assert.rejects(() => audit.analyze('1,211,' + Array.from({ length: 512 }, (_, i) => `${i + 1000},x`).join(',') + ';'), /pares/);
});
await test('Reject oversized literal values', async () => {
  await assert.rejects(() => audit.analyze('1,211,999,' + 'a'.repeat(4097) + ';'), /largo/);
});
await test('Reject oversized input', async () => { await assert.rejects(() => audit.analyze('x'.repeat(audit.LIMITS.inputBytes + 1)), /8 MiB/); });
await test('Reject compressed output beyond decoded limit', async () => {
  const bytes = gzipSync('x'.repeat(audit.LIMITS.decodedBytes + 1));
  await assert.rejects(() => audit.analyze(bytes.toString('base64')), /32 MiB/);
});
await test('Reject more than 100,000 objects', async () => { await assert.rejects(() => audit.analyze('1,1;'.repeat(100001)), /100.000/); });
await test('Published manifest schema is valid JSON and labels structural validation', async () => {
  const schema = JSON.parse(readFileSync(new URL('./provenance.schema.json', import.meta.url)));
  assert.equal(schema.$schema, 'https://json-schema.org/draft/2020-12/schema');
  assert.match(schema.description, /does not authenticate/);
});
console.log(JSON.stringify({ analyzerVersion: audit.VERSION, passed: results.length, checks: results }, null, 2));
