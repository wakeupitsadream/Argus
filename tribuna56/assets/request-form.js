// Переиспользуемая форма заявки (главная и страница матча).
// Правила эталона: ввод зеркалится в state, экран успеха держится флагом
// showingDone, деградация сети — честный тост с телефоном (бизнес, не демо).

import { SERVICES, SPORTS, CONTACT_CHANNELS, BRAND, SLOT, sportLabel } from './data.js';
import { quoteServices, formatRub } from './pricing.js';
import { validateRequest } from './validate.js';
import { formatMatchDate } from './format.js';
import { toast, esc, icon } from './ui.js';

export function mountRequestForm(root, opts = {}) {
  const state = {
    match: null,
    customMode: false,
    services: new Set(['stream']),
    form: {
      name: '', phone: '', channel: 'phone', comment: '', player: '', consent: false,
      cSport: SPORTS[0].id, cTeams: '', cVenue: '', cDate: '',
    },
    showingDone: false,
    slotReq: 0,
  };

  root.innerHTML = `
    <div class="request-grid" id="rf-grid">
      <div>
        <div id="rf-selection"></div>
        <div id="rf-custom" hidden>
          <div class="field">
            <label for="rf-c-sport">Вид спорта</label>
            <select id="rf-c-sport">
              ${SPORTS.map((s) => `<option value="${s.id}">${esc(s.label)}</option>`).join('')}
            </select>
          </div>
          <div class="field" data-f="cTeams">
            <label for="rf-c-teams">Команды / соперники</label>
            <input type="text" id="rf-c-teams" placeholder="Юниор-2012 — Сармат-2012" autocomplete="off">
            <p class="field-error">Напишите, кто играет</p>
          </div>
          <div class="field-row">
            <div class="field" data-f="cDate">
              <label for="rf-c-date">Дата и время</label>
              <input type="text" id="rf-c-date" placeholder="суббота, 14 сентября, 12:00" autocomplete="off">
              <p class="field-error">Когда матч?</p>
            </div>
            <div class="field">
              <label for="rf-c-venue">Где играют</label>
              <input type="text" id="rf-c-venue" placeholder="ЛД «Звёздный»" autocomplete="off">
            </div>
          </div>
        </div>
        <h3 style="margin-top:18px">Услуги</h3>
        <div class="svc-options">
          ${SERVICES.map((s) => `
            <label class="svc-option${state.services.has(s.id) ? ' is-on' : ''}" data-svc="${s.id}">
              <input type="checkbox" value="${s.id}" ${state.services.has(s.id) ? 'checked' : ''}>
              <span style="flex:1">
                <span class="svc-label"><span>${esc(s.label)}</span><span class="svc-price">${formatRub(s.price)}</span></span>
                <p>${esc(s.desc)}</p>
              </span>
            </label>`).join('')}
        </div>
        <div id="rf-calc"></div>
        <div id="rf-slot"></div>
      </div>
      <div>
        <form id="rf-form" novalidate>
          <div class="field" data-f="name">
            <label for="rf-name">Ваше имя</label>
            <input type="text" id="rf-name" autocomplete="name" placeholder="Как к вам обращаться">
            <p class="field-error">Укажите имя</p>
          </div>
          <div class="field" data-f="phone">
            <label for="rf-phone">Телефон</label>
            <input type="tel" id="rf-phone" inputmode="tel" autocomplete="tel" placeholder="+7 ___ ___-__-__">
            <p class="field-error">Укажите телефон полностью</p>
          </div>
          <div class="field">
            <label for="rf-channel">Как с вами связаться</label>
            <select id="rf-channel">
              ${CONTACT_CHANNELS.map((c) => `<option value="${c.id}">${esc(c.label)}</option>`).join('')}
            </select>
          </div>
          <div class="field" id="rf-player-field" hidden>
            <label for="rf-player">Номер и фамилия игрока</label>
            <input type="text" id="rf-player" placeholder="№ 17, Иванов" autocomplete="off">
          </div>
          <div class="field">
            <label for="rf-comment">Комментарий</label>
            <textarea id="rf-comment" placeholder="Всё, что нам стоит знать: ворота, трибуна, особые моменты"></textarea>
          </div>
          <input class="hp" type="text" name="website" id="rf-website" tabindex="-1" autocomplete="off" aria-hidden="true">
          <label class="consent" id="rf-consent-label">
            <input type="checkbox" id="rf-consent">
            <span>Согласен на обработку персональных данных и принимаю <a href="privacy.html" target="_blank" rel="noopener">политику конфиденциальности</a></span>
          </label>
          <button class="btn" type="submit" id="rf-submit" style="width:100%">${icon('i-send')} Отправить заявку</button>
          <p class="calc-note" style="text-align:center">Ответим в течение часа в рабочее время</p>
        </form>
      </div>
    </div>
    <div class="form-done" id="rf-done" hidden>
      ${icon('i-check')}
      <h3>Заявка отправлена!</h3>
      <p id="rf-done-text">Мы свяжемся с вами, подтвердим детали и пришлем ссылку на трансляцию.</p>
      <button class="btn btn-ghost btn-sm" id="rf-again" type="button" style="margin-top:18px">Отправить еще одну заявку</button>
    </div>`;

  const $ = (id) => root.querySelector(id);
  const grid = $('#rf-grid');
  const done = $('#rf-done');
  const form = $('#rf-form');
  const selectionEl = $('#rf-selection');
  const customEl = $('#rf-custom');
  const calcEl = $('#rf-calc');
  const slotEl = $('#rf-slot');
  const submitBtn = $('#rf-submit');

  // ---- зеркалирование ввода в state ----
  const mirror = [
    ['#rf-name', 'name'], ['#rf-phone', 'phone'], ['#rf-channel', 'channel'],
    ['#rf-comment', 'comment'], ['#rf-player', 'player'],
    ['#rf-c-sport', 'cSport'], ['#rf-c-teams', 'cTeams'],
    ['#rf-c-venue', 'cVenue'], ['#rf-c-date', 'cDate'],
  ];
  for (const [sel, key] of mirror) {
    const el = $(sel);
    el.value = state.form[key];
    el.addEventListener('input', () => {
      state.form[key] = el.value;
      el.closest('.field')?.classList.remove('is-error');
    });
  }
  $('#rf-consent').addEventListener('change', (e) => {
    state.form.consent = e.target.checked;
    $('#rf-consent-label').classList.remove('is-error');
  });

  for (const label of root.querySelectorAll('.svc-option')) {
    const input = label.querySelector('input');
    input.addEventListener('change', () => {
      if (input.checked) state.services.add(input.value);
      else state.services.delete(input.value);
      label.classList.toggle('is-on', input.checked);
      renderCalc();
      $('#rf-player-field').hidden = !state.services.has('personal');
    });
  }

  function renderSelection() {
    if (state.match) {
      const m = state.match;
      const meta = [sportLabel(m.sport), m.age_group, m.league].filter(Boolean).join(' · ');
      selectionEl.innerHTML = `
        <div class="selected-match">
          <div class="match-teams">${icon(`i-${esc(m.sport)}`)}<span>${esc(m.team_home)} — ${esc(m.team_away)}</span></div>
          <div class="match-meta">${esc(meta)}<br>${esc(formatMatchDate(m.starts_at))}${m.venue ? ` · ${esc(m.venue)}` : ''}</div>
          <button type="button" class="unselect" id="rf-unselect">Выбрать другой матч</button>
        </div>`;
      $('#rf-unselect').addEventListener('click', () => {
        api.setSelection(null);
        opts.onChangeRequest?.();
      });
    } else if (state.customMode) {
      selectionEl.innerHTML = `
        <div class="selected-match">
          <div class="match-teams">${icon('i-other')}<span>Свой матч</span></div>
          <div class="match-meta">Впишите игру — добавим ее в каталог и подтвердим съемку.</div>
          <button type="button" class="unselect" id="rf-from-catalog">Выбрать из каталога</button>
        </div>`;
      $('#rf-from-catalog').addEventListener('click', () => {
        api.setSelection(null);
        opts.onChangeRequest?.();
      });
    } else {
      selectionEl.innerHTML = `
        <div class="selected-match" data-f="match">
          <div class="match-teams">${icon('i-target')}<span>Матч не выбран</span></div>
          <div class="match-meta">Выберите игру в каталоге — или впишите свою.</div>
          <p class="field-error">Выберите матч из каталога или опишите свой</p>
          <button type="button" class="unselect" id="rf-go-custom">Моего матча нет в списке</button>
        </div>`;
      $('#rf-go-custom').addEventListener('click', () => api.setSelection(null, { custom: true }));
    }
    customEl.hidden = !state.customMode;
  }

  function renderCalc() {
    const q = quoteServices([...state.services]);
    if (!q.items.length) {
      calcEl.innerHTML = '<p class="calc-note">Отметьте хотя бы одну услугу</p>';
      return;
    }
    calcEl.innerHTML = `
      ${q.items.map((i) => `<div class="calc-row"><span>${esc(i.label)}</span><span>${formatRub(i.price)}</span></div>`).join('')}
      ${q.discount ? `<div class="calc-row"><span>${esc(q.discountLabel)}</span><span>−${formatRub(q.discount)}</span></div>` : ''}
      <div class="calc-total"><span>Итого</span><b>${formatRub(q.total)}</b></div>
      <p class="calc-note">Точную сумму подтвердим вместе с заявкой — без скрытых доплат.</p>`;
  }

  async function checkSlot() {
    slotEl.innerHTML = '';
    if (!state.match) return;
    const reqId = ++state.slotReq;
    try {
      const p = new URLSearchParams({
        starts_at: state.match.starts_at,
        duration_min: String(state.match.duration_min || SLOT.defaultDurationMin),
      });
      const r = await fetch(`/api/availability?${p}`);
      const data = await r.json();
      if (reqId !== state.slotReq || !data.ok) return;
      slotEl.innerHTML = data.free
        ? `<span class="slot-badge slot-free">${icon('i-check')} Дата свободна — оператор успевает на этот матч</span>`
        : `<span class="slot-badge slot-busy">${icon('i-clock')} На это время уже есть съемка — примем заявку и уточним второго оператора</span>`;
    } catch {
      /* проверка информирует, а не блокирует — молчим */
    }
  }

  function markErrors(errors) {
    for (const key of ['name', 'phone']) {
      if (errors[key]) root.querySelector(`[data-f="${key}"]`)?.classList.add('is-error');
    }
    if (errors.consent) $('#rf-consent-label').classList.add('is-error');
    if (errors.services) toast('Отметьте хотя бы одну услугу');
    if (errors.match) {
      if (state.customMode) {
        if (String(state.form.cTeams).trim().length < 3) root.querySelector('[data-f="cTeams"]')?.classList.add('is-error');
        if (String(state.form.cDate).trim().length < 3) root.querySelector('[data-f="cDate"]')?.classList.add('is-error');
      } else {
        selectionEl.querySelector('[data-f="match"]')?.classList.add('is-error');
        toast('Сначала выберите матч из каталога');
        opts.onChangeRequest?.();
      }
    }
  }

  function buildPayload() {
    const f = state.form;
    return {
      match_id: state.match ? state.match.id : null,
      custom_match: state.customMode ? {
        sport: f.cSport, teams: f.cTeams, venue: f.cVenue, date_text: f.cDate,
      } : null,
      services: [...state.services],
      player_note: f.player,
      name: f.name,
      phone: f.phone,
      contact_channel: f.channel,
      comment: f.comment,
      consent: f.consent,
      website: $('#rf-website').value,
    };
  }

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const payload = buildPayload();
    const v = validateRequest(payload);
    if (!v.ok) {
      markErrors(v.errors);
      return;
    }
    submitBtn.disabled = true;
    submitBtn.textContent = 'Отправляем…';
    try {
      const r = await fetch('/api/request', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      const data = await r.json().catch(() => ({}));
      if (r.status === 400 && data.fields) {
        markErrors(data.fields);
        return;
      }
      if (!r.ok || data.ok !== true) throw new Error('send failed');
      state.showingDone = true;
      const total = quoteServices(v.services).total;
      $('#rf-done-text').textContent =
        `Мы свяжемся с вами, подтвердим детали и пришлем ссылку на трансляцию. Расчет: ${formatRub(total)}.`;
      grid.hidden = true;
      done.hidden = false;
      done.scrollIntoView({ behavior: 'smooth', block: 'center' });
    } catch {
      toast(`Не получилось отправить. Позвоните нам: ${BRAND.phone}`);
    } finally {
      submitBtn.disabled = false;
      submitBtn.innerHTML = `${icon('i-send')} Отправить заявку`;
    }
  });

  $('#rf-again').addEventListener('click', () => {
    state.showingDone = false;
    done.hidden = true;
    grid.hidden = false;
  });

  const api = {
    setSelection(match, { custom = false } = {}) {
      if (state.showingDone) {
        state.showingDone = false;
        done.hidden = true;
        grid.hidden = false;
      }
      state.match = match || null;
      state.customMode = Boolean(custom && !match);
      renderSelection();
      checkSlot();
    },
    getState: () => state,
  };

  renderSelection();
  renderCalc();
  return api;
}
