// Заявка с сайта → Telegram владельца клуба.
// env: TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID (добавляются в настройках Vercel).
// Демо не ломается без них: заявка логируется предупреждением, гостю — «принято».

// Примитивный rate-limit по IP. In-memory: у serverless-инстанса память
// эфемерна, так что это защита от заливки в рамках одного инстанса, не более.
const RATE = new Map();
const RATE_LIMIT = 5;
const RATE_WINDOW_MS = 60_000;

function tooMany(ip, now) {
  const hits = (RATE.get(ip) || []).filter((t) => now - t < RATE_WINDOW_MS);
  hits.push(now);
  RATE.set(ip, hits);
  return hits.length > RATE_LIMIT;
}

export default async function handler(req, res) {
  if (req.method !== 'POST') {
    res.setHeader('Allow', 'POST');
    return res.status(405).json({ ok: false, error: 'method' });
  }

  let body = req.body;
  if (typeof body === 'string') {
    try { body = JSON.parse(body); } catch { body = {}; }
  }
  body = body || {};

  // honeypot: поле «website» заполняют только боты — молча отвечаем «принято»
  if (body.website) return res.status(200).json({ ok: true });

  const name = String(body.name || '').trim();
  const digits = String(body.phone || '').replace(/\D/g, '');
  if (name.length < 2 || digits.length < 10 || body.consent !== true) {
    return res.status(400).json({ ok: false, error: 'validation' });
  }

  const ip = String(req.headers['x-forwarded-for'] || '').split(',')[0].trim() || 'local';
  if (tooMany(ip, Date.now())) return res.status(200).json({ ok: true });

  const token = process.env.TELEGRAM_BOT_TOKEN;
  const chatId = process.env.TELEGRAM_CHAT_ID;
  const lines = [
    '🎮 Заявка на бронь с сайта ARGUS',
    `Имя: ${name}`,
    `Телефон: ${body.phone}`,
    body.club ? `Клуб: ${body.club}` : null,
    body.seat ? `Место: ${body.seat}` : null,
    body.zone ? `Зона: ${body.zone}` : null,
    body.time ? `Время: ${body.time}` : null,
    body.price ? `Расчёт: ${body.price}` : null,
  ].filter(Boolean);

  if (!token || !chatId) {
    console.warn('[order] TELEGRAM_BOT_TOKEN/TELEGRAM_CHAT_ID не заданы — заявка не отправлена:', lines.join(' | '));
    return res.status(200).json({ ok: true });
  }

  try {
    const tg = await fetch(`https://api.telegram.org/bot${token}/sendMessage`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ chat_id: chatId, text: lines.join('\n') }),
    });
    if (!tg.ok) console.warn('[order] Telegram ответил', tg.status);
  } catch (e) {
    console.warn('[order] отправка в Telegram упала:', e && e.message);
  }
  return res.status(200).json({ ok: true });
}
