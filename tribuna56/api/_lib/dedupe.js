// Дедупликация матчей при импорте. Чистые функции без DOM и без сети.

import { dateKey } from '../../assets/format.js';

// «ЮНИОР-2012» ≡ «юниор 2012» ≡ «Юниор—2012»: нижний регистр, ё→е,
// кавычки долой, любые дефисы/тире → пробел, схлопнуть пробелы.
export function normalizeTeam(s) {
  return String(s || '')
    .toLowerCase()
    .replace(/ё/g, 'е')
    .replace(/[«»"'’‘“”„]/g, '')
    .replace(/[‐‑‒–—―-]/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

export function matchFingerprint(sport, teamHome, teamAway, key) {
  return [sport, key, normalizeTeam(teamHome), normalizeTeam(teamAway)].join('|');
}

// a, b: {sport, teamHome, teamAway, startsAt}. Один и тот же матч =
// одинаковый отпечаток (спорт+день+команды) и старт в пределах 2 часов.
export function isSameMatch(a, b) {
  const ta = Date.parse(a && a.startsAt);
  const tb = Date.parse(b && b.startsAt);
  if (Number.isNaN(ta) || Number.isNaN(tb)) return false;
  if (Math.abs(ta - tb) > 2 * 3600_000) return false;
  return matchFingerprint(a.sport, a.teamHome, a.teamAway, dateKey(a.startsAt)) ===
         matchFingerprint(b.sport, b.teamHome, b.teamAway, dateKey(b.startsAt));
}

// Стабильный sourceKey, когда у источника нет внешнего ID матча.
export function fallbackSourceKey(m) {
  return [
    dateKey(m.startsAt),
    normalizeTeam(m.teamHome),
    normalizeTeam(m.teamAway),
    normalizeTeam(m.league || ''),
  ].join('|');
}
