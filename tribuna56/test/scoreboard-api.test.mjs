import test from 'node:test';
import assert from 'node:assert/strict';
import handler from '../api/scoreboard.js';

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

function makeReq({ method = 'GET', body = {}, query = {}, ip = '2.2.2.2' } = {}) {
  return { method, body, query, headers: { 'x-forwarded-for': ip } };
}

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

// шпион Supabase REST: routes(url, opts) → {ok, status, body} | undefined
async function withFetchSpy(routes, fn) {
  const calls = [];
  const orig = globalThis.fetch;
  globalThis.fetch = async (url, opts = {}) => {
    const u = String(url);
    calls.push({ url: u, opts });
    const r = (routes && routes(u, opts)) || {};
    return {
      ok: r.ok !== false,
      status: r.status || 200,
      text: async () => r.body ?? '[]',
    };
  };
  try { return await fn(calls); } finally { globalThis.fetch = orig; }
}

const SB = { SUPABASE_URL: 'https://x.supabase.co', SUPABASE_SERVICE_KEY: 'k' };

test('без БД → unavailable со статусом 200', async () => {
  await withEnv({}, async () => {
    const res = makeRes();
    await handler(makeReq({ query: { id: 'abc123' } }), res);
    assert.equal(res.statusCode, 200);
    assert.equal(res.body.error, 'unavailable');
  });
});

test('GET: кривой id → 400, ответ всегда no-store', async () => {
  await withEnv(SB, () => withFetchSpy(null, async () => {
    const res = makeRes();
    await handler(makeReq({ query: { id: 'НЕ ТО' } }), res);
    assert.equal(res.statusCode, 400);
    assert.equal(res.headers['Cache-Control'], 'no-store');
  }));
});

test('GET: нет такого табло → 404; есть → данные санитизированы', async () => {
  await withEnv(SB, () => withFetchSpy((u) => {
    if (u.includes('id=eq.zzz999')) return { body: '[]' };
    return { body: JSON.stringify([{ data: { sport: 'blimps', home: { score: '3' } }, updated_at: 't' }]) };
  }, async () => {
    let res = makeRes();
    await handler(makeReq({ query: { id: 'zzz999' } }), res);
    assert.equal(res.statusCode, 404);
    res = makeRes();
    await handler(makeReq({ query: { id: 'abc123' } }), res);
    assert.equal(res.body.ok, true);
    assert.equal(res.body.data.sport, 'hockey'); // мусорный спорт заменен
    assert.equal(res.body.data.home.score, 3);
  }));
});

test('POST: первая запись создает (и перепроверяет token), чужой token → 403, свой → апдейт', async () => {
  let inserted = false;
  await withEnv(SB, () => withFetchSpy((u, opts) => {
    if (opts.method === 'POST') { inserted = true; return { body: '[]' }; }
    if (opts.method === 'PATCH') return { body: '[]' };
    if (u.includes('id=eq.new111')) {
      // до вставки строки нет; после — строка с нашим token (гонко-проверка)
      return { body: inserted ? JSON.stringify([{ token: 'righttoken1' }]) : '[]' };
    }
    return { body: JSON.stringify([{ token: 'righttoken1', data: { rev: 1 } }]) };
  }, async (calls) => {
    let res = makeRes();
    await handler(makeReq({ method: 'POST', body: { id: 'new111', token: 'righttoken1', data: { rev: 1 } } }), res);
    assert.equal(res.body.ok, true);
    const ins = calls.find((c) => c.opts.method === 'POST');
    assert.ok(ins, 'должен быть INSERT');
    assert.match(ins.url, /on_conflict=id/); // гонка первых записей не роняет

    res = makeRes();
    await handler(makeReq({ method: 'POST', body: { id: 'old111', token: 'wrongtoken1', data: { rev: 2 } } }), res);
    assert.equal(res.statusCode, 403);

    res = makeRes();
    await handler(makeReq({ method: 'POST', body: { id: 'old111', token: 'righttoken1', data: { rev: 2, home: { name: 'Юниор', score: 2 } } } }), res);
    assert.equal(res.body.ok, true);
    const patch = calls.find((c) => c.opts.method === 'PATCH');
    assert.ok(patch, 'должен быть PATCH существующей строки');
    assert.match(patch.opts.body, /Юниор/);
  }));
});

test('POST: устаревшая ревизия (out-of-order дебаунс) не затирает свежую запись', async () => {
  await withEnv(SB, () => withFetchSpy((u, opts) => {
    if (opts.method === 'PATCH') return { body: '[]' };
    return { body: JSON.stringify([{ token: 'righttoken1', data: { rev: 6, home: { score: 2 } } }]) };
  }, async (calls) => {
    const res = makeRes();
    await handler(makeReq({ method: 'POST', body: { id: 'old111', token: 'righttoken1', data: { rev: 5, home: { score: 1 } } } }), res);
    assert.equal(res.body.ok, true);
    assert.equal(res.body.stale, true);
    assert.ok(!calls.some((c) => c.opts.method === 'PATCH'), 'записи быть не должно');
  }));
});

test('GET: отдает serverNow для синхронизации часов устройств', async () => {
  await withEnv(SB, () => withFetchSpy(() => (
    { body: JSON.stringify([{ data: {}, updated_at: 't' }]) }
  ), async () => {
    const res = makeRes();
    await handler(makeReq({ query: { id: 'abc123' } }), res);
    assert.equal(res.body.ok, true);
    assert.ok(Number.isFinite(res.body.serverNow));
  }));
});

test('POST: валидация id/token/размера', async () => {
  await withEnv(SB, () => withFetchSpy(null, async () => {
    for (const body of [
      { id: 'ab', token: 'goodtoken11' },                     // id короткий
      { id: 'abcd12', token: 'short' },                       // token короткий
      { id: 'abcd12', token: 'goodtoken11', data: { tournament: 'x'.repeat(9000) } }, // жирный payload
    ]) {
      const res = makeRes();
      await handler(makeReq({ method: 'POST', body }), res);
      assert.equal(res.statusCode, 400, JSON.stringify(body).slice(0, 60));
    }
  }));
});

test('PUT → 405', async () => {
  await withEnv(SB, async () => {
    const res = makeRes();
    await handler(makeReq({ method: 'PUT' }), res);
    assert.equal(res.statusCode, 405);
  });
});
