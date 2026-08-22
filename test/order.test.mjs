import test from 'node:test';
import assert from 'node:assert/strict';
import handler from '../api/order.js';

function makeRes() {
  return {
    statusCode: 0,
    headers: {},
    body: null,
    setHeader(k, v) { this.headers[k] = v; },
    status(code) { this.statusCode = code; return this; },
    json(obj) { this.body = obj; return this; },
  };
}

const validBody = {
  name: 'Тест', phone: '+7 999 123-45-67', consent: true,
  club: 'ARGUS', seat: 'C01', zone: 'COMFORT', time: '22:00–05:00', price: '1 100 ₽',
};

function withFetchSpy(fn) {
  const orig = globalThis.fetch;
  let calls = 0;
  globalThis.fetch = async () => { calls += 1; return { ok: true, status: 200 }; };
  return fn(() => calls).finally(() => { globalThis.fetch = orig; });
}

test('не-POST → 405', async () => {
  const res = makeRes();
  await handler({ method: 'GET', headers: {} }, res);
  assert.equal(res.statusCode, 405);
});

test('без согласия на ПД → 400', async () => {
  const res = makeRes();
  await handler(
    { method: 'POST', headers: {}, body: { ...validBody, consent: false } },
    res,
  );
  assert.equal(res.statusCode, 400);
});

test('пустое имя и короткий телефон → 400', async () => {
  for (const patch of [{ name: ' ' }, { phone: '123' }]) {
    const res = makeRes();
    await handler({ method: 'POST', headers: {}, body: { ...validBody, ...patch } }, res);
    assert.equal(res.statusCode, 400);
  }
});

test('без env-переменных → 200 ok и никакого fetch (демо не ломается)', async () => {
  delete process.env.TELEGRAM_BOT_TOKEN;
  delete process.env.TELEGRAM_CHAT_ID;
  await withFetchSpy(async (calls) => {
    const res = makeRes();
    await handler({ method: 'POST', headers: {}, body: { ...validBody } }, res);
    assert.equal(res.statusCode, 200);
    assert.deepEqual(res.body, { ok: true });
    assert.equal(calls(), 0);
  });
});

test('honeypot заполнен → 200 без отправки', async () => {
  process.env.TELEGRAM_BOT_TOKEN = 'x';
  process.env.TELEGRAM_CHAT_ID = 'y';
  await withFetchSpy(async (calls) => {
    const res = makeRes();
    await handler(
      { method: 'POST', headers: {}, body: { ...validBody, website: 'spam.example' } },
      res,
    );
    assert.equal(res.statusCode, 200);
    assert.equal(calls(), 0);
  });
  delete process.env.TELEGRAM_BOT_TOKEN;
  delete process.env.TELEGRAM_CHAT_ID;
});

test('env заданы, Telegram упал → всё равно 200 ok', async () => {
  process.env.TELEGRAM_BOT_TOKEN = 'x';
  process.env.TELEGRAM_CHAT_ID = 'y';
  const orig = globalThis.fetch;
  globalThis.fetch = async () => { throw new Error('network down'); };
  try {
    const res = makeRes();
    await handler({ method: 'POST', headers: { 'x-forwarded-for': '10.0.0.9' }, body: { ...validBody } }, res);
    assert.equal(res.statusCode, 200);
    assert.deepEqual(res.body, { ok: true });
  } finally {
    globalThis.fetch = orig;
    delete process.env.TELEGRAM_BOT_TOKEN;
    delete process.env.TELEGRAM_CHAT_ID;
  }
});

test('строковое тело JSON парсится', async () => {
  const res = makeRes();
  await handler({ method: 'POST', headers: {}, body: JSON.stringify(validBody) }, res);
  assert.equal(res.statusCode, 200);
});
