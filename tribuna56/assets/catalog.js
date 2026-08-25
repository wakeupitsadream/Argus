// Фильтрация, группировка и статусы матчей каталога. Чистые функции без DOM.

import { dateKey } from './format.js';
import { SLOT } from './data.js';

export function sortByStart(list) {
  return [...(list || [])].sort((a, b) => Date.parse(a.starts_at) - Date.parse(b.starts_at));
}

// Фильтры каталога; пустой/отсутствующий фильтр пропускает всё.
export function filterMatches(list, { sport, age, from, to } = {}) {
  return (list || []).filter((m) => {
    if (sport && sport !== 'all' && m.sport !== sport) return false;
    if (age && age !== 'all' && m.age_group !== age) return false;
    const t = Date.parse(m.starts_at);
    if (from && t < Date.parse(from)) return false;
    if (to && t > Date.parse(to)) return false;
    return true;
  });
}

// → [{key: 'YYYY-MM-DD', matches: [...]}] по оренбургским суткам, отсортировано.
export function groupByDay(list) {
  const groups = new Map();
  for (const m of sortByStart(list)) {
    const key = dateKey(m.starts_at);
    if (!key) continue;
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(m);
  }
  return [...groups.entries()].map(([key, matches]) => ({ key, matches }));
}

// Уникальные возрастные группы для фильтра, отсортированы по алфавиту.
export function ageGroups(list) {
  const set = new Set((list || []).map((m) => m.age_group).filter(Boolean));
  return [...set].sort((a, b) => a.localeCompare(b, 'ru'));
}

// Статус для отображения: canceled | live | finished | today | upcoming.
// live/canceled/finished — только от статуса в БД (админ управляет),
// scheduled сам «дозревает» в finished по времени.
export function displayStatus(m, nowIso) {
  if (m.status === 'canceled') return 'canceled';
  if (m.status === 'live') return 'live';
  if (m.status === 'finished') return 'finished';
  const now = nowIso ? Date.parse(nowIso) : Date.now();
  const start = Date.parse(m.starts_at);
  const end = start + (Number(m.duration_min) || SLOT.defaultDurationMin) * 60_000;
  if (now >= end) return 'finished';
  if (dateKey(m.starts_at) === dateKey(new Date(now).toISOString())) return 'today';
  return 'upcoming';
}

export const STATUS_LABELS = {
  live: 'В эфире',
  today: 'Сегодня',
  upcoming: 'Скоро',
  finished: 'Завершен',
  canceled: 'Отменен',
};
