import test from 'node:test';
import assert from 'node:assert/strict';
import { filterMatches, groupByDay, sortByStart, ageGroups, displayStatus } from '../assets/catalog.js';

const M = [
  { id: 1, sport: 'hockey', age_group: '2012 г.р.', starts_at: '2026-09-12T10:00:00+05:00', status: 'scheduled' },
  { id: 2, sport: 'football', age_group: '2013 г.р.', starts_at: '2026-09-12T12:00:00+05:00', status: 'scheduled' },
  { id: 3, sport: 'hockey', age_group: '2013 г.р.', starts_at: '2026-09-13T09:00:00+05:00', status: 'scheduled' },
];

test('фильтр по спорту', () => {
  assert.deepEqual(filterMatches(M, { sport: 'hockey' }).map((m) => m.id), [1, 3]);
});

test('фильтр по возрасту и комбинация', () => {
  assert.deepEqual(filterMatches(M, { age: '2013 г.р.' }).map((m) => m.id), [2, 3]);
  assert.deepEqual(filterMatches(M, { sport: 'hockey', age: '2013 г.р.' }).map((m) => m.id), [3]);
});

test('пустой фильтр и "all" возвращают всё', () => {
  assert.equal(filterMatches(M, {}).length, 3);
  assert.equal(filterMatches(M, { sport: 'all', age: 'all' }).length, 3);
});

test('фильтр по датам from/to', () => {
  const r = filterMatches(M, { from: '2026-09-12T11:00:00+05:00', to: '2026-09-13T00:00:00+05:00' });
  assert.deepEqual(r.map((m) => m.id), [2]);
});

test('сортировка по времени старта', () => {
  const shuffled = [M[2], M[0], M[1]];
  assert.deepEqual(sortByStart(shuffled).map((m) => m.id), [1, 2, 3]);
});

test('группировка по оренбургским суткам, в т.ч. через полночь', () => {
  const across = [
    { id: 10, starts_at: '2026-09-12T23:30:00+05:00' }, // 12-е по Оренбургу
    { id: 11, starts_at: '2026-09-12T19:30:00Z' },      // 00:30 13-го по Оренбургу
  ];
  const groups = groupByDay(across);
  assert.equal(groups.length, 2);
  assert.deepEqual(groups.map((g) => g.key), ['2026-09-12', '2026-09-13']);
});

test('ageGroups: уникальные, отсортированные', () => {
  assert.deepEqual(ageGroups(M), ['2012 г.р.', '2013 г.р.']);
});

test('displayStatus: будущий → upcoming, сегодняшний → today', () => {
  const now = '2026-09-12T06:00:00+05:00';
  assert.equal(displayStatus(M[0], now), 'today');
  assert.equal(displayStatus(M[2], now), 'upcoming');
});

test('displayStatus: live из БД важнее времени', () => {
  const m = { ...M[0], status: 'live', starts_at: '2026-09-20T10:00:00+05:00' };
  assert.equal(displayStatus(m, '2026-09-12T06:00:00+05:00'), 'live');
});

test('displayStatus: прошедший scheduled дозревает в finished', () => {
  assert.equal(displayStatus(M[0], '2026-09-12T13:00:00+05:00'), 'finished');
  // матч идет прямо сейчас, но админ не включил live → today
  assert.equal(displayStatus(M[0], '2026-09-12T10:30:00+05:00'), 'today');
});

test('displayStatus: canceled и finished из БД', () => {
  assert.equal(displayStatus({ ...M[0], status: 'canceled' }, '2026-09-01T00:00:00Z'), 'canceled');
  assert.equal(displayStatus({ ...M[0], status: 'finished' }, '2026-09-01T00:00:00Z'), 'finished');
});
