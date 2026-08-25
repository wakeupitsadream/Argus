// DOM-логика админки: гейт по токену, вкладки Матчи / Импорт / Заявки.
// Ошибки API показываются явно — это инструмент владельца.

import { SPORTS, SERVICES, sportLabel } from './data.js';
import { formatMatchDate, TZ } from './format.js';
import { formatRub, plural } from './pricing.js';
import { STATUS_LABELS, displayStatus } from './catalog.js';
import { esc, toast } from './ui.js';

const TOKEN_KEY = 't56_admin_token';
const state = {
  token: sessionStorage.getItem(TOKEN_KEY) || '',
  tab: 'matches',
  matches: [],
  includePast: false,
  queue: [],
  requests: [],
  reqStatus: 'new',
  editing: null, // null | 'new' | match
  editingDraft: null, // несохраненный ввод формы — переживает перерисовки
  lastReport: null,
};

const $ = (id) => document.getElementById(id);
const gate = $('gate');
const app = $('app');
const gateError = $('gate-error');

// ---------- API ----------

class ApiError extends Error {
  constructor(status, code) { super(code || String(status)); this.status = status; this.code = code; }
}

async function api(path, opts = {}) {
  const r = await fetch(path, {
    ...opts,
    headers: {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${state.token}`,
      ...(opts.headers || {}),
    },
  });
  const data = await r.json().catch(() => ({}));
  if (r.status === 401 || r.status === 429 || r.status === 503) {
    throw new ApiError(r.status, data.error);
  }
  if (!r.ok || data.ok !== true) throw new ApiError(r.status, data.error || 'unknown');
  return data;
}

function showGate(message) {
  sessionStorage.removeItem(TOKEN_KEY);
  app.hidden = true;
  gate.hidden = false;
  if (message) {
    gateError.textContent = message;
    gateError.classList.add('is-on');
  }
}

function explainError(e) {
  if (e instanceof ApiError) {
    if (e.status === 401) return 'Неверный токен';
    if (e.status === 429) return 'Слишком много попыток — подождите минуту';
    if (e.status === 503) return 'ADMIN_TOKEN не задан в env Vercel — админка выключена';
    if (e.code === 'db_not_configured') return 'Supabase не настроен (env SUPABASE_URL / SUPABASE_SERVICE_KEY)';
    if (e.code === 'db') return 'База данных недоступна';
    return `Ошибка: ${e.code || e.status}`;
  }
  return 'Сеть недоступна';
}

// ---------- Загрузка данных ----------

async function loadMatches() {
  const data = await api(`/api/admin/matches${state.includePast ? '?include_past=1' : ''}`);
  state.matches = data.matches;
}
async function loadQueue() {
  const data = await api('/api/admin/import');
  state.queue = data.queue;
}
async function loadRequests() {
  const q = state.reqStatus === 'all' ? '' : `?status=${state.reqStatus}`;
  const data = await api(`/api/admin/requests${q}`);
  state.requests = data.requests;
}

async function enter() {
  try {
    await Promise.all([loadMatches(), loadQueue(), loadRequests()]);
  } catch (e) {
    // токен принят, но база лежит — пускаем и показываем ошибку; всё прочее — на гейт
    const dbDown = e instanceof ApiError &&
      (e.status === 502 || e.code === 'db' || e.code === 'db_not_configured');
    if (!dbDown) {
      showGate(explainError(e));
      return;
    }
    toast(explainError(e));
  }
  sessionStorage.setItem(TOKEN_KEY, state.token);
  gate.hidden = true;
  app.hidden = false;
  renderAll();
}

// ---------- Вкладка «Матчи» ----------

// datetime-local всегда показывает и принимает ОРЕНБУРГСКОЕ время (UTC+5,
// без переходов), даже если браузер админа в другой таймзоне.
function toOrenburgInput(iso) {
  const d = iso ? new Date(iso) : new Date();
  const p = new Intl.DateTimeFormat('en-CA', {
    timeZone: TZ, year: 'numeric', month: '2-digit', day: '2-digit',
    hour: '2-digit', minute: '2-digit', hour12: false,
  }).formatToParts(d).reduce((a, x) => ((a[x.type] = x.value), a), {});
  const hour = p.hour === '24' ? '00' : p.hour;
  return `${p.year}-${p.month}-${p.day}T${hour}:${p.minute}`;
}

function orenburgInputToIso(value) {
  return new Date(`${value}:00+05:00`).toISOString();
}

function editingKey() {
  return state.editing === 'new' ? 'new' : state.editing ? String(state.editing.id) : '';
}

function matchForm(m) {
  const isNew = !m || !m.id;
  const d = state.editingDraft || {};
  const val = (name, fb) => esc(d[name] !== undefined ? d[name] : (fb ?? ''));
  const sportSel = d.sport !== undefined ? d.sport : m && m.sport;
  return `
    <form class="admin-form" id="match-form" data-key="${editingKey()}">
      <div class="field">
        <label>Вид спорта</label>
        <select name="sport">${SPORTS.map((s) =>
          `<option value="${s.id}"${sportSel === s.id ? ' selected' : ''}>${esc(s.label)}</option>`).join('')}</select>
      </div>
      <div class="field"><label>Турнир / лига</label><input type="text" name="league" value="${val('league', m?.league)}" placeholder="Первенство области"></div>
      <div class="field"><label>Возраст</label><input type="text" name="age_group" value="${val('age_group', m?.age_group)}" placeholder="2012 г.р."></div>
      <div class="field"><label>Хозяева</label><input type="text" name="team_home" value="${val('team_home', m?.team_home)}" placeholder="Юниор-2012" required></div>
      <div class="field"><label>Гости</label><input type="text" name="team_away" value="${val('team_away', m?.team_away)}" placeholder="Сармат-2012" required></div>
      <div class="field"><label>Начало (по Оренбургу)</label><input type="datetime-local" name="starts_at" value="${val('starts_at', toOrenburgInput(m?.starts_at))}" required></div>
      <div class="field"><label>Арена</label><input type="text" name="venue" value="${val('venue', m?.venue)}" placeholder="ЛД «Звёздный»"></div>
      <div class="field"><label>Адрес</label><input type="text" name="address" value="${val('address', m?.address)}"></div>
      <div class="field"><label>Длительность, мин</label><input type="text" name="duration_min" inputmode="numeric" value="${val('duration_min', m?.duration_min || 90)}"></div>
      <div class="field span-3"><label>Ссылка на трансляцию (VK Видео)</label><input type="text" name="stream_url" value="${val('stream_url', m?.stream_url)}" placeholder="https://vk.com/video-…"></div>
      <div class="field span-3"><label>Ссылка на хайлайты</label><input type="text" name="highlights_url" value="${val('highlights_url', m?.highlights_url)}"></div>
      <div class="form-actions">
        <button class="btn btn-sm" type="submit">${isNew ? 'Создать матч' : 'Сохранить'}</button>
        <button class="mini-btn" type="button" id="form-cancel">Отмена</button>
      </div>
    </form>`;
}

function matchRow(m) {
  const st = displayStatus(m);
  return `
    <tr data-id="${m.id}" class="${m.published ? '' : 'is-unpublished'}">
      <td>#${m.id}<br><span style="color:var(--muted)">${esc(formatMatchDate(m.starts_at))}</span></td>
      <td>
        <b>${esc(m.team_home)} — ${esc(m.team_away)}</b><br>
        <span style="color:var(--muted); font-size:12.5px">${esc([sportLabel(m.sport), m.age_group, m.league, m.venue].filter(Boolean).join(' · '))}</span>
        ${m.source !== 'manual' ? `<br><span style="color:var(--muted); font-size:11.5px">импорт: ${esc(m.source)}</span>` : ''}
      </td>
      <td><span class="badge badge-${st}">${STATUS_LABELS[st]}</span></td>
      <td class="t-actions">
        ${m.status !== 'live' && st !== 'finished' && m.status !== 'canceled' ? '<button class="mini-btn primary" data-act="live">▶ LIVE</button>' : ''}
        ${m.status === 'live' ? '<button class="mini-btn" data-act="finish">Завершить</button>' : ''}
        <button class="mini-btn" data-act="edit">Ред.</button>
        <button class="mini-btn" data-act="pub">${m.published ? 'Скрыть' : 'Показать'}</button>
        ${m.status !== 'canceled' ? '<button class="mini-btn" data-act="cancel">Отмена матча</button>' : ''}
        <button class="mini-btn danger" data-act="del">✕</button>
      </td>
    </tr>`;
}

function renderMatches() {
  const pane = $('pane-matches');
  // перерисовка не должна съедать несохраненный ввод открытой формы
  const liveForm = document.getElementById('match-form');
  if (liveForm && state.editing && liveForm.dataset.key === editingKey()) {
    state.editingDraft = Object.fromEntries(new FormData(liveForm).entries());
  }
  pane.innerHTML = `
    <div class="toolbar">
      <button class="btn btn-sm" id="btn-new-match" type="button">+ Новый матч</button>
      <label><input type="checkbox" id="chk-past"${state.includePast ? ' checked' : ''}> показывать прошедшие</label>
      <button class="mini-btn" id="btn-reload-matches" type="button">Обновить</button>
    </div>
    <div id="match-form-slot">${state.editing ? matchForm(state.editing === 'new' ? null : state.editing) : ''}</div>
    ${state.matches.length ? `
      <div class="table-scroll"><table class="admin-table">
        <thead><tr><th>Когда</th><th>Матч</th><th>Статус</th><th>Действия</th></tr></thead>
        <tbody>${state.matches.map(matchRow).join('')}</tbody>
      </table></div>`
      : '<p class="empty-note">Матчей нет. Создайте первый — или запустите импорт.</p>'}`;

  $('btn-new-match').addEventListener('click', () => {
    state.editing = 'new';
    state.editingDraft = null;
    renderMatches();
  });
  $('chk-past').addEventListener('change', async (e) => {
    state.includePast = e.target.checked;
    await guard(loadMatches);
    renderMatches();
  });
  $('btn-reload-matches').addEventListener('click', async () => { await guard(loadMatches); renderMatches(); });

  const form = $('match-form');
  if (form) {
    $('form-cancel').addEventListener('click', () => {
      state.editing = null;
      state.editingDraft = null;
      renderMatches();
    });
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const fd = new FormData(form);
      const body = Object.fromEntries(fd.entries());
      body.starts_at = orenburgInputToIso(body.starts_at);
      body.duration_min = Number(body.duration_min) || 90;
      await guard(async () => {
        if (state.editing === 'new') await api('/api/admin/matches', { method: 'POST', body: JSON.stringify(body) });
        else await api(`/api/admin/matches?id=${state.editing.id}`, { method: 'PATCH', body: JSON.stringify(body) });
        state.editing = null;
        state.editingDraft = null;
        await loadMatches();
        toast('Сохранено');
      });
      renderMatches();
    });
  }

  for (const tr of pane.querySelectorAll('tbody tr')) {
    const m = state.matches.find((x) => String(x.id) === tr.dataset.id);
    if (!m) continue;
    tr.addEventListener('click', async (e) => {
      const act = e.target.closest('[data-act]')?.dataset.act;
      if (!act) return;
      if (act === 'edit') { state.editing = m; state.editingDraft = null; renderMatches(); return; }
      if (act === 'live') {
        const url = prompt('Ссылка на трансляцию VK Видео (можно оставить пустой и добавить позже):', m.stream_url || '');
        if (url === null) return;
        await guard(() => api(`/api/admin/matches?id=${m.id}`, { method: 'PATCH', body: JSON.stringify({ status: 'live', stream_url: url }) }).then(loadMatches));
      }
      if (act === 'finish') {
        const hl = prompt('Ссылка на хайлайты/запись (не обязательно):', m.highlights_url || '');
        const body = { status: 'finished' };
        if (hl) body.highlights_url = hl;
        await guard(() => api(`/api/admin/matches?id=${m.id}`, { method: 'PATCH', body: JSON.stringify(body) }).then(loadMatches));
      }
      if (act === 'pub') {
        await guard(() => api(`/api/admin/matches?id=${m.id}`, { method: 'PATCH', body: JSON.stringify({ published: !m.published }) }).then(loadMatches));
      }
      if (act === 'cancel') {
        if (!confirm('Пометить матч отмененным?')) return;
        await guard(() => api(`/api/admin/matches?id=${m.id}`, { method: 'PATCH', body: JSON.stringify({ status: 'canceled' }) }).then(loadMatches));
      }
      if (act === 'del') {
        if (!confirm(`Удалить матч #${m.id} (${m.team_home} — ${m.team_away})? Заявки на него останутся.`)) return;
        await guard(() => api(`/api/admin/matches?id=${m.id}`, { method: 'DELETE' }).then(loadMatches));
      }
      renderMatches();
    });
  }
  updateCounters();
}

