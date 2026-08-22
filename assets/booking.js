// Страница бронирования: схема залов, калькулятор, заявка.
import { BRAND, ZONES, PRICING, CLUBS } from './data.js';
import {
  quote, advice, shiftAdvice, formatRub, plural, formatHourRange,
} from './pricing.js';
import { snapshot, totalFree } from './occupancy.js';
import { toast } from './ui.js';

const $ = (sel) => document.querySelector(sel);

// ---------- Состояние ----------
const state = {
  clubId: CLUBS[0].id,
  zoneId: 'comfort',
  seatId: null,
  startHour: (new Date().getHours() + 1) % 24,
  hours: 3,
  form: { name: '', phone: '', consent: false },
  showingDone: false,
};

const club = () => CLUBS.find((c) => c.id === state.clubId);

// ---------- Геометрия схемы: место = монитор (экран + ножка + подставка) ----------
const SCREEN_W = 42;
const SCREEN_H = 24;
const SCREEN_SKEW = 5;
const SEAT_H = 33; // экран + ножка + подставка

const screenPoints = (x, y) =>
  `${x + SCREEN_SKEW},${y} ${x + SCREEN_W},${y} ${x + SCREEN_W - SCREEN_SKEW},${y + SCREEN_H} ${x},${y + SCREEN_H}`;

const svgEl = (tag, attrs = {}) => {
  const el = document.createElementNS('http://www.w3.org/2000/svg', tag);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
  return el;
};

const seatNodes = new Map(); // seatId → { g, zone }
const zoneCountNodes = new Map();

function makeSeat(id, x, y, labelText) {
  const g = svgEl('g', { class: 'seat', tabindex: '0', role: 'button', 'data-seat': id });
  const cx = x + SCREEN_W / 2;
  g.append(
    svgEl('polygon', { points: screenPoints(x, y), class: 'seat-screen' }),
    svgEl('line', { x1: cx - 1, y1: y + SCREEN_H, x2: cx - 1, y2: y + SCREEN_H + 5, class: 'seat-stand' }),
    svgEl('line', { x1: cx - 8, y1: y + SEAT_H - 1, x2: cx + 6, y2: y + SEAT_H - 1, class: 'seat-stand' }),
    Object.assign(
      svgEl('text', { x: cx - 1, y: y + SCREEN_H / 2 + 4, 'text-anchor': 'middle', class: 'seat-id' }),
      { textContent: labelText },
    ),
  );
  const title = svgEl('title');
  g.append(title);
  return g;
}

function buildHall(c) {
  seatNodes.clear();
  zoneCountNodes.clear();
  const svg = svgEl('svg', {
    viewBox: `0 0 ${c.plan.w} ${c.plan.h}`,
    role: 'group',
    'aria-label': `Схема зала: ${c.name}`,
  });

  // контур пола, ресепшен и вход — для читаемости схемы
  svg.append(svgEl('rect', { x: 8, y: 8, width: c.plan.w - 16, height: c.plan.h - 16, rx: 10, class: 'hall-floor' }));
  const rec = svgEl('rect', { x: 30, y: 44, width: 118, height: 44, class: 'zone-box' });
  const recT = svgEl('text', { x: 89, y: 70, 'text-anchor': 'middle', class: 'deco-label' });
  recT.textContent = 'РЕСЕПШЕН';
  svg.append(rec, recT);
  const ent = svgEl('text', { x: c.plan.w - 28, y: c.plan.h - 20, 'text-anchor': 'end', class: 'deco-label' });
  ent.textContent = '← ВХОД';
  svg.append(ent);

  for (const block of c.zonesLayout) {
    const zone = ZONES[block.zone];
    if (block.room) {
      const { x, y, w, h } = block.room;
      svg.append(svgEl('rect', { x, y, width: w, height: h, rx: 6, class: 'zone-box' }));
      const label = svgEl('text', { x: x + w / 2, y: y - 10, 'text-anchor': 'middle', class: 'zone-label' });
      label.textContent = zone.name;
      svg.append(label);
      const id = `${block.prefix}01`;
      const g = makeSeat(id, x + w / 2 - SCREEN_W / 2, y + h / 2 - SEAT_H / 2 + 2, 'PS5');
      svg.append(g);
      seatNodes.set(id, { g, zone: block.zone });
      continue;
    }
    const { cols, x, y, gapX, gapY } = block.grid;
    const rows = Math.ceil(block.count / cols);
    const boxW = (Math.min(cols, block.count) - 1) * gapX + SCREEN_W + 24;
    const boxH = (rows - 1) * gapY + SEAT_H + 22;
    svg.append(svgEl('rect', { x: x - 12, y: y - 12, width: boxW, height: boxH, rx: 6, class: 'zone-box' }));
    const label = svgEl('text', { x: x - 12, y: y - 36, class: 'zone-label' });
    label.textContent = zone.name;
    svg.append(label);
    const count = svgEl('text', { x: x - 12, y: y - 21, class: 'zone-count', 'data-zone-count': block.zone });
    svg.append(count);
    zoneCountNodes.set(block.zone, count);

    for (let i = 0; i < block.count; i++) {
      const sx = x + (i % cols) * gapX;
      const sy = y + Math.floor(i / cols) * gapY;
      const id = `${block.prefix}${String(i + 1).padStart(2, '0')}`;
      const g = makeSeat(id, sx, sy, id);
      svg.append(g);
      seatNodes.set(id, { g, zone: block.zone });
    }
  }

  $('#hall').replaceChildren(svg);
  $('#hall-note').textContent = c.layoutNote;

  svg.addEventListener('click', onSeatEvent);
  svg.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' || e.key === ' ') {
      onSeatEvent(e);
      e.preventDefault();
    }
  });
}

