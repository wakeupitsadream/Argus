// ARGUS demo — DOM-слой. Данные: data.js, формулы: pricing.js, симуляция: occupancy.js.
import { BRAND, ZONES, PRICING, CLUBS, PROMOS, MENU, RULES } from './data.js';
import {
  quote, advice, shiftAdvice, formatRub, plural, formatHourRange, rateWindow,
} from './pricing.js';
import { snapshot, totalFree } from './occupancy.js';

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
  priceWindowTouched: false,
};

const club = () => CLUBS.find((c) => c.id === state.clubId);

// ---------- Геометрия схемы зала ----------
const SEAT_W = 44;
const SEAT_H = 30;
const SEAT_SKEW = 6;

const seatPoints = (x, y) =>
  `${x + SEAT_SKEW},${y} ${x + SEAT_W},${y} ${x + SEAT_W - SEAT_SKEW},${y + SEAT_H} ${x},${y + SEAT_H}`;

const svgEl = (tag, attrs = {}) => {
  const el = document.createElementNS('http://www.w3.org/2000/svg', tag);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
  return el;
};

const seatNodes = new Map(); // seatId → { g, zone }
const zoneCountNodes = new Map(); // zone → text node счётчика

function buildHall(c) {
  seatNodes.clear();
  zoneCountNodes.clear();
  const svg = svgEl('svg', {
    viewBox: `0 0 ${c.plan.w} ${c.plan.h}`,
    role: 'group',
    'aria-label': `Схема зала: ${c.name}`,
  });

  // ресепшен и вход — декор для читаемости схемы
  const rec = svgEl('rect', { x: 30, y: 44, width: 118, height: 44, class: 'zone-box' });
  const recT = svgEl('text', { x: 89, y: 70, 'text-anchor': 'middle', class: 'deco-label' });
  recT.textContent = 'РЕСЕПШЕН';
  svg.append(rec, recT);
  const ent = svgEl('text', { x: c.plan.w - 24, y: c.plan.h - 14, 'text-anchor': 'end', class: 'deco-label' });
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
      const g = svgEl('g', { class: 'seat', tabindex: '0', role: 'button', 'data-seat': id });
      const body = svgEl('polygon', { points: seatPoints(x + w / 2 - SEAT_W / 2, y + h / 2 - SEAT_H / 2), class: 'seat-body' });
      const txt = svgEl('text', { x: x + w / 2, y: y + h / 2 + 4, 'text-anchor': 'middle', class: 'seat-id' });
      txt.textContent = 'PS5';
      g.append(body, txt);
      svg.append(g);
      seatNodes.set(id, { g, zone: block.zone });
      continue;
    }
    const { cols, x, y, gapX, gapY } = block.grid;
    const rows = Math.ceil(block.count / cols);
    const boxW = (Math.min(cols, block.count) - 1) * gapX + SEAT_W + 24;
    const boxH = (rows - 1) * gapY + SEAT_H + 24;
    svg.append(svgEl('rect', { x: x - 12, y: y - 12, width: boxW, height: boxH, rx: 6, class: 'zone-box' }));
    const label = svgEl('text', { x: x - 12, y: y - 34, class: 'zone-label' });
    label.textContent = zone.name;
    svg.append(label);
    const count = svgEl('text', { x: x - 12, y: y - 20, class: 'zone-count', 'data-zone-count': block.zone });
    svg.append(count);
    zoneCountNodes.set(block.zone, count);

    for (let i = 0; i < block.count; i++) {
      const sx = x + (i % cols) * gapX;
      const sy = y + Math.floor(i / cols) * gapY;
      const id = `${block.prefix}${String(i + 1).padStart(2, '0')}`;
      const g = svgEl('g', { class: 'seat', tabindex: '0', role: 'button', 'data-seat': id });
      g.append(
        svgEl('polygon', { points: seatPoints(sx, sy), class: 'seat-body' }),
        Object.assign(svgEl('text', { x: sx + SEAT_W / 2 - 2, y: sy + SEAT_H / 2 + 4, 'text-anchor': 'middle', class: 'seat-id' }), { textContent: id }),
      );
      svg.append(g);
      seatNodes.set(id, { g, zone: block.zone });
    }
  }

  const hall = $('#hall');
  hall.replaceChildren(svg);
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
  const label = state.seatId
    ? `Место <b>${state.seatId}</b> · зона ${ZONES[state.zoneId].name}`
    : 'Выберите место на схеме или зону ниже';
  $('#seat-pick').innerHTML = label;
}