// ---------- Вкладка «Импорт» ----------

function queueCard(item) {
  const n = item.payload?.normalized || {};
  return `
    <div class="queue-card" data-id="${item.id}">
      <div class="row">
        <b>${esc(n.teamHome || '?')} — ${esc(n.teamAway || '?')}</b>
        <span class="badge ${item.kind === 'update' ? 'badge-today' : 'badge-upcoming'}">${item.kind === 'update' ? 'Изменение' : 'Новый'}</span>
        <span style="color:var(--muted); font-size:12px">источник: ${esc(item.source)}</span>
      </div>
      <p class="meta">${esc([sportLabel(n.sport), n.ageGroup, n.league, n.venue].filter(Boolean).join(' · '))}<br>
        ${n.startsAt ? esc(formatMatchDate(n.startsAt)) : 'дата неизвестна'}</p>
      ${item.kind === 'update' && item.payload.existing_match_id ? `<p class="queue-warn">Обновит матч #${item.payload.existing_match_id} (перенос времени/арены)</p>` : ''}
      ${item.payload?.possible_duplicate_of ? `<p class="queue-warn">⚠ Похож на матч #${item.payload.possible_duplicate_of} — проверьте, не дубль ли</p>` : ''}
      <div class="queue-actions">
        <button class="mini-btn primary" data-act="approve">Подтвердить</button>
        <button class="mini-btn danger" data-act="reject">Отклонить</button>
      </div>
    </div>`;
}

function reportText(report) {
  if (!report) return '';
  if (!report.bySource.length) return 'Источники не настроены: реестр адаптеров пуст (SOURCES.md — роадмап подключения). Каталог ведется вручную.';
  return report.bySource.map((s) =>
    `${s.label}: найдено ${s.fetched}, новых в очереди ${s.queued}, изменений ${s.updates}, пропущено ${s.skipped}` +
    (s.errors.length ? ` — ОШИБКА: ${s.errors.join('; ')}` : '')).join('\n');
}

function renderImport() {
  const pane = $('pane-import');
  pane.innerHTML = `
    <div class="toolbar">
      <button class="btn btn-sm" id="btn-run-import" type="button">Проверить обновления</button>
      <button class="mini-btn" id="btn-reload-queue" type="button">Обновить очередь</button>
    </div>
    ${state.lastReport ? `<div class="import-report">${esc(reportText(state.lastReport))}</div>` : ''}
    ${state.queue.length
      ? state.queue.map(queueCard).join('')
      : '<p class="empty-note">Очередь пуста. Новые матчи из источников появятся здесь после импорта.</p>'}`;

  $('btn-run-import').addEventListener('click', async () => {
    const btn = $('btn-run-import');
    btn.disabled = true;
    btn.textContent = 'Импортируем…';
    await guard(async () => {
      const data = await api('/api/admin/import', { method: 'POST' });
      state.lastReport = data.report;
      await loadQueue();
    });
    renderImport();
  });
  $('btn-reload-queue').addEventListener('click', async () => { await guard(loadQueue); renderImport(); });

  for (const card of pane.querySelectorAll('.queue-card')) {
    card.addEventListener('click', async (e) => {
      const act = e.target.closest('[data-act]')?.dataset.act;
      if (!act) return;
      await guard(async () => {
        await api(`/api/admin/import?id=${card.dataset.id}`, { method: 'PATCH', body: JSON.stringify({ action: act === 'approve' ? 'approve' : 'reject' }) });
        await Promise.all([loadQueue(), loadMatches()]);
        toast(act === 'approve' ? 'Матч добавлен в каталог' : 'Отклонено — больше не появится');
      });
      renderImport();
      renderMatches();
    });
  }
  updateCounters();
}

// ---------- Вкладка «Заявки» ----------

const serviceLabel = (id) => SERVICES.find((s) => s.id === id)?.label || id;

function requestCard(r) {
  const created = formatMatchDate(r.created_at);
  const matchInfo = r.matches
    ? `${esc(r.matches.team_home)} — ${esc(r.matches.team_away)} · ${esc(formatMatchDate(r.matches.starts_at))}${r.matches.venue ? ` · ${esc(r.matches.venue)}` : ''}`
    : r.custom_match
      ? `Свой матч: ${esc(r.custom_match.teams || '')} · ${esc(r.custom_match.date_text || '')}${r.custom_match.venue ? ` · ${esc(r.custom_match.venue)}` : ''} (${esc(sportLabel(r.custom_match.sport))})`
      : 'Матч не указан';
  const st = r.status;
  return `
    <div class="req-card" data-id="${r.id}">
      <div class="row">
        <b>${esc(r.name)}</b>
        <a href="tel:${esc(String(r.phone).replace(/[^+\d]/g, ''))}">${esc(r.phone)}</a>
        ${r.contact_channel ? `<span style="color:var(--muted); font-size:12px">${esc(r.contact_channel)}</span>` : ''}
        <span class="badge badge-${st === 'new' ? 'live' : st === 'confirmed' ? 'today' : 'upcoming'}">${{ new: 'Новая', confirmed: 'Подтверждена', done: 'Выполнена', declined: 'Отклонена' }[st]}</span>
        <span style="margin-left:auto; color:var(--muted); font-size:12px">${esc(created)}</span>
      </div>
      <p class="meta">${matchInfo}</p>
      <p class="meta">Услуги: ${(r.services || []).map(serviceLabel).map(esc).join(' + ')} ·
        <span class="price">${formatRub(r.price_quote || 0)}</span>
        ${r.player_note ? ` · Игрок: ${esc(r.player_note)}` : ''}</p>
      ${r.comment ? `<p class="meta">Комментарий: ${esc(r.comment)}</p>` : ''}
      <div class="req-actions">
        ${st === 'new' ? '<button class="mini-btn primary" data-st="confirmed">Подтвердить</button>' : ''}
        ${st === 'confirmed' ? '<button class="mini-btn primary" data-st="done">Выполнена</button>' : ''}
        ${st !== 'declined' && st !== 'done' ? '<button class="mini-btn danger" data-st="declined">Отклонить</button>' : ''}
        ${st === 'declined' || st === 'done' ? '<button class="mini-btn" data-st="new">Вернуть в новые</button>' : ''}
      </div>
    </div>`;
}

function renderRequests() {
  const pane = $('pane-requests');
  pane.innerHTML = `
    <div class="toolbar">
      <select class="filter-select" id="req-filter">
        ${[['new', 'Новые'], ['confirmed', 'Подтвержденные'], ['done', 'Выполненные'], ['declined', 'Отклоненные'], ['all', 'Все']]
          .map(([v, l]) => `<option value="${v}"${state.reqStatus === v ? ' selected' : ''}>${l}</option>`).join('')}
      </select>
      <button class="mini-btn" id="btn-reload-req" type="button">Обновить</button>
    </div>
    ${state.requests.length ? state.requests.map(requestCard).join('') : '<p class="empty-note">Заявок в этом статусе нет.</p>'}`;

  $('req-filter').addEventListener('change', async (e) => {
    state.reqStatus = e.target.value;
    await guard(loadRequests);
    renderRequests();
  });
  $('btn-reload-req').addEventListener('click', async () => { await guard(loadRequests); renderRequests(); });

  for (const card of pane.querySelectorAll('.req-card')) {
    card.addEventListener('click', async (e) => {
      const st = e.target.closest('[data-st]')?.dataset.st;
      if (!st) return;
      await guard(async () => {
        await api(`/api/admin/requests?id=${card.dataset.id}`, { method: 'PATCH', body: JSON.stringify({ status: st }) });
        await loadRequests();
      });
      renderRequests();
    });
  }
  updateCounters();
}

// ---------- Общее ----------

async function guard(fn) {
  try {
    await fn();
  } catch (e) {
    if (e instanceof ApiError && [401, 429, 503].includes(e.status)) showGate(explainError(e));
    else toast(explainError(e));
  }
}

function updateCounters() {
  $('cnt-matches').textContent = state.matches.length ? `(${state.matches.length})` : '';
  $('cnt-import').textContent = state.queue.length ? `(${state.queue.length})` : '';
  const fresh = state.requests.filter((r) => r.status === 'new').length;
  $('cnt-requests').textContent = fresh ? `(${fresh})` : '';
}

function renderAll() {
  renderMatches();
  renderImport();
  renderRequests();
}

function initTabs() {
  $('tabs').addEventListener('click', (e) => {
    const btn = e.target.closest('.admin-tab');
    if (!btn) return;
    state.tab = btn.dataset.tab;
    for (const b of document.querySelectorAll('.admin-tab')) b.classList.toggle('is-active', b === btn);
    for (const tab of ['matches', 'import', 'requests']) {
      $(`pane-${tab}`).hidden = tab !== state.tab;
    }
  });
  $('btn-logout').addEventListener('click', () => { state.token = ''; showGate(); });
}

function init() {
  initTabs();
  $('gate-form').addEventListener('submit', (e) => {
    e.preventDefault();
    gateError.classList.remove('is-on');
    state.token = $('gate-token').value.trim();
    if (state.token) enter();
  });
  if (state.token) enter();
}

init();