function onSeatEvent(e) {
  const g = e.target.closest('.seat');
  if (!g) return;
  const id = g.dataset.seat;
  const info = seatNodes.get(id);
  if (!info || g.classList.contains('is-busy')) return;
  if (state.seatId === id) {
    state.seatId = null;
  } else {
    state.seatId = id;
    state.zoneId = info.zone;
  }
  syncSelection();
  renderZoneChips();
  renderQuote();
}

function syncSelection() {
  for (const [id, { g }] of seatNodes) {
    g.classList.toggle('is-selected', id === state.seatId);
  }
  $('#seat-pick').innerHTML = state.seatId
    ? `Выбрано: <b>${state.seatId}</b> · зона ${ZONES[state.zoneId].name}`
    : 'Кликните по месту на схеме — или просто выберите зону';
}

function updateHall(snap) {
  for (const [id, { g, zone }] of seatNodes) {
    const s = snap.seats.get(id);
    if (!s) continue;
    g.classList.toggle('is-busy', s.busy);
    const zoneName = ZONES[zone].name;
    const stateText = s.busy ? 'занято' : 'свободно';
    g.setAttribute('aria-label', `Место ${id}, ${zoneName}, ${stateText}`);
    if (s.busy) g.setAttribute('aria-disabled', 'true');
    else g.removeAttribute('aria-disabled');
    const title = g.querySelector('title');
    if (title) title.textContent = `${id} · ${zoneName} · ${stateText}`;
  }
  for (const [zoneId, node] of zoneCountNodes) {
    const z = snap.byZone[zoneId];
    if (!z) continue;
    node.textContent = `свободно ${z.free} из ${z.total}`;
    node.classList.toggle('low', z.free <= Math.max(1, Math.round(z.total * 0.15)));
  }
  if (state.seatId && snap.seats.get(state.seatId)?.busy) {
    toast(`Место ${state.seatId} только что заняли — выберите другое`);
    state.seatId = null;
    syncSelection();
    renderQuote();
  }
}

// ---------- Табы клубов ----------
function renderClubTabs() {
  $('#club-tabs').replaceChildren(
    ...CLUBS.map((c) => {
      const b = document.createElement('button');
      b.className = 'club-tab';
      b.type = 'button';
      b.setAttribute('role', 'tab');
      b.setAttribute('aria-selected', String(c.id === state.clubId));
      b.innerHTML = `<span class="t-name">${c.name}</span><span class="t-sub">${c.label} · ${c.pcTotal} ПК${c.hasPs5Room ? ' + PS5' : ''}</span>`;
      b.addEventListener('click', () => switchClub(c.id));
      return b;
    }),
  );
}

function switchClub(id) {
  if (state.clubId === id) return;
  state.clubId = id;
  state.seatId = null;
  const zones = new Set(club().zonesLayout.map((b) => b.zone));
  if (!zones.has(state.zoneId)) state.zoneId = 'comfort';
  renderClubTabs();
  buildHall(club());
  updateHall(snapshot(club(), new Date()));
  syncSelection();
  renderZoneChips();
  renderQuote();
  $('#langame-cta').href = club().langameUrl;
}

// ---------- Панель расчёта ----------
function renderZoneChips() {
  const wrap = $('#zone-chips');
  const zones = club().zonesLayout.map((b) => b.zone);
  wrap.replaceChildren(
    ...zones.map((z) => {
      const b = document.createElement('button');
      b.className = 'chip';
      b.type = 'button';
      b.textContent = ZONES[z].name;
      b.setAttribute('aria-pressed', String(z === state.zoneId));
      b.addEventListener('click', () => {
        state.zoneId = z;
        if (state.seatId && seatNodes.get(state.seatId)?.zone !== z) {
          state.seatId = null;
          syncSelection();
        }
        renderZoneChips();
        renderQuote();
      });
      return b;
    }),
  );
}

function renderTimeControls() {
  const sel = $('#start-hour');
  sel.replaceChildren(
    ...Array.from({ length: 24 }, (_, h) => {
      const o = document.createElement('option');
      o.value = h;
      o.textContent = `${String(h).padStart(2, '0')}:00`;
      return o;
    }),
  );
  sel.value = state.startHour;
  sel.addEventListener('change', () => {
    state.startHour = Number(sel.value);
    renderQuote();
  });

  const chips = $('#hours-chips');
  const variants = [1, 2, 3, 5, 7, 12];
  chips.replaceChildren(
    ...variants.map((h) => {
      const b = document.createElement('button');
      b.className = 'chip';
      b.type = 'button';
      b.dataset.hours = h;
      b.textContent = `${h} ч`;
      b.setAttribute('aria-pressed', String(h === state.hours));
      b.addEventListener('click', () => {
        state.hours = h;
        chips.querySelectorAll('.chip').forEach((c) =>
          c.setAttribute('aria-pressed', String(Number(c.dataset.hours) === h)));
        renderQuote();
      });
      return b;
    }),
  );
}

