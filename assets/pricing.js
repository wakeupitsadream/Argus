// Калькулятор тарифов ARGUS. Чистые функции: ни DOM, ни Date.now —
// одни и те же формулы работают в браузере, в тестах и в будущем бэкенде.
//
// Семантика (согласована с прайсом, спорное окно 6–10 трактуем так):
// - почасовая: каждый час визита тарифицируется своим окном (день [6,18), ночь [18,6));
// - ночной набор 3/5 ч действует, только если все его часы ночные; набор
//   покрывает первые часы визита, остаток — почасово;
// - пакет применим, если час прихода внутри его окна; покрывает часы до конца
//   окна, остаток после окна — почасово.

export function clampHours(hours) {
  const n = Math.round(Number(hours) || 0);
  return Math.min(12, Math.max(1, n));
}

export function normalizeHour(hour) {
  const n = Math.round(Number(hour) || 0);
  return ((n % 24) + 24) % 24;
}

export function rateWindow(hour) {
  const h = normalizeHour(hour);
  return h >= 6 && h < 18 ? 'day' : 'night';
}

// Стоимость interval почасово с разбиением по окнам тарифа.
export function hourlyCost(pricing, zoneId, startHour, hours) {
  const start = normalizeHour(startHour);
  const n = clampHours(hours);
  const breakdown = [];
  let total = 0;
  for (let i = 0; i < n; i++) {
    const h = (start + i) % 24;
    const win = rateWindow(h);
    const perHour = pricing.hourly[win].prices[zoneId];
    total += perHour;
    const last = breakdown[breakdown.length - 1];
    if (last && last.window === win) {
      last.hours += 1;
      last.sum += perHour;
    } else {
      breakdown.push({ window: win, hours: 1, perHour, sum: perHour });
    }
  }
  return { total, breakdown };
}

function allNight(startHour, hours) {
  for (let i = 0; i < hours; i++) {
    if (rateWindow((startHour + i) % 24) !== 'night') return false;
  }
  return true;
}

// Варианты с ночными наборами: точный набор или «набор + остаток почасово».
export function bundleOptions(pricing, zoneId, startHour, hours) {
  const start = normalizeHour(startHour);
  const n = clampHours(hours);
  const options = [];
  for (const b of pricing.nightBundles) {
    if (n < b.hours) continue;
    if (!allNight(start, b.hours)) continue;
    const price = b.prices[zoneId];
    if (n === b.hours) {
      options.push({
        kind: 'bundle', id: b.id, label: `Набор «${b.name}»`,
        total: price,
        detail: `${b.hours} ч набором за фиксированную цену`,
      });
    } else {
      const rest = hourlyCost(pricing, zoneId, (start + b.hours) % 24, n - b.hours);
      options.push({
        kind: 'bundle+hourly', id: b.id, label: `Набор «${b.name}» + ${n - b.hours} ч почасово`,
        total: price + rest.total,
        detail: `${price} ₽ набор + ${rest.total} ₽ почасово`,
      });
    }
  }
  return options;
}

// Попадает ли час в окно [from, to) с возможным переходом через полночь.
export function inWindow(hour, from, to) {
  const h = normalizeHour(hour);
  if (from < to) return h >= from && h < to;
  return h >= from || h < to; // окно через полночь
}

// Сколько часов остаётся от hour до конца окна to (через полночь — по модулю 24).
function hoursUntil(hour, to) {
  return ((to - hour) % 24 + 24) % 24 || 24;
}

// Пакеты: применимы по часу прихода; визит длиннее окна → пакет + хвост почасово.
export function packageOptions(pricing, zoneId, startHour, hours) {
  const start = normalizeHour(startHour);
  const n = clampHours(hours);
  const options = [];
  for (const p of pricing.packages) {
    if (!inWindow(start, p.from, p.to)) continue;
    const covered = hoursUntil(start, p.to);
    const price = p.prices[zoneId];
    if (n <= covered) {
      options.push({
        kind: 'package', id: p.id, label: `Пакет «${p.name}»`,
        total: price,
        detail: `Фиксированная цена до ${String(p.to).padStart(2, '0')}:00`,
      });
    } else {
      const rest = hourlyCost(pricing, zoneId, p.to % 24, n - covered);
      options.push({
        kind: 'package+hourly', id: p.id, label: `Пакет «${p.name}» + ${n - covered} ч почасово`,
        total: price + rest.total,
        detail: `${price} ₽ пакет + ${rest.total} ₽ почасово после ${String(p.to).padStart(2, '0')}:00`,
      });
    }
  }
  return options;
}

// Полный расчёт: все варианты, отсортированные по цене, лучший и экономия.
export function quote(pricing, zoneId, startHour, hours) {
  if (zoneId === 'ps5' || !pricing.hourly.day.prices[zoneId]) {
    return { ps5: true, zoneId, options: [], best: null, hourlyTotal: null, savings: 0 };
  }
  const start = normalizeHour(startHour);
  const n = clampHours(hours);
  const hourly = hourlyCost(pricing, zoneId, start, n);
  const options = [
    {
      kind: 'hourly', id: 'hourly', label: 'Почасовая оплата',
      total: hourly.total,
      detail: hourly.breakdown
        .map((b) => `${b.hours} ч × ${b.perHour} ₽ (${b.window === 'day' ? 'день' : 'ночь'})`)
        .join(' + '),
    },
    ...bundleOptions(pricing, zoneId, start, n),
    ...packageOptions(pricing, zoneId, start, n),
  ].sort((a, b) => a.total - b.total);
  const best = options[0];
  return {
    zoneId, startHour: start, hours: n,
    hourlyTotal: hourly.total,
    options, best,
    savings: best.kind === 'hourly' ? 0 : hourly.total - best.total,
  };
}

// Главный совет: чем лучший вариант выгоднее почасовой оплаты.
export function advice(q) {
  if (!q || q.ps5 || !q.best || q.best.kind === 'hourly' || q.savings <= 0) return null;
  return `${q.best.label} — выгоднее почасовой на ${formatRub(q.savings)}`;
}

// Подсказка-сдвиг: если прийти к началу окна пакета «Ночь», выйдет дешевле.
export function shiftAdvice(pricing, zoneId, startHour, hours) {
  const start = normalizeHour(startHour);
  if (zoneId === 'ps5') return null;
  if (start < 19 || start > 21) return null;
  const now = quote(pricing, zoneId, start, hours);
  const night = pricing.packages.find((p) => p.id === 'night');
  if (!night) return null;
  const atTen = quote(pricing, zoneId, night.from, hours);
  if (atTen.best && atTen.best.total < now.best.total) {
    return `Придёте к ${String(night.from).padStart(2, '0')}:00 — пакет «${night.name}» за ${formatRub(atTen.best.total)}`;
  }
  return null;
}

const NBSP = '\u00a0';

export function formatRub(n) {
  const s = String(Math.round(n)).replace(/\B(?=(\d{3})+(?!\d))/g, NBSP);
  return `${s}${NBSP}₽`;
}

export function plural(n, one, few, many) {
  const abs = Math.abs(n) % 100;
  const d = abs % 10;
  if (abs > 10 && abs < 20) return many;
  if (d === 1) return one;
  if (d >= 2 && d <= 4) return few;
  return many;
}

export function formatHourRange(startHour, hours) {
  const start = normalizeHour(startHour);
  const end = (start + clampHours(hours)) % 24;
  const f = (h) => `${String(h).padStart(2, '0')}:00`;
  return `${f(start)}–${f(end)}`;
}
