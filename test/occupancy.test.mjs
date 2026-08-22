import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { CLUBS } from '../assets/data.js';
import {
  hash32, bucket, seatsOf, loadProfile, snapshot, totalFree,
} from '../assets/occupancy.js';

const salm = CLUBS.find((c) => c.id === 'salm');
const lenin = CLUBS.find((c) => c.id === 'lenin');

test('seatsOf: количество и стабильные id', () => {
  const s1 = seatsOf(salm);
  const s2 = seatsOf(lenin);
  assert.equal(s1.length, 31); // 18 + 8 + 4 + PS5
  assert.equal(s2.length, 21);
  assert.equal(new Set(s1.map((s) => s.id)).size, 31);
  assert.equal(s1[0].id, 'C01');
  assert.equal(s1[17].id, 'C18');
  assert.equal(s1[30].id, 'P01');
  assert.deepEqual(seatsOf(salm), s1); // детерминированный порядок
});

test('hash32: стабильность', () => {
  assert.equal(hash32(''), 2166136261);
  assert.equal(hash32('C01'), hash32('C01'));
  assert.notEqual(hash32('C01'), hash32('C02'));
});

test('bucket: 10-минутные вёдра', () => {
  const a = new Date('2026-08-21T20:00:30');
  const b = new Date('2026-08-21T20:09:59');
  const c = new Date('2026-08-21T20:10:01');
  assert.equal(bucket(a, 10), bucket(b, 10));
  assert.notEqual(bucket(b, 10), bucket(c, 10));
});

test('снимок детерминирован', () => {
  const d = new Date('2026-08-21T20:15:00');
  assert.deepEqual(snapshot(salm, d), snapshot(salm, d));
});

test('клубы дают разные картинки', () => {
  const d = new Date('2026-08-21T20:15:00');
  const a = [...snapshot(salm, d).seats.values()].map((s) => s.busy).join('');
  const b = [...snapshot(lenin, d).seats.values()].map((s) => s.busy).join('');
  assert.notEqual(a.slice(0, 21), b);
});

test('инварианты на развёртке недели: суммы и «жизнь» клуба', () => {
  for (let day = 17; day < 24; day++) {
    for (let hour = 0; hour < 24; hour += 3) {
      const d = new Date(2026, 7, day, hour, 5);
      for (const club of [salm, lenin]) {
        const snap = snapshot(club, d);
        assert.equal(snap.free + snap.busy, snap.total);
        assert.ok(snap.free >= 1, `нет свободных: ${club.id} ${d}`);
        assert.ok(snap.busy >= 1, `клуб пуст: ${club.id} ${d}`);
        const zoneSum = Object.values(snap.byZone)
          .reduce((acc, z) => acc + z.total, 0);
        assert.equal(zoneSum, snap.total);
      }
    }
  }
});

test('профиль загрузки: пики и спады', () => {
  assert.equal(loadProfile(20, false), 0.88);
  assert.equal(loadProfile(11, false), 0.18);
  assert.ok(loadProfile(15, true) > loadProfile(15, false)); // выходной день выше
  assert.ok(loadProfile(3, false) > loadProfile(8, false)); // ночные пакетники
  assert.ok(loadProfile(20, true) >= 0.9); // пик выходного
});

test('занятость следует профилю: вечер пятницы против утра вторника', () => {
  const fri20 = new Date(2026, 7, 21, 20, 15);
  const tue11 = new Date(2026, 7, 18, 11, 15);
  const busyAt = (d) => snapshot(salm, d).busy + snapshot(lenin, d).busy;
  assert.ok(busyAt(fri20) > busyAt(tue11) + 5);
});

test('totalFree: сумма по сети', () => {
  const d = new Date(2026, 7, 21, 20, 15);
  assert.equal(totalFree([salm, lenin], d), snapshot(salm, d).free + snapshot(lenin, d).free);
});

test('страж симуляции: в данных нет Math.random', () => {
  const src = readFileSync(new URL('../assets/occupancy.js', import.meta.url), 'utf8');
  assert.ok(!src.includes('Math.random('));
});
