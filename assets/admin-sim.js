// Демо-данные админки. Детерминированная симуляция бизнес-показателей
// из того же профиля загрузки, что и схема залов: каждая цифра объяснима
// («ожидаемая занятость × тариф»), воспроизводима и помечена в UI как демо.
// Боевой источник (LANGAME Software / касса) заменит этот модуль тем же интерфейсом.
import { PRICING } from './data.js';
import { hash32, loadProfile, seatsOf } from './occupancy.js';
import { rateWindow } from './pricing.js';

const AVG_SESSION_HOURS = 3.2;   // средняя сессия для оценки числа чеков
const KITCHEN_SHARE = 0.22;      // кухня/бар поверх игрового времени

const dayKey = (d) => `${d.getFullYear()}-${d.getMonth() + 1}-${d.getDate()}`;

// ±8% дневной джиттер, чтобы ряд выглядел живым, но воспроизводимым
function dayJitter(clubId, d) {
  return 1 + ((hash32(`${clubId}@${dayKey(d)}`) % 17) - 8) / 100;
}

function zoneRate(zoneId, hour) {
  const win = rateWindow(hour);
  const prices = PRICING.hourly[win].prices;
  return prices[zoneId] ?? prices.vip; // PS5 нет в прайсе — оцениваем по VIP
}

// Ожидаемая выручка клуба за час: занятые места каждой зоны × тариф часа.
export function hourRevenue(club, date, hour) {
  const isWeekend = date.getDay() === 0 || date.getDay() === 6;
  const p = loadProfile(hour, isWeekend);
  let sum = 0;
  for (const block of club.zonesLayout) {
    sum += block.count * p * zoneRate(block.zone, hour);
  }
  return sum;
}

// Игровая выручка клуба за календарный день (без кухни).
export function gamingRevenue(club, date) {
  let sum = 0;
  for (let h = 0; h < 24; h++) sum += hourRevenue(club, date, h);
  return sum * dayJitter(club.id, date);
}

// Полный дневной срез: игровое время + кухня, чеки, средний чек.
export function daySummary(clubs, date) {
  let gaming = 0;
  let seatHours = 0;
  const isWeekend = date.getDay() === 0 || date.getDay() === 6;
  for (const club of clubs) {
    gaming += gamingRevenue(club, date);
    const seats = seatsOf(club).length;
    for (let h = 0; h < 24; h++) seatHours += seats * loadProfile(h, isWeekend);
  }
  const kitchen = Math.round(gaming * KITCHEN_SHARE);
  const revenue = Math.round(gaming) + kitchen;
  const checks = Math.max(1, Math.round(seatHours / AVG_SESSION_HOURS));
  return {
    date,
    revenue,
    gaming: Math.round(gaming),
    kitchen,
    checks,
    avgCheck: Math.round(revenue / checks),
  };
}

// Ряд по дням, заканчивая endDate включительно.
export function revenueSeries(clubs, endDate, days) {
  const out = [];
  for (let i = days - 1; i >= 0; i--) {
    const d = new Date(endDate.getFullYear(), endDate.getMonth(), endDate.getDate() - i, 12, 0);
    out.push(daySummary(clubs, d));
  }
  return out;
}

// Профиль загрузки по часам конкретного дня, 0..1.
export function hourlyLoad(date) {
  const isWeekend = date.getDay() === 0 || date.getDay() === 6;
  return Array.from({ length: 24 }, (_, h) => loadProfile(h, isWeekend));
}

// Выручка по зонам за период (игровое время).
export function zoneRevenue(clubs, endDate, days) {
  const perZone = {};
  for (let i = days - 1; i >= 0; i--) {
    const d = new Date(endDate.getFullYear(), endDate.getMonth(), endDate.getDate() - i, 12, 0);
    const isWeekend = d.getDay() === 0 || d.getDay() === 6;
    for (const club of clubs) {
      const j = dayJitter(club.id, d);
      for (const block of club.zonesLayout) {
        let s = 0;
        for (let h = 0; h < 24; h++) s += block.count * loadProfile(h, isWeekend) * zoneRate(block.zone, h);
        perZone[block.zone] = (perZone[block.zone] || 0) + s * j;
      }
    }
  }
  return Object.fromEntries(Object.entries(perZone).map(([z, v]) => [z, Math.round(v)]));
}

// Топ кухни за период — демо-позиции, количества детерминированы от периода.
export function kitchenTop(endDate, days) {
  const base = [
    ['Авторский чай', 200],
    ['Энергетики', 150],
    ['Бургер с курицей', 320],
    ['Паровой коктейль', 600],
    ['Картофель фри', 150],
  ];
  return base.map(([name, price], i) => {
    const qty = Math.round(
      (days * (8 - i * 1.2)) * (1 + ((hash32(`${name}#${dayKey(endDate)}`) % 9) - 4) / 100),
    );
    return { name, qty, sum: qty * price };
  });
}
