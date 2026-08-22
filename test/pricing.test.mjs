import test from 'node:test';
import assert from 'node:assert/strict';
import { PRICING } from '../assets/data.js';
import {
  rateWindow, hourlyCost, bundleOptions, packageOptions, quote,
  advice, shiftAdvice, formatRub, plural, formatHourRange, clampHours,
} from '../assets/pricing.js';

const NBSP = '\u00a0';

test('rateWindow: границы окон', () => {
  assert.equal(rateWindow(6), 'day');
  assert.equal(rateWindow(17), 'day');
  assert.equal(rateWindow(18), 'night');
  assert.equal(rateWindow(23), 'night');
  assert.equal(rateWindow(0), 'night');
  assert.equal(rateWindow(5), 'night');
});

test('почасовая днём', () => {
  assert.equal(hourlyCost(PRICING, 'comfort', 10, 4).total, 600);
  assert.equal(hourlyCost(PRICING, 'vip', 11, 2).total, 380);
  assert.equal(hourlyCost(PRICING, 'stream', 17, 1).total, 240);
});

test('почасовая через границу 18:00 и через полночь', () => {
  const cross = hourlyCost(PRICING, 'comfort', 17, 2);
  assert.equal(cross.total, 350); // 150 день + 200 ночь
  assert.deepEqual(cross.breakdown.map((b) => b.window), ['day', 'night']);
  assert.equal(hourlyCost(PRICING, 'comfort', 23, 4).total, 800); // 23,0,1,2 — всё ночь
  assert.equal(hourlyCost(PRICING, 'comfort', 4, 4).total, 700); // 2×200 + 2×150
});

test('ночные наборы: точные и составные', () => {
  const n3 = bundleOptions(PRICING, 'comfort', 19, 3);
  assert.equal(n3.length, 1);
  assert.equal(n3[0].total, 540);
  const n5 = bundleOptions(PRICING, 'vip', 22, 5).find((o) => o.id === 'n5');
  assert.equal(n5.total, 1000);
  // 4:00+3 — час 6 уже дневной, набор неприменим
  assert.equal(bundleOptions(PRICING, 'comfort', 4, 3).length, 0);
  // составной: 19:00+7 → n5 (900) + 2 ч ночью (400)
  const comp = bundleOptions(PRICING, 'comfort', 19, 7).find((o) => o.id === 'n5');
  assert.equal(comp.total, 1300);
  assert.equal(comp.kind, 'bundle+hourly');
});

test('пакеты: применимость по часу прихода', () => {
  const night = packageOptions(PRICING, 'comfort', 22, 7).find((o) => o.id === 'night');
  assert.equal(night.total, 1100);
  assert.equal(night.kind, 'package');
  assert.equal(packageOptions(PRICING, 'comfort', 21, 5).find((o) => o.id === 'night'), undefined);
  const day = packageOptions(PRICING, 'comfort', 10, 8).find((o) => o.id === 'day');
  assert.equal(day.total, 600);
  assert.equal(packageOptions(PRICING, 'comfort', 9, 4).find((o) => o.id === 'day'), undefined);
  const nosleep = packageOptions(PRICING, 'comfort', 2, 8).find((o) => o.id === 'nosleep');
  assert.equal(nosleep.total, 550);
});

test('пакет + хвост почасово: 22:00 + 13 ч', () => {
  const opt = packageOptions(PRICING, 'comfort', 22, 13).find((o) => o.id === 'night');
  // окно пакета кончается в 10:00 (12 ч), но clampHours режет визит до 12 ч → чистый пакет
  assert.equal(clampHours(13), 12);
  assert.equal(opt.total, 1100);
});

test('quote: контрольный пример из ТЗ — Comfort 22:00 + 7 ч', () => {
  const q = quote(PRICING, 'comfort', 22, 7);
  assert.equal(q.hourlyTotal, 1400);
  assert.equal(q.best.id, 'night');
  assert.equal(q.best.total, 1100);
  assert.equal(q.savings, 300);
  const totals = q.options.map((o) => o.total);
  assert.deepEqual(totals, [...totals].sort((a, b) => a - b));
});

test('quote: 23:00 + 5 ч Comfort — лучший вариант набор 900', () => {
  const q = quote(PRICING, 'comfort', 23, 5);
  assert.equal(q.hourlyTotal, 1000);
  assert.equal(q.best.id, 'n5');
  assert.equal(q.best.total, 900);
});

test('quote: PS5 — честный контракт без цены', () => {
  const q = quote(PRICING, 'ps5', 20, 3);
  assert.equal(q.ps5, true);
  assert.equal(q.best, null);
  assert.deepEqual(q.options, []);
});

test('quote: клампы входа', () => {
  assert.equal(quote(PRICING, 'comfort', 10, 0).hours, 1);
  assert.equal(quote(PRICING, 'comfort', 10, 99).hours, 12);
  assert.equal(quote(PRICING, 'comfort', 26, 1).startHour, 2);
});

test('advice: текст экономии', () => {
  const q = quote(PRICING, 'comfort', 22, 7);
  assert.equal(advice(q), `Пакет «Ночь» — выгоднее почасовой на 300${NBSP}₽`);
  assert.equal(advice(quote(PRICING, 'comfort', 12, 2)), null); // почасовая лучшая
});

test('shiftAdvice: пришёл в 20:00 на 8 ч — подсказка про 22:00', () => {
  const hint = shiftAdvice(PRICING, 'comfort', 20, 8);
  assert.ok(hint.includes('22:00'));
  assert.ok(hint.includes(`1${NBSP}100${NBSP}₽`));
  assert.equal(shiftAdvice(PRICING, 'comfort', 12, 3), null);
});

test('formatRub и plural', () => {
  assert.equal(formatRub(1300), `1${NBSP}300${NBSP}₽`);
  assert.equal(formatRub(150), `150${NBSP}₽`);
  assert.equal(plural(1, 'место', 'места', 'мест'), 'место');
  assert.equal(plural(2, 'место', 'места', 'мест'), 'места');
  assert.equal(plural(5, 'место', 'места', 'мест'), 'мест');
  assert.equal(plural(11, 'место', 'места', 'мест'), 'мест');
  assert.equal(plural(21, 'место', 'места', 'мест'), 'место');
  assert.equal(plural(111, 'место', 'места', 'мест'), 'мест');
});

test('formatHourRange', () => {
  assert.equal(formatHourRange(22, 7), '22:00–05:00');
  assert.equal(formatHourRange(10, 4), '10:00–14:00');
});
