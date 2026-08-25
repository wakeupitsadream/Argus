import test from 'node:test';
import assert from 'node:assert/strict';
import { quoteServices, formatRub, plural } from '../assets/pricing.js';
import { SERVICES, BUNDLE } from '../assets/data.js';

const price = (id) => SERVICES.find((s) => s.id === id).price;

test('пустой список услуг → ноль', () => {
  const q = quoteServices([]);
  assert.equal(q.total, 0);
  assert.equal(q.discount, 0);
  assert.deepEqual(q.items, []);
});

test('каждая услуга по отдельности', () => {
  for (const s of SERVICES) {
    const q = quoteServices([s.id]);
    assert.equal(q.total, s.price, s.id);
    assert.equal(q.items.length, 1);
  }
});

test('все услуги: сумма минус пакетная скидка', () => {
  const q = quoteServices(SERVICES.map((s) => s.id));
  const subtotal = SERVICES.reduce((sum, s) => sum + s.price, 0);
  assert.equal(q.subtotal, subtotal);
  assert.equal(q.discount, BUNDLE.discount);
  assert.equal(q.total, subtotal - BUNDLE.discount);
});

test('скидка только за полный пакет «эфир + хайлайты»', () => {
  assert.equal(quoteServices(['stream', 'highlights']).discount, BUNDLE.discount);
  assert.equal(quoteServices(['stream', 'personal']).discount, 0);
  assert.equal(quoteServices(['highlights']).discount, 0);
});

test('неизвестный id игнорируется, дубликаты не задваивают', () => {
  const q = quoteServices(['stream', 'stream', 'нечто']);
  assert.equal(q.total, price('stream'));
  assert.equal(q.items.length, 1);
});

test('formatRub: неразрывные пробелы', () => {
  assert.equal(formatRub(3500), '3 500 ₽');
  assert.equal(formatRub(999), '999 ₽');
  assert.equal(formatRub(1234567), '1 234 567 ₽');
  assert.equal(formatRub(0), '0 ₽');
});

test('plural: русские формы', () => {
  const f = (n) => plural(n, 'матч', 'матча', 'матчей');
  assert.equal(f(1), 'матч');
  assert.equal(f(2), 'матча');
  assert.equal(f(5), 'матчей');
  assert.equal(f(11), 'матчей');
  assert.equal(f(21), 'матч');
  assert.equal(f(114), 'матчей');
});
