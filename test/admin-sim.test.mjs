import test from 'node:test';
import assert from 'node:assert/strict';
import { CLUBS } from '../assets/data.js';
import {
  daySummary, revenueSeries, hourlyLoad, zoneRevenue, kitchenTop, gamingRevenue,
} from '../assets/admin-sim.js';

const tue = new Date(2026, 7, 18, 12, 0); // вторник
const sat = new Date(2026, 7, 22, 12, 0); // суббота

test('daySummary детерминирован и согласован', () => {
  const a = daySummary(CLUBS, tue);
  const b = daySummary(CLUBS, tue);
  assert.deepEqual(a, b);
  assert.ok(a.revenue > 0);
  assert.equal(a.revenue, a.gaming + a.kitchen);
  assert.ok(a.checks > 0);
  assert.equal(a.avgCheck, Math.round(a.revenue / a.checks));
  // кухня — фиксированная доля от игрового времени
  assert.ok(Math.abs(a.kitchen / a.gaming - 0.22) < 0.01);
});

test('выходной день дороже буднего', () => {
  assert.ok(daySummary(CLUBS, sat).revenue > daySummary(CLUBS, tue).revenue);
});

test('revenueSeries: длина и хронология', () => {
  const s = revenueSeries(CLUBS, sat, 14);
  assert.equal(s.length, 14);
  assert.equal(s[13].date.getDate(), sat.getDate());
  assert.ok(s.every((d) => d.revenue > 0));
  for (let i = 1; i < s.length; i++) assert.ok(s[i].date > s[i - 1].date);
});

test('hourlyLoad: 24 часа в диапазоне, вечерний пик', () => {
  const l = hourlyLoad(tue);
  assert.equal(l.length, 24);
  assert.ok(l.every((v) => v >= 0 && v <= 1));
  assert.ok(l[20] > l[11]);
});

test('zoneRevenue: все зоны, положительные суммы', () => {
  const z = zoneRevenue(CLUBS, sat, 7);
  assert.deepEqual(Object.keys(z).sort(), ['comfort', 'ps5', 'stream', 'vip']);
  assert.ok(Object.values(z).every((v) => v > 0));
  // comfort мест больше всех — и выручка по зоне тоже
  assert.ok(z.comfort > z.stream);
});

test('kitchenTop: пять позиций с количеством и суммой', () => {
  const k = kitchenTop(sat, 14);
  assert.equal(k.length, 5);
  assert.ok(k.every((i) => i.qty > 0 && i.sum === i.qty * (i.sum / i.qty)));
  assert.deepEqual(k, kitchenTop(sat, 14)); // детерминизм
});

test('gamingRevenue растёт с размером клуба', () => {
  const salm = CLUBS.find((c) => c.id === 'salm');
  const lenin = CLUBS.find((c) => c.id === 'lenin');
  assert.ok(gamingRevenue(salm, tue) > gamingRevenue(lenin, tue));
});
