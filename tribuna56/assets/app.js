// DOM-логика главной: каталог матчей, фильтры, статичные секции, форма.

import { SPORTS, SERVICES, BUNDLE, PORTFOLIO, FAQ, BRAND, sportLabel } from './data.js';
import { filterMatches, groupByDay, displayStatus, STATUS_LABELS, ageGroups } from './catalog.js';
import { formatTime, formatDayLabel, dateKey, vkEmbedUrl, TZ } from './format.js';
import { formatRub, plural, quoteServices } from './pricing.js';
import { SEED_MATCHES, SEED_GENERATED_AT } from './seed-matches.js';
import { initNav, initStickyCta, initThemeToggle, fillBrand, esc, icon } from './ui.js';
import { mountRequestForm } from './request-form.js';

const store = {
  matches: [],
  state: 'loading', // loading | ready | unavailable
  seeded: false, // каталог из вшитого seed (БД не подключена/недоступна)
  filters: { sport: 'all', age: 'all', when: 'all' },
};

const catalogEl = document.getElementById('catalog');
const filtersEl = document.getElementById('filters');

const weekdayFmt = new Intl.DateTimeFormat('en-US', { timeZone: TZ, weekday: 'short' });
const isWeekend = (iso) => ['Sat', 'Sun'].includes(weekdayFmt.format(new Date(iso)));
const todayKey = () => dateKey(new Date().toISOString());

// ---------- Каталог ----------

async function loadMatches() {
  store.state = 'loading';
  renderCatalog();
  try {
    const r = await fetch('/api/matches');
    const data = await r.json();
    if (!data.ok) throw new Error(data.error || 'unavailable');
    store.matches = data.matches || [];
    store.state = 'ready';
  } catch {
    // БД не подключена или недоступна → вшитый каталог реальных матчей
    const cutoff = Date.now() - 4 * 3600_000;
    store.matches = SEED_MATCHES.filter((m) => Date.parse(m.starts_at) >= cutoff);
    store.seeded = true;
    store.state = store.matches.length ? 'ready' : 'unavailable';
  }
  renderFilters();
  renderCatalog();
  renderHeroFacts();
}

function visibleMatches() {
  const { sport, age, when } = store.filters;
  let list = filterMatches(store.matches, { sport, age });
  if (when === 'today') list = list.filter((m) => dateKey(m.starts_at) === todayKey());
  if (when === 'weekend') list = list.filter((m) => isWeekend(m.starts_at));
  return list.filter((m) => displayStatus(m) !== 'finished' || m.stream_url || m.highlights_url);
}

function renderFilters() {
  if (store.state !== 'ready' || !store.matches.length) {
    filtersEl.innerHTML = '';
    return;
  }
  const present = new Set(store.matches.map((m) => m.sport));
  const sports = SPORTS.filter((s) => present.has(s.id));
  filtersEl.innerHTML = `
    <button class="chip${store.filters.sport === 'all' ? ' is-active' : ''}" data-sport="all">Все</button>
    ${sports.map((s) => `
      <button class="chip${store.filters.sport === s.id ? ' is-active' : ''}" data-sport="${s.id}">
        ${icon(`i-${s.id}`)} ${esc(s.label)}
      </button>`).join('')}
    <select class="filter-select" id="filter-age" aria-label="Возраст">
      <option value="all">Все возрасты</option>
      ${ageGroups(store.matches).map((a) =>
        `<option value="${esc(a)}"${store.filters.age === a ? ' selected' : ''}>${esc(a)}</option>`).join('')}
    </select>
    <select class="filter-select" id="filter-when" aria-label="Дата">
      <option value="all">Любой день</option>
      <option value="today"${store.filters.when === 'today' ? ' selected' : ''}>Сегодня</option>
      <option value="weekend"${store.filters.when === 'weekend' ? ' selected' : ''}>Выходные</option>
    </select>`;

  for (const chip of filtersEl.querySelectorAll('.chip')) {
    chip.addEventListener('click', () => {
      store.filters.sport = chip.dataset.sport;
      for (const c of filtersEl.querySelectorAll('.chip')) {
        c.classList.toggle('is-active', c === chip);
      }
      renderCatalog();
    });
  }
  filtersEl.querySelector('#filter-age').addEventListener('change', (e) => {
    store.filters.age = e.target.value;
    renderCatalog();
  });
  filtersEl.querySelector('#filter-when').addEventListener('change', (e) => {
    store.filters.when = e.target.value;
    renderCatalog();
  });
}

function matchCard(m) {
  const st = displayStatus(m);
  const meta = [m.league, m.age_group, m.venue].filter(Boolean).join(' · ');
  const cta = st === 'live'
    ? `<span class="btn btn-sm" data-act="watch">${icon('i-play')} Смотреть эфир</span>`
    : st === 'finished'
      ? `<span class="btn btn-ghost btn-sm" data-act="watch">Запись</span>`
      : st === 'canceled' ? ''
        : `<button class="btn btn-sm" data-act="order" type="button">Заказать съемку</button>`;
  return `
    <div class="match-card" data-id="${m.id}" tabindex="0" role="link"
         aria-label="${esc(m.team_home)} — ${esc(m.team_away)}">
      <div class="match-time"><b>${formatTime(m.starts_at)}</b><span>${esc(sportLabel(m.sport))}</span></div>
      <div class="match-main">
        <div class="match-teams">${icon(`i-${esc(m.sport)}`)}<span>${esc(m.team_home)} — ${esc(m.team_away)}</span></div>
        <div class="match-meta">${esc(meta)}</div>
      </div>
      <div class="match-side">
        <span class="badge badge-${st}">${st === 'live' ? '<span class="live-dot"></span>' : ''}${STATUS_LABELS[st]}</span>
        ${cta}
      </div>
    </div>`;
}

