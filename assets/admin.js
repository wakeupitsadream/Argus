// Админка ARGUS: PIN-гейт, KPI, графики (SVG, без библиотек), компы, журнал операций.
// Демо-показатели — из admin-sim.js (детерминированная симуляция, помечено в UI).
// Журнал и пометки «обслуживание» — реальный локальный учёт в localStorage.
import { CLUBS, ZONES } from './data.js';
import { formatRub, plural } from './pricing.js';
import { snapshot, seatsOf } from './occupancy.js';
import { daySummary, revenueSeries, hourlyLoad, zoneRevenue, kitchenTop } from './admin-sim.js';

const $ = (s) => document.querySelector(s);
const PIN = '2025';
const LS_LEDGER = 'argus-admin-ledger';
const LS_MAINT = 'argus-admin-maint';
const CATS = ['Игровое время', 'Бар и кухня', 'Турнир', 'Аренда', 'Закупка', 'Зарплата', 'Коммуналка', 'Прочее'];

const state = {
  club: 'all',
  period: 14,
  opType: 'in',
};

const clubsSel = () => (state.club === 'all' ? CLUBS : CLUBS.filter((c) => c.id === state.club));

const readLS = (key, fallback) => {
  try { return JSON.parse(localStorage.getItem(key)) ?? fallback; } catch { return fallback; }
};
const writeLS = (key, val) => {
  try { localStorage.setItem(key, JSON.stringify(val)); } catch { /* приватный режим — молча */ }
};

// ---------- PIN ----------
function initGate() {
  const gate = $('#pin-gate');
  const app = $('#admin-app');
  const input = $('#pin-input');
  const open = () => {
    gate.hidden = true;
    app.hidden = false;
    renderAll();
    setInterval(tick, 60_000);
  };
  if (sessionStorage.getItem('argus-admin') === '1') {
    open();
    return;
  }
  $('#pin-form').addEventListener('submit', (e) => {
    e.preventDefault();
    if (input.value === PIN) {
      sessionStorage.setItem('argus-admin', '1');
      open();
    } else {
      input.classList.remove('is-error');
      void input.offsetWidth; // перезапуск анимации shake
      input.classList.add('is-error');
      input.value = '';
      input.focus();
    }
  });
  input.focus();
  $('#logout').addEventListener('click', () => {
    sessionStorage.removeItem('argus-admin');
    location.reload();
  });
}

// ---------- Тултип графиков ----------
const tip = () => $('#chart-tip');
function bindTips(container) {
  container.addEventListener('mousemove', (e) => {
    const t = e.target.closest('[data-tip]');
    if (!t) { tip().hidden = true; return; }
    tip().innerHTML = t.dataset.tip;
    tip().hidden = false;
    tip().style.left = `${e.clientX}px`;
    tip().style.top = `${e.clientY}px`;
  });
  container.addEventListener('mouseleave', () => { tip().hidden = true; });
}

// ---------- KPI ----------
function renderKpis() {
  const now = new Date();
  const s = daySummary(clubsSel(), now);
  let busy = 0;
  let total = 0;
  for (const c of clubsSel()) {
    const snap = snapshot(c, now);
    busy += snap.busy;
    total += snap.total;
  }
  const loadPct = Math.round((busy / total) * 100);
  $('#kpi-grid').innerHTML = `
    <div class="kpi k-accent">
      <span class="k-label">Выручка сегодня</span>
      <span class="k-num">${formatRub(s.revenue)}</span>
      <span class="k-sub">оценка на полный день · демо</span>
    </div>
    <div class="kpi">
      <span class="k-label">Загрузка сейчас</span>
      <span class="k-num">${loadPct}%</span>
      <span class="k-sub">${busy} из ${total} ${plural(total, 'места', 'мест', 'мест')} занято</span>
    </div>
    <div class="kpi">
      <span class="k-label">Чеки за день</span>
      <span class="k-num">${s.checks}</span>
      <span class="k-sub">игровые сессии · демо</span>
    </div>
    <div class="kpi">
      <span class="k-label">Средний чек</span>
      <span class="k-num">${formatRub(s.avgCheck)}</span>
      <span class="k-sub">выручка ÷ чеки</span>
    </div>`;
}