function updateHall(snap) {
  for (const [id, { g }] of seatNodes) {
    const s = snap.seats.get(id);
    if (!s) continue;
    const wasBusy = g.classList.contains('is-busy');
    if (wasBusy !== s.busy) {
      g.classList.toggle('is-busy', s.busy);
      const zoneName = ZONES[seatNodes.get(id).zone].name;
      g.setAttribute('aria-label', `Место ${id}, ${zoneName}, ${s.busy ? 'занято' : 'свободно'}`);
      if (s.busy) g.setAttribute('aria-disabled', 'true');
      else g.removeAttribute('aria-disabled');
    } else if (!g.hasAttribute('aria-label')) {
      const zoneName = ZONES[seatNodes.get(id).zone].name;
      g.setAttribute('aria-label', `Место ${id}, ${zoneName}, ${s.busy ? 'занято' : 'свободно'}`);
      if (s.busy) g.setAttribute('aria-disabled', 'true');
    }
  }
  for (const [zone, node] of zoneCountNodes) {
    const z = snap.byZone[zone];
    if (z) node.textContent = `свободно ${z.free} из ${z.total}`;
  }
  // выбранное место «заняли» — демо живое
  if (state.seatId && snap.seats.get(state.seatId)?.busy) {
    toast(`Место ${state.seatId} только что заняли — выберите другое`);
    state.seatId = null;
    syncSelection();
    renderQuote();
  }
}

let toastTimer = null;
function toast(text) {
  let el = $('.toast');
  if (!el) {
    el = document.createElement('div');
    el.className = 'toast';
    document.body.append(el);
  }
  el.textContent = text;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.remove(), 4200);
}

