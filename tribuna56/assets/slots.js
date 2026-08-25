// Занятость оператора. Чистые функции без DOM.
// Оператор один: между съемками нужен буфер на дорогу и монтаж камер.

import { SLOT } from './data.js';

const MIN = 60_000;

// busy: [{starts_at, duration_min}] — подтвержденные съемки.
// Возвращает первый конфликтующий элемент или null.
export function conflictWith(busy, startsAt, durationMin, bufferMin) {
  const start = Date.parse(startsAt);
  if (Number.isNaN(start)) return null; // невалидная дата — не пугаем клиента
  const dur = (Number(durationMin) || SLOT.defaultDurationMin) * MIN;
  const buf = (Number.isFinite(Number(bufferMin)) ? Number(bufferMin) : SLOT.bufferMin) * MIN;
  const end = start + dur;
  for (const b of Array.isArray(busy) ? busy : []) {
    const bStart = Date.parse(b && b.starts_at);
    if (Number.isNaN(bStart)) continue;
    const bEnd = bStart + (Number(b.duration_min) || SLOT.defaultDurationMin) * MIN;
    // свободно, только если между съемками остается полный буфер
    const fits = start >= bEnd + buf || end + buf <= bStart;
    if (!fits) return b;
  }
  return null;
}

export function isSlotFree(busy, startsAt, durationMin, bufferMin) {
  return conflictWith(busy, startsAt, durationMin, bufferMin) === null;
}