// ---------- Выручка по дням (SVG-бары) ----------
function renderRevChart() {
  const series = revenueSeries(clubsSel(), new Date(), state.period);
  const W = 640;
  const H = 230;
  const padL = 8;
  const padR = 8;
  const padT = 26;
  const padB = 26;
  const innerW = W - padL - padR;
  const innerH = H - padT - padB;
  const max = Math.max(...series.map((d) => d.revenue));
  const gap = 2;
  const bw = Math.max(6, Math.floor(innerW / series.length) - gap);
  const maxIdx = series.findIndex((d) => d.revenue === max);
  const fmtDay = (d) => `${String(d.getDate()).padStart(2, '0')}.${String(d.getMonth() + 1).padStart(2, '0')}`;
  const kFmt = (v) => (v >= 1000 ? `${Math.round(v / 1000)}к` : String(v));

  const bars = series.map((d, i) => {
    const h = Math.max(3, Math.round((d.revenue / max) * innerH));
    const x = padL + i * (bw + gap);
    const y = padT + innerH - h;
    const isToday = i === series.length - 1;
    const label = (isToday || i === maxIdx)
      ? `<text class="bar-label" x="${x + bw / 2}" y="${y - 6}" text-anchor="middle">${kFmt(d.revenue)}</text>` : '';
    const tipHtml = `<b>${formatRub(d.revenue)}</b><br><span class="t-sub">${fmtDay(d.date)} · ${d.checks} ${plural(d.checks, 'чек', 'чека', 'чеков')} · кухня ${formatRub(d.kitchen)}</span>`;
    return `${label}<rect class="bar${isToday ? ' is-today' : ''}" x="${x}" y="${y}" width="${bw}" height="${h}" rx="2"
      data-tip="${tipHtml.replaceAll('"', '&quot;')}"><title>${fmtDay(d.date)}: ${formatRub(d.revenue)}</title></rect>`;
  }).join('');

  const grid = [0.5, 1].map((f) => {
    const y = padT + innerH - innerH * f;
    // подпись только у средней линии — у верхней она сталкивается с подписью max-бара
    const label = f === 0.5 ? `<text class="axis-label" x="${padL}" y="${y - 4}">${kFmt(max * f)}</text>` : '';
    return `<line class="gridline" x1="${padL}" y1="${y}" x2="${W - padR}" y2="${y}"></line>${label}`;
  }).join('');

  const xLabels = `
    <text class="axis-label" x="${padL}" y="${H - 8}">${fmtDay(series[0].date)}</text>
    <text class="axis-label" x="${W - padR}" y="${H - 8}" text-anchor="end">сегодня</text>`;

  $('#rev-chart').innerHTML = `<svg class="chart-svg" viewBox="0 0 ${W} ${H}" role="img" aria-label="Выручка по дням">${grid}${bars}${xLabels}</svg>`;
  const totalRev = series.reduce((a, d) => a + d.revenue, 0);
  $('#rev-sub').textContent = `${state.period} дн. · всего ${formatRub(totalRev)} · демо`;
  bindTips($('#rev-chart'));
}

// ---------- Загрузка по часам ----------
function renderHoursChart() {
  const now = new Date();
  const load = hourlyLoad(now);
  const W = 640;
  const H = 170;
  const padL = 8;
  const padR = 8;
  const padT = 14;
  const padB = 26;
  const innerW = W - padL - padR;
  const innerH = H - padT - padB;
  const gap = 3;
  const bw = Math.floor(innerW / 24) - gap;
  const bars = load.map((v, h) => {
    const bh = Math.max(3, Math.round(v * innerH));
    const x = padL + h * (bw + gap);
    const y = padT + innerH - bh;
    const isNow = h === now.getHours();
    return `<rect class="bar${isNow ? ' is-today' : ''}" x="${x}" y="${y}" width="${bw}" height="${bh}" rx="2"
      data-tip="<b>${Math.round(v * 100)}%</b><br><span class='t-sub'>${String(h).padStart(2, '0')}:00${isNow ? ' · сейчас' : ''}</span>"><title>${String(h).padStart(2, '0')}:00 — ${Math.round(v * 100)}%</title></rect>`;
  }).join('');
  const labels = [0, 6, 12, 18, 23].map((h) =>
    `<text class="axis-label" x="${padL + h * (bw + gap) + bw / 2}" y="${H - 8}" text-anchor="middle">${String(h).padStart(2, '0')}</text>`).join('');
  $('#hours-chart').innerHTML = `<svg class="chart-svg" viewBox="0 0 ${W} ${H}" role="img" aria-label="Загрузка по часам">${bars}${labels}</svg>`;
  bindTips($('#hours-chart'));
}

