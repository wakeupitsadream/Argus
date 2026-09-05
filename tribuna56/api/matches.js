// Публичный каталог матчей. Только published; без служебных полей.
// БД недоступна → {ok:false, error:'unavailable'} со статусом 200:
// клиент показывает заглушку, посетитель не видит ошибок.

import { sbConfigured, sbSelect } from './_lib/supabase.js';
import { SPORT_IDS } from '../assets/data.js';

const PUBLIC_FIELDS = [
  'id', 'sport', 'league', 'age_group', 'team_home', 'team_away',
  'venue', 'address', 'starts_at', 'duration_min', 'status',
  'stream_url', 'highlights_url',
].join(',');

// По умолчанию показываем матчи от «4 часа назад»: идущие и только что
// закончившиеся не пропадают из каталога посреди эфира.
const LOOKBACK_MS = 4 * 3600_000;

// Кэш ТОЛЬКО на успешные ответы: заглушка об ошибке БД или 404,
// закэшированные CDN, минуту прятали бы каталог от всех посетителей.
const cacheOk = (res) => res.setHeader('Cache-Control', 's-maxage=60, stale-while-revalidate=300');
const noCache = (res) => res.setHeader('Cache-Control', 'no-store');

export default async function handler(req, res) {
  if (req.method !== 'GET') {
    res.setHeader('Allow', 'GET');
    return res.status(405).json({ ok: false, error: 'method' });
  }

  if (!sbConfigured()) {
    console.warn('[matches] SUPABASE_URL/SUPABASE_SERVICE_KEY не заданы');
    noCache(res);
    return res.status(200).json({ ok: false, error: 'unavailable' });
  }

  const q = req.query || {};
  try {
    if (q.id) {
      const id = Number(q.id);
      if (!Number.isInteger(id) || id <= 0) {
        noCache(res);
        return res.status(404).json({ ok: false, error: 'not_found' });
      }
      const rows = await sbSelect('matches',
        `select=${PUBLIC_FIELDS}&id=eq.${id}&published=is.true&limit=1`);
      if (!rows || !rows.length) {
        noCache(res); // только что опубликованный матч не должен минуту быть «не найден»
        return res.status(404).json({ ok: false, error: 'not_found' });
      }
      cacheOk(res);
      return res.status(200).json({ ok: true, match: rows[0] });
    }

    // Архив: завершенные эфиры с видео — портфолио подтягивает их само.
    // Фильтр «есть видео» делаем в JS: or=() PostgREST не переварил бы
    // транслятор Neon, а выборка тут маленькая.
    if (q.archive === '1') {
      const ap = new URLSearchParams();
      ap.set('select', PUBLIC_FIELDS);
      ap.set('published', 'is.true');
      ap.set('status', 'eq.finished');
      ap.set('order', 'starts_at.desc');
      ap.set('limit', '50');
      const rows = await sbSelect('matches', ap.toString());
      const lim = Math.min(Math.max(Number(q.limit) || 30, 1), 50);
      const withVideo = (rows || [])
        .filter((m) => m.stream_url || m.highlights_url)
        .slice(0, lim);
      cacheOk(res);
      return res.status(200).json({ ok: true, matches: withVideo });
    }

    const p = new URLSearchParams();
    p.set('select', PUBLIC_FIELDS);
    p.set('published', 'is.true');
    p.set('order', 'starts_at.asc');
    const limit = Math.min(Math.max(Number(q.limit) || 100, 1), 100);
    p.set('limit', String(limit));
    if (q.sport && SPORT_IDS.includes(q.sport)) p.append('sport', `eq.${q.sport}`);
    if (q.age) p.append('age_group', `eq.${String(q.age).slice(0, 60)}`);
    const from = q.from && !Number.isNaN(Date.parse(q.from))
      ? new Date(q.from) : new Date(Date.now() - LOOKBACK_MS);
    p.append('starts_at', `gte.${from.toISOString()}`);
    if (q.to && !Number.isNaN(Date.parse(q.to))) {
      p.append('starts_at', `lte.${new Date(q.to).toISOString()}`);
    }

    const rows = await sbSelect('matches', p.toString());
    cacheOk(res);
    return res.status(200).json({ ok: true, matches: rows || [] });
  } catch (e) {
    console.warn('[matches] Supabase недоступен:', e && e.message);
    noCache(res);
    return res.status(200).json({ ok: false, error: 'unavailable' });
  }
}
