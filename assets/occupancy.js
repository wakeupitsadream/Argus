// Симуляция занятости мест — ДЕМО-РЕЖИМ.
//
// Этот модуль экспортирует тот же интерфейс, который будет экспортировать
// боевой источник данных на API LANGAME Software (ключ выдаётся владельцу
// клуба в панели). При подключении реальной интеграции заменяется только
// этот файл — остальной код сайта не меняется.
//
// Симуляция детерминирована: профиль загрузки от часа суток и дня недели
// + хеш-джиттер от «ведра времени». Никакого Math.random — картинка живая,
// но воспроизводимая в любой момент показа.

// FNV-1a, 32 бита — стабильный хеш строк.
export function hash32(str) {
  let h = 0x811c9dc5;
  for (let i = 0; i < str.length; i++) {
    h ^= str.charCodeAt(i);
    h = Math.imul(h, 0x01000193);
  }
  return h >>> 0;
}

// «Ведро времени»: номер интервала в minutes минут от эпохи.
export function bucket(date, minutes = 10) {
  return Math.floor(date.getTime() / 60000 / minutes);
}

// Развёртка zonesLayout клуба в плоский список мест со стабильными id.
export function seatsOf(club) {
  const seats = [];
  for (const block of club.zonesLayout) {
    for (let i = 0; i < block.count; i++) {
      seats.push({
        id: `${block.prefix}${String(i + 1).padStart(2, '0')}`,
        zone: block.zone,
      });
    }
  }
  return seats;
}

// Кусочно-линейный профиль занятости 0..1 по опорным часам.
const PROFILE_WEEKDAY = [
  [0, 0.55], [3, 0.42], [6, 0.30], [9, 0.20], [11, 0.18],
  [14, 0.30], [16, 0.45], [18, 0.65], [20, 0.88], [22, 0.80], [24, 0.55],
];
const PROFILE_WEEKEND = [
  [0, 0.62], [3, 0.50], [6, 0.32], [9, 0.25], [11, 0.35],
  [13, 0.60], [15, 0.60], [18, 0.75], [20, 0.92], [22, 0.85], [24, 0.62],
];

export function loadProfile(hour, isWeekend) {
  const pts = isWeekend ? PROFILE_WEEKEND : PROFILE_WEEKDAY;
  const h = ((hour % 24) + 24) % 24;
  for (let i = 0; i < pts.length - 1; i++) {
    const [h0, v0] = pts[i];
    const [h1, v1] = pts[i + 1];
    if (h >= h0 && h <= h1) {
      return h1 === h0 ? v0 : v0 + ((v1 - v0) * (h - h0)) / (h1 - h0);
    }
  }
  return pts[pts.length - 1][1];
}

// VIP днём берут реже, вечером — чаще; PS5-комната чуть «липче» вечером.
function zoneBias(zone, hour) {
  const day = hour >= 6 && hour < 18;
  if (zone === 'vip') return day ? -0.06 : 0.05;
  if (zone === 'ps5') return day ? -0.02 : 0.03;
  return 0;
}

const clamp01 = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// ~10% мест «дышат» на минутном ведре — между 10-минутными тиками
// единичные места приходят/уходят, остальные стабильны.
function seatBucket(clubId, seatId, date) {
  const breathing = hash32(`${clubId}~${seatId}`) % 10 === 0;
  return bucket(date, breathing ? 1 : 10);
}

export function seatBusy(clubId, seat, date) {
  const hour = date.getHours();
  const dow = date.getDay();
  const isWeekend = dow === 0 || dow === 6;
  let p = loadProfile(hour, isWeekend) + zoneBias(seat.zone, hour);
  // дыхание клуба целиком: ±7 п.п. раз в 10 минут
  p += ((hash32(`${clubId}:${bucket(date, 10)}`) % 15) - 7) / 100;
  p = clamp01(p, 0.04, 0.96);
  const roll = (hash32(`${clubId}|${seat.id}|${seatBucket(clubId, seat.id, date)}`) % 1000) / 1000;
  return roll < p;
}

// Снимок занятости клуба на момент date.
// Гарантии для витрины: хотя бы одно место свободно и хотя бы одно занято —
// клуб работает 24/7, и CTA «выбрать место» не должен умирать в пике.
export function snapshot(club, date) {
  const seats = seatsOf(club);
  const map = new Map();
  for (const seat of seats) {
    map.set(seat.id, { zone: seat.zone, busy: seatBusy(club.id, seat, date) });
  }
  const states = [...map.values()];
  const overrideId = [...map.keys()]
    .sort((a, b) => hash32(`${club.id}#${a}`) - hash32(`${club.id}#${b}`))[0];
  if (states.every((s) => s.busy)) map.get(overrideId).busy = false;
  else if (states.every((s) => !s.busy)) map.get(overrideId).busy = true;
  const byZone = {};
  let free = 0;
  for (const [, s] of map) {
    byZone[s.zone] ??= { free: 0, total: 0 };
    byZone[s.zone].total += 1;
    if (!s.busy) {
      byZone[s.zone].free += 1;
      free += 1;
    }
  }
  return { free, busy: map.size - free, total: map.size, byZone, seats: map };
}

// Суммарно свободных мест по сети (для hero).
export function totalFree(clubs, date) {
  return clubs.reduce((sum, club) => sum + snapshot(club, date).free, 0);
}
