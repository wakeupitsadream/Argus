// Примитивный in-memory rate-limit по ключу. Память serverless-инстанса
// эфемерна: это защита от заливки в рамках одного инстанса, не более
// (принято так же в эталоне ARGUS). Honeypot и валидация — вторая линия.

const buckets = new Map();

export function record(key, now = Date.now()) {
  const arr = buckets.get(key) || [];
  arr.push(now);
  buckets.set(key, arr);
}

export function countRecent(key, windowMs, now = Date.now()) {
  const arr = (buckets.get(key) || []).filter((t) => now - t < windowMs);
  buckets.set(key, arr);
  return arr.length;
}

// Записывает попытку и отвечает, не превышен ли лимит.
export function tooMany(key, limit, windowMs, now = Date.now()) {
  record(key, now);
  return countRecent(key, windowMs, now) > limit;
}

export function clientIp(req) {
  return String(req.headers['x-forwarded-for'] || '').split(',')[0].trim() || 'local';
}