// ---------- Горизонтальные бары ----------
function hbars(items) {
  const max = Math.max(...items.map((i) => i.value));
  return items.map((i) => `
    <div class="hbar">
      <span class="hb-name" title="${i.name}">${i.name}</span>
      <span class="hb-track"><span class="hb-fill" style="width:${Math.max(2, Math.round((i.value / max) * 100))}%"></span></span>
      <span class="hb-val">${formatRub(i.value)}${i.sub ? ` <span class="hb-sub">${i.sub}</span>` : ''}</span>
    </div>`).join('');
}

function renderZoneBars() {
  const data = zoneRevenue(clubsSel(), new Date(), state.period);
  const total = Object.values(data).reduce((a, v) => a + v, 0);
  const items = Object.entries(data)
    .sort((a, b) => b[1] - a[1])
    .map(([z, v]) => ({ name: ZONES[z].name, value: v, sub: `${Math.round((v / total) * 100)}%` }));
  $('#zones-bars').innerHTML = hbars(items);
  $('#zones-sub').textContent = `игровое время · ${state.period} дн. · демо`;
}

function renderKitchenBars() {
  const items = kitchenTop(new Date(), state.period)
    .sort((a, b) => b.sum - a.sum)
    .map((i) => ({ name: i.name, value: i.sum, sub: `× ${i.qty}` }));
  $('#kitchen-bars').innerHTML = hbars(items);
  $('#kitchen-sub').textContent = `${state.period} дн. · демо`;
}

// ---------- Компы ----------
const maintSet = () => new Set(readLS(LS_MAINT, []));

function renderSeats() {
  const now = new Date();
  const maint = maintSet();
  $('#seats-board').innerHTML = clubsSel().map((c) => {
    const snap = snapshot(c, now);
    const cells = seatsOf(c).map((seat) => {
      const key = `${c.id}|${seat.id}`;
      const isMaint = maint.has(key);
      const busy = snap.seats.get(seat.id)?.busy;
      const cls = isMaint ? 'st-maint' : busy ? 'st-busy' : 'st-free';
      const label = isMaint ? 'сервис' : busy ? 'занято' : 'своб.';
      return `<button type="button" class="seat-cell ${cls}" data-key="${key}" title="${seat.id} · ${ZONES[seat.zone].name}">${seat.id}<small>${label}</small></button>`;
    }).join('');
    return `<div class="seats-club"><h4>${c.name} · ${c.label}</h4><div class="seats-grid">${cells}</div></div>`;
  }).join('');
}

function initSeatsBoard() {
  $('#seats-board').addEventListener('click', (e) => {
    const cell = e.target.closest('.seat-cell');
    if (!cell) return;
    const set = maintSet();
    if (set.has(cell.dataset.key)) set.delete(cell.dataset.key);
    else set.add(cell.dataset.key);
    writeLS(LS_MAINT, [...set]);
    renderSeats();
  });
}

// ---------- Журнал ----------
const ops = () => readLS(LS_LEDGER, []);