// ---------- Табы клубов ----------
function renderClubTabs() {
  const wrap = $('#club-tabs');
  wrap.replaceChildren(
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
  const box = $('#quote-box');
  const adviceBox = $('#advice');
  const q = quote(PRICING, state.zoneId, state.startHour, state.hours);

  if (q.ps5) {
    box.innerHTML = '<p class="quote-ps5">Тариф PS5-комнаты в прайсе не указан — стоимость уточнит администратор. Забронируйте комнату через LANGAME или оставьте заявку.</p>';
    adviceBox.textContent = '';
    return;
  }

  const rows = q.options.slice(0, 4).map((o, i) => `
    <div class="quote-option${i === 0 ? ' is-best' : ''}">
      <span class="q-label">${o.label}${i === 0 ? ' <span class="q-best-tag">выгодно</span>' : ''}<small>${o.detail}</small></span>
      <span class="q-price">${formatRub(o.total)}</span>
    </div>`).join('');
  box.innerHTML = `
    <div class="quote-meta" style="color:var(--muted);font-size:13px;margin-bottom:10px">
      ${ZONES[state.zoneId].name} · ${formatHourRange(state.startHour, state.hours)} · ${state.hours} ${plural(state.hours, 'час', 'часа', 'часов')}
    </div>${rows}`;

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

  // ввод зеркалится в store — форма переживает любые перерисовки
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

// ---------- Зоны и железо ----------
function renderZones() {
  $('#zones-grid').innerHTML = Object.values(ZONES).map((z) => `
    <article class="zone-card">
      <h3>${z.name}</h3>
      <p class="z-tag">${z.tagline}</p>
      ${z.specs.length
        ? `<dl>${z.specs.map(([k, v]) => `<div><dt>${k}</dt><dd>${v}</dd></div>`).join('')}</dl>`
        : ''}
      ${z.note ? `<p class="z-note">${z.note}</p>` : ''}
    </article>`).join('');
}

// ---------- Прайс ----------
function priceTable(rows) {
  const zoneCols = ['comfort', 'vip', 'stream'];
  const head = `<thead><tr><th>Тариф</th>${zoneCols.map((z) => `<th>${ZONES[z].name}</th>`).join('')}</tr></thead>`;
  const body = rows.map((r) => `
    <tr${r.pkg ? ' class="is-package"' : ''}>
      <td>${r.name}${r.note ? ` <span class="row-note">${r.note}</span>` : ''}</td>
      ${zoneCols.map((z) => `<td class="p">${formatRub(r.prices[z])}</td>`).join('')}
    </tr>`).join('');
  return `<div class="price-table-wrap"><table class="price-table">${head}<tbody>${body}</tbody></table></div>`;
}

function renderPrices() {
  $('#price-day').innerHTML = priceTable([
    { name: '1 час', prices: PRICING.hourly.day.prices },
    { name: 'Пакет «День»', note: '10:00–18:00', prices: PRICING.packages.find((p) => p.id === 'day').prices, pkg: true },
  ]);
  $('#price-night').innerHTML = priceTable([
    { name: '1 час', prices: PRICING.hourly.night.prices },
    ...PRICING.nightBundles.map((b) => ({ name: b.name[0].toUpperCase() + b.name.slice(1), prices: b.prices })),
    ...PRICING.packages.filter((p) => p.id !== 'day').map((p) => ({
      name: `Пакет «${p.name}»`,
      note: `${p.from}:00–${p.to}:00`,
      prices: p.prices,
      pkg: true,
    })),
  ]);

  const setWindow = (win, touched) => {
    if (touched) state.priceWindowTouched = true;
    $('#pt-day').setAttribute('aria-pressed', String(win === 'day'));
    $('#pt-night').setAttribute('aria-pressed', String(win === 'night'));
    $('#price-day').hidden = win !== 'day';
    $('#price-night').hidden = win !== 'night';
  };
  $('#pt-day').addEventListener('click', () => setWindow('day', true));
  $('#pt-night').addEventListener('click', () => setWindow('night', true));
  setWindow(rateWindow(new Date().getHours()), false);
}

// ---------- Акции ----------
function renderPromos() {
  const cards = PROMOS.map((p) => {
    const prices = p.dayPrice
      ? `<div class="promo-prices">
           <span class="pp-day"><b>${formatRub(p.dayPrice)}</b><span>6:00–18:00</span></span>
           <span class="pp-night"><b>${formatRub(p.nightPrice)}</b><span>18:00–6:00</span></span>
         </div>`
      : '';
    const apps = p.id === 'langame'
      ? `<div class="apps">
           <a href="${BRAND.langameApps.rustore}" target="_blank" rel="noopener">RuStore</a>
           <a href="${BRAND.langameApps.appstore}" target="_blank" rel="noopener">App Store</a>
         </div>`
      : '';
    return `<article class="promo-card">
      <h3>${p.name}</h3>
      ${p.size ? `<p class="promo-size">${p.size}</p>` : ''}
      ${prices}
      <p>${p.text}</p>
      ${apps}
    </article>`;
  });
  $('#promos-grid').innerHTML = cards.join('');
}

// ---------- Кухня ----------
function renderMenu() {
  $('#menu-note').textContent = MENU.disclaimer;
  $('#menu-grid').innerHTML = MENU.sections.map((s) => `
    <div class="menu-col">
      <h3>${s.name}</h3>
      ${s.items.map((it) => `
        <div class="menu-item">
          <span class="m-name">${it.name}${it.note ? `<span class="m-note">${it.note}</span>` : ''}${it.sample ? '<span class="sample-chip">пример</span>' : ''}</span>
          <span class="m-price">${it.dayPrice
            ? `${formatRub(it.dayPrice)} <small>день</small> · ${formatRub(it.nightPrice)} <small>ночь</small>`
            : formatRub(it.price)}</span>
        </div>`).join('')}
    </div>`).join('');
}

// ---------- Клубы ----------
function renderClubCards() {
  $('#club-cards').innerHTML = CLUBS.map((c) => {
    const gis = `https://2gis.ru/orenburg/search/${encodeURIComponent(c.address)}`;
    const ya = `https://yandex.ru/maps/?text=${encodeURIComponent(c.address)}`;
    return `<article class="club-card">
      <h3>${c.name}</h3>
      <p class="c-addr">${c.address}</p>
      <div class="c-meta">
        <span class="open-dot">Открыто · круглосуточно</span>
        <span>${c.pcTotal} ПК${c.hasPs5Room ? ' + PS5-комната' : ''}</span>
      </div>
      <div class="c-links">
        <a class="link-btn primary" href="${c.langameUrl}" target="_blank" rel="noopener">Бронь в LANGAME</a>
        <a class="link-btn" href="${BRAND.vk}" target="_blank" rel="noopener">ВКонтакте</a>
        <a class="link-btn" href="${gis}" target="_blank" rel="noopener">Маршрут 2ГИС</a>
        <a class="link-btn" href="${ya}" target="_blank" rel="noopener">Яндекс Карты</a>
      </div>
      <figure class="photo-slot" data-slot="${c.id}">Фото клуба — скоро</figure>
    </article>`;
  }).join('');
}

// ---------- Карта ----------
let mapStarted = false;
function initMap() {
  if (mapStarted || typeof window.L === 'undefined') return;
  mapStarted = true;
  const L = window.L;
  const map = L.map('map', { scrollWheelZoom: false, zoomControl: true });
  L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> &copy; <a href="https://carto.com/attributions">CARTO</a>',
    subdomains: 'abcd',
    maxZoom: 19,
  }).addTo(map);
  map.attributionControl.setPrefix('<a href="https://leafletjs.com">Leaflet</a>');
  const bounds = [];
  for (const c of CLUBS) {
    const icon = L.divIcon({ className: '', html: '<span class="argus-pin">A</span>', iconSize: [34, 26], iconAnchor: [17, 13] });
    L.marker([c.lat, c.lon], { icon })
      .addTo(map)
      .bindPopup(`<b>${c.name}</b><br>${c.address}<br><a href="${c.langameUrl}" target="_blank" rel="noopener">Забронировать</a>`);
    bounds.push([c.lat, c.lon]);
  }
  map.fitBounds(bounds, { padding: [46, 46], maxZoom: 13 });
  map.on('click', () => map.scrollWheelZoom.enable());
  map.on('mouseout', () => map.scrollWheelZoom.disable());
}