function renderCatalog() {
  if (store.state === 'loading') {
    catalogEl.innerHTML = '<div class="match-list"><div class="skeleton"></div><div class="skeleton"></div><div class="skeleton"></div></div>';
    return;
  }
  if (store.state === 'unavailable') {
    catalogEl.innerHTML = `
      <div class="state-plate">
        <b>Расписание временно недоступно</b>
        Каталог скоро вернется. А съемку можно заказать прямо сейчас —
        нажмите «Моего матча нет в списке» или позвоните: <a href="${esc(BRAND.phoneHref)}">${esc(BRAND.phone)}</a>
      </div>`;
    return;
  }
  const list = visibleMatches();
  if (!list.length) {
    catalogEl.innerHTML = `
      <div class="state-plate">
        <b>${store.matches.length ? 'По выбранным фильтрам матчей нет' : 'Каталог наполняется'}</b>
        ${store.matches.length ? 'Попробуйте сбросить фильтры — или впишите матч вручную ниже.' : 'Впишите свой матч вручную — снимем и его.'}
      </div>`;
    return;
  }
  const now = new Date().toISOString();
  catalogEl.innerHTML = `
    ${store.seeded ? `<p class="calc-note">Расписание сверено вручную ${esc(SEED_GENERATED_AT.split('-').reverse().join('.'))} по данным федераций и лиг. Дату и время подтверждаем при заявке.</p>` : ''}
    ${groupByDay(list).map((g) => `
      <div class="day-group">
        <h3 class="day-title">${esc(formatDayLabel(g.matches[0].starts_at, now))}</h3>
        <div class="match-list">${g.matches.map(matchCard).join('')}</div>
      </div>`).join('')}`;

  for (const card of catalogEl.querySelectorAll('.match-card')) {
    const m = store.matches.find((x) => String(x.id) === card.dataset.id);
    if (!m) continue;
    const go = () => { location.href = `/match/${m.id}`; };
    card.addEventListener('click', go);
    card.addEventListener('keydown', (e) => { if (e.key === 'Enter') go(); });
    card.querySelector('[data-act="order"]')?.addEventListener('click', (e) => {
      e.stopPropagation();
      selectMatch(m);
    });
  }
}

function renderHeroFacts() {
  const factEl = document.getElementById('fact-matches');
  if (factEl && store.state === 'ready') {
    const n = store.matches.length;
    factEl.textContent = `${n} ${plural(n, 'матч', 'матча', 'матчей')}`;
  } else if (factEl) {
    factEl.textContent = '4 вида спорта';
  }
  const live = store.matches.find((m) => displayStatus(m) === 'live');
  const liveEl = document.getElementById('hero-live');
  if (live && liveEl) {
    liveEl.innerHTML = `
      <a class="badge badge-live" style="font-size:13px; padding:8px 13px" href="/match/${live.id}">
        <span class="live-dot"></span>
        Сейчас в эфире: ${esc(live.team_home)} — ${esc(live.team_away)}
      </a>`;
    liveEl.hidden = false;
  }
}

// ---------- Статичные секции из data.js ----------

const SERVICE_ICONS = { stream: 'i-cam', highlights: 'i-scissors', personal: 'i-target' };

function renderStatic() {
  document.getElementById('services-grid').innerHTML = SERVICES.map((s) => `
    <div class="service-card">
      ${icon(SERVICE_ICONS[s.id] || 'i-cam')}
      <b>${esc(s.label)}</b>
      <p>${esc(s.desc)}</p>
      <span class="service-price">${formatRub(s.price)} <small>за матч</small></span>
    </div>`).join('');

  const full = quoteServices(BUNDLE.ids);
  document.getElementById('bundle-note').innerHTML =
    `${esc(BUNDLE.label)}: <b>${formatRub(full.total)}</b> вместо ${formatRub(full.subtotal)} — выгода ${formatRub(BUNDLE.discount)}.`;

  document.getElementById('portfolio-grid').innerHTML = PORTFOLIO.map((v) => {
    const src = vkEmbedUrl(v.vkUrl);
    return `
      <figure class="video-card" style="margin:0">
        <div class="frame">
          ${src
            ? `<iframe src="${esc(src)}" allow="autoplay; encrypted-media; fullscreen; picture-in-picture" allowfullscreen loading="lazy" referrerpolicy="no-referrer"></iframe>`
            : `<div class="video-placeholder">${icon('i-play')}<span>Ролик скоро здесь</span></div>`}
        </div>
        <figcaption>${esc(v.title)}</figcaption>
      </figure>`;
  }).join('');

  document.getElementById('faq-list').innerHTML = FAQ.map((f) => `
    <details class="faq-item">
      <summary>${esc(f.q)}</summary>
      <p>${esc(f.a)}</p>
    </details>`).join('');
}

// ---------- Форма ----------

let form = null;

function selectMatch(m) {
  form.setSelection(m);
  document.getElementById('request').scrollIntoView({ behavior: 'smooth' });
}

function init() {
  fillBrand();
  initNav();
  initThemeToggle();
  initStickyCta();
  renderStatic();
  form = mountRequestForm(document.getElementById('request-form'), {
    onChangeRequest: () => document.getElementById('matches').scrollIntoView({ behavior: 'smooth' }),
  });
  document.getElementById('btn-custom').addEventListener('click', () => {
    form.setSelection(null, { custom: true });
    document.getElementById('request').scrollIntoView({ behavior: 'smooth' });
  });
  loadMatches();
}

init();
