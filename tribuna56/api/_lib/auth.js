// Авторизация админки: Bearer ADMIN_TOKEN из env.
// Сравнение — timingSafeEqual; brute-force глушится лимитом неудачных попыток.

import { timingSafeEqual } from 'node:crypto';
import { record, countRecent, clientIp } from './ratelimit.js';

const FAIL_LIMIT = 10;
const FAIL_WINDOW_MS = 60_000;

function safeEqual(a, b) {
  const ba = Buffer.from(String(a));
  const bb = Buffer.from(String(b));
  if (ba.length !== bb.length) return false;
  return timingSafeEqual(ba, bb);
}

// true — авторизован; false — ответ (401/429/503) уже записан в res.
export function checkAdmin(req, res) {
  const token = process.env.ADMIN_TOKEN;
  if (!token) {
    res.status(503).json({ ok: false, error: 'admin_disabled' });
    return false;
  }
  const key = `auth:${clientIp(req)}`;
  if (countRecent(key, FAIL_WINDOW_MS) >= FAIL_LIMIT) {
    res.status(429).json({ ok: false, error: 'too_many_attempts' });
    return false;
  }
  const header = String(req.headers['authorization'] || '');
  const provided = header.startsWith('Bearer ') ? header.slice(7) : '';
  if (!provided || !safeEqual(provided, token)) {
    record(key);
    res.status(401).json({ ok: false, error: 'auth' });
    return false;
  }
  return true;
}