// ---------- Правила ----------
function renderRules() {
  $('#rules-list').innerHTML = `
    <details>
      <summary>В клубе запрещено — ${RULES.length} ${plural(RULES.length, 'пункт', 'пункта', 'пунктов')}</summary>
      <p class="rules-intro">Правила действуют в обоих клубах сети. Полная версия — на ресепшене.</p>
      <div class="rules-body"><ol>${RULES.map((r) => `<li>${r}</li>`).join('')}</ol></div>
    </details>`;
}

// ---------- Hero и минутный тик ----------
function updateLive() {
  const now = new Date();
  const free = totalFree(CLUBS, now);
  $('#hero-free').textContent = free;
  $('#hero-free-label').innerHTML = `${plural(free, 'место свободно', 'места свободно', 'мест свободно')} сейчас<sup>демо</sup>`;
  updateHall(snapshot(club(), now));
}

// ---------- Липкая мобильная CTA ----------
function initStickyCta() {
  const sticky = $('#sticky-cta');
  let heroVisible = true;
  let bookingVisible = false;
  const apply = () => sticky.classList.toggle('is-visible', !heroVisible && !bookingVisible && !state.showingDone);
  new IntersectionObserver(([e]) => { heroVisible = e.isIntersecting; apply(); }, { threshold: 0.05 })
    .observe($('.hero'));
  new IntersectionObserver(([e]) => { bookingVisible = e.isIntersecting; apply(); }, { threshold: 0.15 })
    .observe($('#booking'));
}

// ---------- Старт ----------
function main() {
  renderClubTabs();
  buildHall(club());
  renderZoneChips();
  renderTimeControls();
  renderQuote();
  renderZones();
  renderPrices();
  renderPromos();
  renderMenu();
  renderClubCards();
  renderRules();
  initForm();
  initStickyCta();
  syncSelection();
  $('#langame-cta').href = club().langameUrl;
  updateLive();
  setInterval(() => {
    // тик трогает только счётчики и классы мест — поля формы не перерисовываются
    updateLive();
  }, 60_000);

  const mapBox = $('.map-box');
  new IntersectionObserver((entries, obs) => {
    if (entries[0].isIntersecting) {
      initMap();
      if (mapStarted) obs.disconnect();
    }
  }, { rootMargin: '200px' }).observe(mapBox);
  // подстраховка: leaflet.js (defer) мог загрузиться позже IO-события
  window.addEventListener('load', () => {
    const r = mapBox.getBoundingClientRect();
    if (r.top < window.innerHeight + 200) initMap();
  });
}

main();
