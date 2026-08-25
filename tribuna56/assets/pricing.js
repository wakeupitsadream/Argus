// Калькулятор стоимости заявки. Чистые функции без DOM.
// Один и тот же расчет — в браузере (мгновенный пересчет) и на сервере
// (api/request.js не верит цене с клиента и считает сам).

import { SERVICES, BUNDLE } from './data.js';

// ids услуг → {items, subtotal, discount, discountLabel, total}.
// Неизвестные id игнорируются, дубликаты не задваивают.
export function quoteServices(ids) {
  const selected = new Set(Array.isArray(ids) ? ids : []);
  const items = SERVICES.filter((s) => selected.has(s.id));
  const subtotal = items.reduce((sum, s) => sum + s.price, 0);
  const bundled = BUNDLE.ids.every((id) => selected.has(id));
  const discount = bundled ? BUNDLE.discount : 0;
  return {
    items,
    subtotal,
    discount,
    discountLabel: bundled ? BUNDLE.label : null,
    total: subtotal - discount,
  };
}

// 3500 → «3 500 ₽» (неразрывные пробелы).
export function formatRub(n) {
  const num = Math.round(Number(n) || 0);
  const sign = num < 0 ? '−' : '';
  const digits = String(Math.abs(num));
  const grouped = digits.replace(/\B(?=(\d{3})+(?!\d))/g, ' ');
  return `${sign}${grouped} ₽`;
}

// plural(5, 'матч', 'матча', 'матчей') → 'матчей'
export function plural(n, one, few, many) {
  const abs = Math.abs(Math.trunc(Number(n) || 0));
  const d10 = abs % 10;
  const d100 = abs % 100;
  if (d10 === 1 && d100 !== 11) return one;
  if (d10 >= 2 && d10 <= 4 && (d100 < 12 || d100 > 14)) return few;
  return many;
}
