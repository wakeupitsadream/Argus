import test from 'node:test';
import assert from 'node:assert/strict';
import { validateRequest } from '../assets/validate.js';

const valid = {
  match_id: 42,
  services: ['stream'],
  name: 'Анна',
  phone: '+7 (912) 345-67-89',
  consent: true,
};

test('валидная заявка с матчем из каталога', () => {
  const r = validateRequest(valid);
  assert.equal(r.ok, true);
  assert.equal(r.matchId, 42);
  assert.deepEqual(r.services, ['stream']);
  assert.equal(r.customMatch, null);
});

test('короткое имя', () => {
  const r = validateRequest({ ...valid, name: 'А' });
  assert.equal(r.ok, false);
  assert.ok(r.errors.name);
});

test('телефон меньше 10 цифр', () => {
  const r = validateRequest({ ...valid, phone: '12345' });
  assert.equal(r.ok, false);
  assert.ok(r.errors.phone);
});

test('без согласия', () => {
  const r = validateRequest({ ...valid, consent: false });
  assert.equal(r.ok, false);
  assert.ok(r.errors.consent);
});

test('услуги: пустой список и только чужие id', () => {
  assert.ok(validateRequest({ ...valid, services: [] }).errors.services);
  assert.ok(validateRequest({ ...valid, services: ['spa'] }).errors.services);
});

test('услуги: дубликаты схлопываются', () => {
  const r = validateRequest({ ...valid, services: ['stream', 'stream', 'personal'] });
  assert.deepEqual(r.services, ['stream', 'personal']);
});

test('нет ни матча, ни кастома', () => {
  const r = validateRequest({ ...valid, match_id: null });
  assert.equal(r.ok, false);
  assert.ok(r.errors.match);
});

test('кастомный матч валиден без match_id', () => {
  const r = validateRequest({
    ...valid,
    match_id: null,
    custom_match: { sport: 'hockey', teams: 'Юниор — Сармат', venue: 'ЛД «Звездный»', date_text: 'суббота 14:00' },
  });
  assert.equal(r.ok, true);
  assert.equal(r.matchId, null);
  assert.equal(r.customMatch.teams, 'Юниор — Сармат');
});

test('кастом без команд или даты — невалиден', () => {
  const base = { ...valid, match_id: null };
  assert.ok(validateRequest({ ...base, custom_match: { teams: '', date_text: 'суббота' } }).errors.match);
  assert.ok(validateRequest({ ...base, custom_match: { teams: 'А — Б', date_text: '' } }).errors.match);
});

test('оба заданы → приоритет каталожного, кастом отбрасывается', () => {
  const r = validateRequest({
    ...valid,
    custom_match: { teams: 'Юниор — Сармат', date_text: 'суббота 14:00' },
  });
  assert.equal(r.ok, true);
  assert.equal(r.matchId, 42);
  assert.equal(r.customMatch, null);
});

test('мусорный payload не роняет', () => {
  assert.equal(validateRequest(null).ok, false);
  assert.equal(validateRequest('строка').ok, false);
});