function renderLedger() {
  const list = ops();
  const cutoff = Date.now() - state.period * 86400_000;
  const inPeriod = list.filter((o) => o.ts >= cutoff);
  const sumIn = inPeriod.filter((o) => o.type === 'in').reduce((a, o) => a + o.sum, 0);
  const sumOut = inPeriod.filter((o) => o.type === 'out').reduce((a, o) => a + o.sum, 0);
  const signed = (v, sign) => (v ? `${sign}${formatRub(v)}` : formatRub(0));
  const net = sumIn - sumOut;
  $('#ledger-totals').innerHTML = `
    <span class="lt-in">Доходы (${state.period} дн.): <b>${signed(sumIn, '+')}</b></span>
    <span class="lt-out">Расходы: <b>${signed(sumOut, '−')}</b></span>
    <span class="lt-net">Сальдо: <b>${net ? (net > 0 ? '+' : '−') + formatRub(Math.abs(net)) : formatRub(0)}</b></span>`;

  if (!list.length) {
    $('#ledger-table').innerHTML = '<p class="ledger-empty">Операций пока нет. Добавьте первую — форма выше. Записи сохраняются в этом браузере.</p>';
    return;
  }
  const fmt = (ts) => {
    const d = new Date(ts);
    return `${String(d.getDate()).padStart(2, '0')}.${String(d.getMonth() + 1).padStart(2, '0')} ${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
  };
  const rows = list.slice(0, 60).map((o) => `
    <tr class="op-${o.type}">
      <td>${fmt(o.ts)}</td>
      <td>${o.type === 'in' ? '+ Доход' : '− Расход'}</td>
      <td class="lt-cat">${o.cat}</td>
      <td class="lt-sum">${o.type === 'in' ? '+' : '−'}${formatRub(o.sum)}</td>
      <td class="lt-note">${o.note || ''}</td>
      <td><button type="button" class="lt-del" data-id="${o.id}" aria-label="Удалить">×</button></td>
    </tr>`).join('');
  $('#ledger-table').innerHTML = `
    <table class="ledger-table">
      <thead><tr><th>Когда</th><th>Тип</th><th>Категория</th><th>Сумма</th><th>Комментарий</th><th></th></tr></thead>
      <tbody>${rows}</tbody>
    </table>`;
}

function initLedger() {
  $('#op-cat').innerHTML = CATS.map((c) => `<option>${c}</option>`).join('');
  const setType = (t) => {
    state.opType = t;
    $('#tt-in').setAttribute('aria-pressed', String(t === 'in'));
    $('#tt-out').setAttribute('aria-pressed', String(t === 'out'));
  };
  $('#tt-in').addEventListener('click', () => setType('in'));
  $('#tt-out').addEventListener('click', () => setType('out'));

  $('#ledger-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const sumEl = $('#op-sum');
    const sum = Number(String(sumEl.value).replace(/[^\d]/g, ''));
    sumEl.classList.remove('is-error');
    if (!sum) {
      sumEl.classList.add('is-error');
      sumEl.focus();
      return;
    }
    const list = ops();
    list.unshift({
      id: `${Date.now().toString(36)}${list.length}`,
      ts: Date.now(),
      type: state.opType,
      cat: $('#op-cat').value,
      sum,
      note: $('#op-note').value.trim().slice(0, 120),
    });
    writeLS(LS_LEDGER, list);
    sumEl.value = '';
    $('#op-note').value = '';
    renderLedger();
  });

  $('#ledger-table').addEventListener('click', (e) => {
    const btn = e.target.closest('.lt-del');
    if (!btn) return;
    writeLS(LS_LEDGER, ops().filter((o) => o.id !== btn.dataset.id));
    renderLedger();
  });

  $('#export-csv').addEventListener('click', () => {
    const rows = [['Дата', 'Тип', 'Категория', 'Сумма', 'Комментарий'],
      ...ops().map((o) => [
        new Date(o.ts).toLocaleString('ru-RU'),
        o.type === 'in' ? 'Доход' : 'Расход',
        o.cat,
        o.type === 'in' ? o.sum : -o.sum,
        (o.note || '').replaceAll(';', ','),
      ])];
    const csv = '﻿' + rows.map((r) => r.join(';')).join('\r\n'); // BOM — чтобы Excel понял UTF-8
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([csv], { type: 'text/csv;charset=utf-8' }));
    a.download = `argus-ledger-${new Date().toISOString().slice(0, 10)}.csv`;
    a.click();
    URL.revokeObjectURL(a.href);
  });
}

// ---------- Сборка ----------
function renderAll() {
  renderKpis();
  renderRevChart();
  renderHoursChart();
  renderZoneBars();
  renderKitchenBars();
  renderSeats();
  renderLedger();
}

function tick() {
  renderKpis();
  renderHoursChart();
  renderSeats();
}

function initFilters() {
  const clubSel = $('#club-filter');
  for (const c of CLUBS) {
    const o = document.createElement('option');
    o.value = c.id;
    o.textContent = `${c.name} · ${c.label}`;
    clubSel.append(o);
  }
  clubSel.addEventListener('change', () => {
    state.club = clubSel.value;
    renderAll();
  });
  $('#period-filter').addEventListener('change', (e) => {
    state.period = Number(e.target.value);
    renderAll();
  });
}

initFilters();
initSeatsBoard();
initLedger();
initGate();
