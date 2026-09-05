import test from 'node:test';
import assert from 'node:assert/strict';
import handler from '../api/matches.js';

function makeRes() {
  return {
    statusCode: 200,
    headers: {},
    body: null,
    setHeader(k, v) { this.headers[k] = v; },
    status(c) { this.statusCode = c; return this; },
    json(o) { this.body = o; return this; },
  };
}
const makeReq = (query = {}) => ({ method: 'GET', query, headers: {} });

const ENV_KEYS = ['SUPABASE_URL', 'SUPABASE_SERVICE_KEY', 'DATABASE_URL'];
async function withEnv(env, fn) {
  const saved = {};
  for (const k of ENV_KEYS) { saved[k] = process.env[k]; delete process.env[k]; }
  Object.assign(process.env, env);
  try { return await fn(); } finally {
    for (const k of ENV_KEYS) {
      if (saved[k] === undefined) delete process.env[k];
      else process.env[k] = saved[k];
    }
  }
}
async function withFetchSpy(routes, fn) {
  const calls = [];
  const orig = globalThis.fetch;
  globalThis.fetch = async (url, opts = {}) => {
    calls.push({ url: String(url), opts });
    const r = (routes && routes(String(url))) || {};
    return { ok: r.ok !== false, status: r.status || 200, text: async () => r.body ?? '[]' };
  };
  try { return await fn(calls); } finally { globalThis.fetch = orig; }
}
const SB = { SUPABASE_URL: 'https://x.supabase.co', SUPABASE_SERVICE_KEY: 'k' };

test('archive=1: только завершенные С ВИДЕО, свежие первыми, лимит', async () => {
  const rows = [
    { id: 3, team_home: 'Юниор', team_away: 'Сарматы', starts_at: '2026-09-20T10:00:00Z', status: 'finished', stream_url: 'https://vk.com/video-1_3', highlights_url: null },
    { id: 2, team_home: 'Юниор', team_away: 'АкБарс', starts_at: '2026-09-13T10:00:00Z', status: 'finished', stream_url: null, highlights_url: null }, // без видео — мимо
    { id: 1, team_home: 'Сарматы', team_away: 'Союз', starts_at: '2026-09-06T10:00:00Z', status: 'finished', stream_url: null, highlights_url: 'https://vk.com/video-1_1' },
  ];
  await withEnv(SB, () => withFetchSpy(() => ({ body: JSON.stringify(rows) }), async (calls) => {
    const res = makeRes();
    await handler(makeReq({ archive: '1', limit: '10' }), res);
    assert.equal(res.body.ok, true);
    assert.deepEqual(res.body.matches.map((m) => m.id), [3, 1]);
    assert.match(calls[0].url, /status=eq\.finished/);
    assert.match(calls[0].url, /order=starts_at\.desc/);
    assert.match(res.headers['Cache-Control'], /s-maxage/);
  }));
});

test('archive=1 без БД → unavailable без кэша', async () => {
  await withEnv({}, async () => {
    const res = makeRes();
    await handler(makeReq({ archive: '1' }), res);
    assert.equal(res.body.error, 'unavailable');
    assert.equal(res.headers['Cache-Control'], 'no-store');
  });
});
