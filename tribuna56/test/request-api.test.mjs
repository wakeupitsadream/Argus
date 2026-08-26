import test from 'node:test';
import assert from 'node:assert/strict';
import handler from '../api/request.js';
import { quoteServices } from '../assets/pricing.js';

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

function makeReq({ method = 'POST', body = {}, ip = '1.1.1.1' } = {}) {
  return { method, body, headers: { 'x-forwarded-for': ip }, query: {} };
}

const ENV_KEYS = ['SUPABASE_URL', 'SUPABASE_SERVICE_KEY', 'DATABASE_URL', 'TELEGRAM_BOT_TOKEN', 'TELEGRAM_CHAT_ID'];

async function withEnv(env, fn) {
  const saved = {};
  for (const k of ENV_KEYS) { saved[k] = process.env[k]; delete process.env[k]; }
  Object.assign(process.env, env);
  try {
    return await fn();
  } finally {
    for (const k of ENV_KEYS) {
      if (saved[k] === undefined) delete process.env[k];
      else process.env[k] = saved[k];
    }
  }
}

// routes(url) → {ok, status, body} либо undefined (тогда дефолтный ответ '[]')
async function withFetchSpy(routes, fn) {
  const calls = [];
  const orig = globalThis.fetch;
  globalThis.fetch = async (url, opts = {}) => {
    const u = String(url);
    calls.push({ url: u, opts });
    const r = (routes && routes(u)) || {};
    return {
      ok: r.ok !== false,
      status: r.status || 200,
      text: async () => r.body ?? '[]',
      json: async () => JSON.parse(r.body ?? '[]'),
    };
  };
  try {
    return await fn(calls);
  } finally {
    globalThis.fetch = orig;
  }
}

const valid = {
  match_id: 5,
  services: ['stream'],
  name: 'Анна',
  phone: '+7 912 345-67-89',
  consent: true,
};

test('GET → 405', async () => {
  const res = makeRes();
  await handler(makeReq({ method: 'GET' }), res);
  assert.equal(res.statusCode, 405);
});

test('honeypot: боту отвечаем «принято» и никуда не ходим', async () => {
  await withEnv({ TELEGRAM_BOT_TOKEN: 't', TELEGRAM_CHAT_ID: 'c' }, () =>
    withFetchSpy(null, async (calls) => {
      const res = makeRes();
      await handler(makeReq({ body: { ...valid, website: 'spam' }, ip: '2.2.2.1' }), res);
      assert.equal(res.statusCode, 200);
      assert.deepEqual(res.body, { ok: true });
      assert.equal(calls.length, 0);
    }));
});

test('невалидная заявка → 400 с полями', async () => {
  await withEnv({}, () =>
    withFetchSpy(null, async (calls) => {
      const res = makeRes();
      await handler(makeReq({ body: { ...valid, consent: false }, ip: '2.2.2.2' }), res);
      assert.equal(res.statusCode, 400);
      assert.equal(res.body.error, 'validation');
      assert.ok(res.body.fields.consent);
      assert.equal(calls.length, 0);
    }));
});

test('без env вообще: деградация — 200 ok, fetch не вызывается', async () => {
  await withEnv({}, () =>
    withFetchSpy(null, async (calls) => {
      const res = makeRes();
      await handler(makeReq({ body: valid, ip: '2.2.2.3' }), res);
      assert.equal(res.statusCode, 200);
      assert.deepEqual(res.body, { ok: true });
      assert.equal(calls.length, 0);
    }));
});

test('Telegram настроен, БД нет → сообщение уходит с расчетом и пометкой', async () => {
  await withEnv({ TELEGRAM_BOT_TOKEN: 't', TELEGRAM_CHAT_ID: 'c' }, () =>
    withFetchSpy(null, async (calls) => {
      const res = makeRes();
      await handler(makeReq({ body: valid, ip: '2.2.2.4' }), res);
      assert.equal(res.statusCode, 200);
      assert.equal(calls.length, 1);
      assert.match(calls[0].url, /api\.telegram\.org/);
      const text = JSON.parse(calls[0].opts.body).text;
      assert.match(text, /Анна/);
      assert.match(text, /Расчет:/);
      assert.match(text, /БД недоступна/);
    }));
});

test('Supabase упал, Telegram жив → 200 и сообщение все равно уходит', async () => {
  const routes = (u) => (u.includes('sb.test') ? { ok: false, status: 500, body: 'boom' } : {});
  await withEnv({
    SUPABASE_URL: 'https://sb.test', SUPABASE_SERVICE_KEY: 'k',
    TELEGRAM_BOT_TOKEN: 't', TELEGRAM_CHAT_ID: 'c',
  }, () =>
    withFetchSpy(routes, async (calls) => {
      const res = makeRes();
      await handler(makeReq({ body: valid, ip: '2.2.2.5' }), res);
      assert.equal(res.statusCode, 200);
      assert.deepEqual(res.body, { ok: true });
      const tg = calls.filter((c) => c.url.includes('api.telegram.org'));
      assert.equal(tg.length, 1);
      assert.match(JSON.parse(tg[0].opts.body).text, /БД недоступна/);
    }));
});

test('rate-limit: 6-й запрос за минуту с одного IP молча глотается', async () => {
  await withEnv({ TELEGRAM_BOT_TOKEN: 't', TELEGRAM_CHAT_ID: 'c' }, () =>
    withFetchSpy(null, async (calls) => {
      for (let i = 0; i < 5; i++) {
        const res = makeRes();
        await handler(makeReq({ body: valid, ip: '9.9.9.9' }), res);
        assert.equal(res.statusCode, 200);
      }
      assert.equal(calls.length, 5);
      const res6 = makeRes();
      await handler(makeReq({ body: valid, ip: '9.9.9.9' }), res6);
      assert.equal(res6.statusCode, 200);
      assert.deepEqual(res6.body, { ok: true });
      assert.equal(calls.length, 5); // Telegram больше не вызывался
    }));
});

test('price_quote с клиента игнорируется — в БД уходит серверный расчет', async () => {
  await withEnv({ SUPABASE_URL: 'https://sb.test', SUPABASE_SERVICE_KEY: 'k' }, () =>
    withFetchSpy(null, async (calls) => {
      const res = makeRes();
      await handler(makeReq({
        body: { ...valid, services: ['stream', 'highlights'], price_quote: 1 },
        ip: '2.2.2.6',
      }), res);
      assert.equal(res.statusCode, 200);
      const insert = calls.find((c) => c.url.includes('/rest/v1/requests') && c.opts.method === 'POST');
      assert.ok(insert, 'insert в requests должен произойти');
      const row = JSON.parse(insert.opts.body)[0];
      assert.equal(row.price_quote, quoteServices(['stream', 'highlights']).total);
      // матч #5 не нашелся в БД (пустой ответ) → match_id не пишем, чтобы не упасть на FK
      assert.equal(row.match_id, null);
    }));
});
