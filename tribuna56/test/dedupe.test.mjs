import test from 'node:test';
import assert from 'node:assert/strict';
import { normalizeTeam, matchFingerprint, isSameMatch, fallbackSourceKey } from '../api/_lib/dedupe.js';

test('normalizeTeam: регистр, дефисы, тире, кавычки', () => {
  assert.equal(normalizeTeam('ЮНИОР-2012'), 'юниор 2012');
  assert.equal(normalizeTeam('юниор 2012'), 'юниор 2012');
  assert.equal(normalizeTeam('Юниор—2012'), 'юниор 2012');
  assert.equal(normalizeTeam('«Сармат»  2013'), 'сармат 2013');
});

test('normalizeTeam: ё → е', () => {
  assert.equal(normalizeTeam('Орлёнок'), 'орленок');
});

test('одинаковый отпечаток при разном написании команд', () => {
  const a = matchFingerprint('hockey', 'ЮНИОР-2012', '«Сармат»', '2026-09-12');
  const b = matchFingerprint('hockey', 'юниор 2012', 'Сармат', '2026-09-12');
  assert.equal(a, b);
});

const base = {
  sport: 'hockey',
  teamHome: 'Юниор-2012',
  teamAway: 'Сармат-2012',
  startsAt: '2026-09-12T10:00:00+05:00',
};

test('isSameMatch: те же команды, сдвиг 1 час → тот же матч', () => {
  assert.equal(isSameMatch(base, { ...base, teamHome: 'ЮНИОР 2012', startsAt: '2026-09-12T11:00:00+05:00' }), true);
});

test('isSameMatch: сдвиг 3 часа → другой матч', () => {
  assert.equal(isSameMatch(base, { ...base, startsAt: '2026-09-12T13:30:00+05:00' }), false);
});

test('isSameMatch: другой спорт или другие команды → другой матч', () => {
  assert.equal(isSameMatch(base, { ...base, sport: 'football' }), false);
  assert.equal(isSameMatch(base, { ...base, teamAway: 'Металлург-2012' }), false);
});

test('isSameMatch: битые даты → не совпадает', () => {
  assert.equal(isSameMatch(base, { ...base, startsAt: 'мусор' }), false);
});

test('fallbackSourceKey стабилен к написанию', () => {
  const k1 = fallbackSourceKey({ ...base, league: 'Первенство области' });
  const k2 = fallbackSourceKey({ ...base, teamHome: 'ЮНИОР—2012', league: 'первенство области' });
  assert.equal(k1, k2);
  assert.match(k1, /^2026-09-12\|/);
});