function renderQuote() {
  const totalBox = $('#quote-total');
  const box = $('#quote-box');
  const adviceBox = $('#advice');
  const q = quote(PRICING, state.zoneId, state.startHour, state.hours);

  if (q.ps5) {
    totalBox.innerHTML = '';
    box.innerHTML = '<p class="quote-ps5">Тариф PS5-комнаты в прайсе не указан — стоимость уточнит администратор. Забронируйте комнату через LANGAME или оставьте заявку.</p>';
    adviceBox.textContent = '';
    return;
  }

  totalBox.innerHTML = `
    <div>
      <span class="qt-label">${ZONES[state.zoneId].name} · ${formatHourRange(state.startHour, state.hours)} · ${state.hours} ${plural(state.hours, 'час', 'часа', 'часов')}</span>
      <div class="qt-sub">${q.best.label}</div>
    </div>
    <span class="qt-sum">${formatRub(q.best.total)}</span>`;

  box.innerHTML = q.options.slice(0, 3).map((o, i) => `
    <div class="quote-option${i === 0 ? ' is-best' : ''}">
      <span class="q-label">${o.label}${i === 0 ? ' <span class="q-best-tag">выгодно</span>' : ''}<small>${o.detail}</small></span>
      <span class="q-price">${formatRub(o.total)}</span>
    </div>`).join('');

  const main = advice(q);
  const hint = shiftAdvice(PRICING, state.zoneId, state.startHour, state.hours);
  adviceBox.innerHTML = `${main ? `⚡ ${main}` : ''}${hint ? `<span class="hint">💡 ${hint}</span>` : ''}`;
}

// ---------- Форма заявки ----------
function initForm() {
  const form = $('#order-form');
  const nameEl = $('#f-name');
  const phoneEl = $('#f-phone');
  const consentEl = $('#f-consent');

  $('#open-form').addEventListener('click', () => {
    form.hidden = !form.hidden;
    if (!form.hidden) nameEl.focus();
  });

  nameEl.addEventListener('input', () => { state.form.name = nameEl.value; });
  phoneEl.addEventListener('input', () => { state.form.phone = phoneEl.value; });
  consentEl.addEventListener('change', () => { state.form.consent = consentEl.checked; });

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    let valid = true;
    const name = nameEl.value.trim();
    const digits = phoneEl.value.replace(/\D/g, '');
    $('#e-name').textContent = '';
    $('#e-phone').textContent = '';
    nameEl.classList.remove('is-error');
    phoneEl.classList.remove('is-error');
    $('#consent-row').classList.remove('is-error');
    if (name.length < 2) {
      $('#e-name').textContent = 'Как к вам обращаться?';
      nameEl.classList.add('is-error');
      valid = false;
    }
    if (digits.length < 10) {
      $('#e-phone').textContent = 'Нужен телефон из 10–11 цифр';
      phoneEl.classList.add('is-error');
      valid = false;
    }
    if (!consentEl.checked) {
      $('#consent-row').classList.add('is-error');
      valid = false;
    }
    if (!valid) return;

    const q = quote(PRICING, state.zoneId, state.startHour, state.hours);
    const payload = {
      name,
      phone: phoneEl.value,
      consent: true,
      website: $('#f-website').value,
      club: `${club().name} (${club().label})`,
      seat: state.seatId || 'любое',
      zone: ZONES[state.zoneId].name,
      time: formatHourRange(state.startHour, state.hours),
      price: q.best ? `${q.best.label}: ${formatRub(q.best.total)}` : 'PS5 — уточнить',
    };
    try {
      const res = await fetch('/api/order', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      if (!res.ok) console.warn('[order] api ответил', res.status);
    } catch (err) {
      // демо не ломается: превью без serverless тоже показывает успех
      console.warn('[order] api недоступен:', err && err.message);
    }
    state.showingDone = true;
    form.hidden = true;
    $('#order-done').hidden = false;
  });
}

// ---------- Живые цифры и минутный тик ----------
function updateLive() {
  const now = new Date();
  const freeAll = totalFree(CLUBS, now);
  const line = $('#live-free');
  if (line) line.innerHTML = `Сейчас в сети свободно <b>${freeAll}</b> ${plural(freeAll, 'место', 'места', 'мест')}`;
  updateHall(snapshot(club(), now));
}

// ---------- Старт ----------
export function initBooking() {
  renderClubTabs();
  buildHall(club());
  renderZoneChips();
  renderTimeControls();
  renderQuote();
  initForm();
  syncSelection();
  $('#langame-cta').href = club().langameUrl;
  updateLive();
  // тик трогает только счётчики и классы мест — поля формы не перерисовываются
  setInterval(updateLive, 60_000);
}
