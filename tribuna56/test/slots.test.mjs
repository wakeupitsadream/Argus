import test from 'node:test';
import assert from 'node:assert/strict';
import { isSlotFree, conflictWith } from '../assets/slots.js';

// Подтвержденная съемка: суббота 10:00–11:30 по Оренбургу (+05:00), буфер 90 мин.
const busy = [{ starts_at: '2026-09-12T10:00:00+05:00', duration_min: 90 }];

test('пустой список → свободно', () => {
  assert.equal(isSlotFree([], '2026-09-12T10:00:00+05:00', 90, 90), true);
});

test('другой день → свободно', () => {
  assert.equal(isSlotFree(busy, '2026-09-13T10:00:00+05:00', 90, 90), true);
});

test('точное пересечение → занято', () => {
  assert.equal(isSlotFree(busy, '2026-09-12T10:30:00+05:00', 90, 90), false);
  assert.equal(conflictWith(busy, '2026-09-12T10:30:00+05:00', 90, 90), busy[0]);
});

test('границы с буфером: конец съемки 11:30 + 90 мин → свободно ровно с 13:00', () => {
  assert.equal(isSlotFree(busy, '2026-09-12T13:00:00+05:00', 90, 90), true);
  assert.equal(isSlotFree(busy, '2026-09-12T12:59:00+05:00', 90, 90), false);
});

test('слот до занятого: конец кандидата + буфер должен уложиться до 10:00', () => {
  // кандидат 07:00–08:30, +90 буфер = ровно 10:00 → свободно
  assert.equal(isSlotFree(busy, '2026-09-12T07:00:00+05:00', 90, 90), true);
  assert.equal(isSlotFree(busy, '2026-09-12T07:01:00+05:00', 90, 90), false);
});

test('два подтвержденных подряд: окно между ними мало', () => {
  const two = [
    { starts_at: '2026-09-12T10:00:00+05:00', duration_min: 90 },
    { starts_at: '2026-09-12T15:00:00+05:00', duration_min: 90 },
  ];
  // 13:00–14:30 упирается буфером (14:30+90=16:00) во вторую съемку с 15:00
  assert.equal(isSlotFree(two, '2026-09-12T13:00:00+05:00', 90, 90), false);
  assert.equal(isSlotFree(two, '2026-09-12T19:00:00+05:00', 90, 90), true);
});

test('невалидная дата → свободно (деградация, не пугаем клиента)', () => {
  assert.equal(isSlotFree(busy, 'вчера вечером', 90, 90), true);
  assert.equal(isSlotFree(busy, '', 90, 90), true);
});

test('записи с битой датой в списке пропускаются', () => {
  const dirty = [{ starts_at: 'мусор' }, ...busy];
  assert.equal(isSlotFree(dirty, '2026-09-12T10:30:00+05:00', 90, 90), false);
  assert.equal(isSlotFree(dirty, '2026-09-13T10:00:00+05:00', 90, 90), true);
});
