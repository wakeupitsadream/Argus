// Мелкие HTTP-хелперы serverless-функций.

// Тело запроса: Vercel обычно уже парсит JSON, но строка тоже встречается.
export function readBody(req) {
  let body = req.body;
  if (typeof body === 'string') {
    try { body = JSON.parse(body); } catch { body = {}; }
  }
  return body && typeof body === 'object' ? body : {};
}

export function methodNotAllowed(res, allow) {
  res.setHeader('Allow', allow);
  return res.status(405).json({ ok: false, error: 'method' });
}
