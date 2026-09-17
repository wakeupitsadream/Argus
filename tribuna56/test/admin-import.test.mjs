import test from 'node:test';
import assert from 'node:assert/strict';
import handler from '../api/admin/import.js';

function makeRes() {
  return {
    statusCode: 200, headers: {}, body: null,
    setHeader(k, v) { this.headers[k] = v; },
    status(c) { this.statusCode = c; return this; },
    json(o) { this.body = o; return this; },
  };
}
const ENV = { SUPABASE_URL: 'https://x.supabase.co', SUPABASE_SERVICE_KEY: 'k', ADMIN_TOKEN: 'admin-secret' };
const KEYS = [...Object.keys(ENV), 'DATABASE_URL'];
async function withEnv(fn) {
  const saved = {};
  for (const k of KEYS) { saved[k] = process.env[k]; delete process.env[k]; }
  Object.assign(process.env, ENV);
  try { return await fn(); } finally {
    for (const k of KEYS) { if (saved[k] === undefined) delete process.env[k]; else process.env[k] = saved[k]; }
  }
}
async function withFetchSpy(routes, fn) {
  const calls = [];
  const orig = globalThis.fetch;
  globalThis.fetch = async (url, opts = {}) => {
    calls.push({ url: String(url), method: opts.method || 'GET', body: opts.body });
    const body = routes(String(url), opts.method || 'GET') ?? [];
    return { ok: true, status: 200, text: async () => JSON.stringify(body) };
  };
  try { return await fn(calls); } finally { globalThis.fetch = orig; }
}
const A = 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa';
const B = 'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb';
const item = (id, dup) => ({
  id, kind: 'new', source: 'rhockey', status: 'pending',
  payload: { normalized: { sport: 'hockey', teamHome: 'Юниор', teamAway: 'Сарматы', startsAt: '2026-10-01T10:00:00+05:00', sourceKey: `k-${id}` }, possible_duplicate_of: dup },
});

test('PATCH id=all approve: подтверждает всю очередь, «похож на дубль» оставляет', async () => {
  await withEnv(() => withFetchSpy((url, method) => {
    if (method === 'GET' && url.includes('import_queue') && url.includes('status=eq.pending')) return [item(A, null), item(B, 5)];
    if (method === 'PATCH' && url.includes(`id=eq.${A}`) && url.includes('status=eq.pending')) return [item(A, null)]; // claim
    if (method === 'POST' && url.includes('/matches')) return [{ id: 77 }];
    return [];
  }, async (calls) => {
    const res = makeRes();
    await handler({
      method: 'PATCH', query: { id: 'all' }, body: { action: 'approve' },
      headers: { authorization: 'Bearer admin-secret', 'x-forwarded-for': '9.9.9.1' },
    }, res);
    assert.equal(res.statusCode, 200);
    assert.deepEqual(res.body, { ok: true, approved: 1, skipped: 1, failed: 0 });
    // матч вставлен ровно один раз, и только для A
    const inserts = calls.filter((c) => c.method === 'POST' && c.url.includes('/matches'));
    assert.equal(inserts.length, 1);
    assert.match(inserts[0].body, /Юниор/);
    assert.ok(!calls.some((c) => c.method === 'PATCH' && c.url.includes(`id=eq.${B}`)), 'B не трогаем');
  }));
});

test('PATCH id=all с action=reject → validation (массовый только approve)', async () => {
  await withEnv(() => withFetchSpy(() => [], async () => {
    const res = makeRes();
    await handler({
      method: 'PATCH', query: { id: 'all' }, body: { action: 'reject' },
      headers: { authorization: 'Bearer admin-secret', 'x-forwarded-for': '9.9.9.2' },
    }, res);
    assert.equal(res.statusCode, 400);
    assert.equal(res.body.error, 'validation');
  }));
});
