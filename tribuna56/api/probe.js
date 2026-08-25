// ВРЕМЕННЫЙ разведчик форматов источников расписаний (SOURCES.md).
// Нужен, потому что среда разработки не имеет доступа к .ru-сайтам,
// а функции Vercel — имеют (этим же каналом будет пользоваться импортёр).
// Жестко ограничен списком хостов федераций/лиг — это НЕ открытый прокси.
// УДАЛИТЬ после наполнения каталога и написания адаптеров.

const ALLOWED_HOSTS = new Set([
  'oofs56.ru', 'www.oofs56.ru',
  'yflrussia.ru', 'www.yflrussia.ru',
  'r-hockey.ru', 'www.r-hockey.ru',
  'fh56.ru', 'www.fh56.ru',
  'pfo.fhr.ru', 'junior.fhr.ru',
  'russiabasket.ru', 'www.russiabasket.ru', 'orenburg.russiabasket.ru',
  'basket2.sportoteka.org',
  'kes-basket.ru', 'www.kes-basket.ru',
  'mfsprivolg.nagradion.ru', 'oofs.nagradion.ru',
]);

const SLICE = 48_000; // ответ отдаем страницами, чтобы не упереться в лимиты

export default async function handler(req, res) {
  res.setHeader('Cache-Control', 'no-store');
  const q = req.query || {};
  let target;
  try {
    target = new URL(String(q.url || ''));
  } catch {
    return res.status(400).json({ ok: false, error: 'bad_url' });
  }
  if (!['https:', 'http:'].includes(target.protocol) || !ALLOWED_HOSTS.has(target.hostname)) {
    return res.status(400).json({ ok: false, error: 'host_not_allowed' });
  }
  const start = Math.max(0, Number(q.start) || 0);

  try {
    const r = await fetch(target, {
      redirect: 'follow',
      headers: {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36',
        'Accept': 'text/html,application/json;q=0.9,*/*;q=0.8',
        'Accept-Language': 'ru-RU,ru;q=0.9',
        ...(q.origin ? { Origin: String(q.origin), Referer: String(q.origin) + '/' } : {}),
      },
    });
    const text = await r.text();
    return res.status(200).json({
      ok: true,
      status: r.status,
      final_url: r.url,
      total_len: text.length,
      start,
      body: text.slice(start, start + SLICE),
    });
  } catch (e) {
    return res.status(200).json({ ok: false, error: String((e && e.message) || e) });
  }
}
