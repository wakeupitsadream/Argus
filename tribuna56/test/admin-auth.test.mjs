import test from 'node:test';
import assert from 'node:assert/strict';
import { checkAdmin } from '../api/_lib/auth.js';

function makeRes() {
  return {
    statusCode: 200,
    body: null,
    setHeader() {},
    status(c) { this.statusCode = c; return this; },
    json(o) { this.body = o; return this; },
  };
}

function makeReq(token, ip = '3.3.3.1') {
  const headers = { 'x-forwarded-for': ip };
  if (token !== undefined) headers['authorization'] = `Bearer ${token}`;
  return { method: 'GET', headers, query: {} };
}

async function withAdminToken(value, fn) {
  const saved = process.env.ADMIN_TOKEN;
  if (value === undefined) delete process.env.ADMIN_TOKEN;
  else process.env.ADMIN_TOKEN = value;
  try {
    return await fn();
  } finally {
    if (saved === undefined) delete process.env.ADMIN_TOKEN;
    else process.env.ADMIN_TOKEN = saved;
  }
}

test('без ADMIN_TOKEN в env админка выключена → 503', async () => {
  await withAdminToken(undefined, () => {
    const res = makeRes();
    assert.equal(checkAdmin(makeReq('любой', '3.3.3.2'), res), false);
    assert.equal(res.statusCode, 503);
  });
});

test('нет заголовка → 401', async () => {
  await withAdminToken('secret-token', () => {
    const res = makeRes();
    assert.equal(checkAdmin(makeReq(undefined, '3.3.3.3'), res), false);
    assert.equal(res.statusCode, 401);
  });
});

test('неверный токен → 401', async () => {
  await withAdminToken('secret-token', () => {
    const res = makeRes();
    assert.equal(checkAdmin(makeReq('wrong-token!', '3.3.3.4'), res), false);
    assert.equal(res.statusCode, 401);
  });
});

test('токен другой длины не роняет timingSafeEqual', async () => {
  await withAdminToken('secret-token', () => {
    const res = makeRes();
    assert.equal(checkAdmin(makeReq('x', '3.3.3.5'), res), false);
    assert.equal(res.statusCode, 401);
  });
});

test('верный токен → пропускает, ответ не тронут', async () => {
  await withAdminToken('secret-token', () => {
    const res = makeRes();
    assert.equal(checkAdmin(makeReq('secret-token', '3.3.3.6'), res), true);
    assert.equal(res.statusCode, 200);
    assert.equal(res.body, null);
  });
});

test('после 10 неудач с IP — 429 даже с верным токеном', async () => {
  await withAdminToken('secret-token', () => {
    const ip = '3.3.3.7';
    for (let i = 0; i < 10; i++) {
      const res = makeRes();
      assert.equal(checkAdmin(makeReq('wrong', ip), res), false);
      assert.equal(res.statusCode, 401, `попытка ${i + 1}`);
    }
    const res11 = makeRes();
    assert.equal(checkAdmin(makeReq('wrong', ip), res11), false);
    assert.equal(res11.statusCode, 429);
    const resOk = makeRes();
    assert.equal(checkAdmin(makeReq('secret-token', ip), resOk), false);
    assert.equal(resOk.statusCode, 429);
    // другой IP не задет
    const resOther = makeRes();
    assert.equal(checkAdmin(makeReq('secret-token', '3.3.3.8'), resOther), true);
  });
});
