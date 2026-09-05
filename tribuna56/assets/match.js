// Страница матча: /match/:id (rewrite) или match.html?id=…
// Состояния: загрузка → не найден → найден (по displayStatus: плашка ожидания,
// LIVE-плеер VK, запись, отметка об отмене).

import { BRAND, sportLabel } from './data.js';
import { displayStatus, STATUS_LABELS } from './catalog.js';
import { formatMatchDate, vkEmbedUrl, withAutoplayMuted, cacheBucket } from './format.js';
import { nudgeVkMutedPlay } from './vkplayer.js';
import { SEED_MATCHES } from './seed-matches.js';
import { initNav, initThemeToggle, fillBrand, toast, esc, icon } from './ui.js';
import { teamBadgePair } from './badges.js';
import { mountRequestForm } from './request-form.js';

const root = document.getElementById('match-root');
const requestSection = document.getElementById('request');

function matchIdFromUrl() {
  const q = new URLSearchParams(location.search);
  if (q.get('id')) return Number(q.get('id'));
  const m = location.pathname.match(/\/match\/(\d+)/);
  return m ? Number(m[1]) : NaN;
}

async function loadMatch(id) {
  try {
    // ?v=<30с-ведро>: свежий LIVE/статус не залипает в CDN-кэше
    const r = await fetch(`/api/matches?id=${id}&v=${cacheBucket()}`);
    const data = await r.json(); // без API здесь бросит → seed-фолбэк
    if (r.status === 404 && data.error === 'not_found') {
      // БД жива, но матча в ней нет — возможно, это ссылка на seed-каталог
      const seed = SEED_MATCHES.find((m) => m.id === id);
      return seed ? { state: 'ok', match: seed } : { state: 'notfound' };
    }
    if (data.ok && data.match) return { state: 'ok', match: data.match };
    throw new Error(data.error || 'unavailable');
  } catch {
    const seed = SEED_MATCHES.find((m) => m.id === id);
    return seed ? { state: 'ok', match: seed } : { state: 'unavailable' };
  }
}

// Наружу ведем только по http(s): защита от случайного javascript:-URL в БД.
const httpUrl = (u) => (/^https?:\/\//i.test(String(u || '')) ? String(u) : null);

function plate(iconId, title, text, extra = '') {
  return `
    <div class="player-plate">
      ${icon(iconId)}
      <b>${esc(title)}</b>
      <span>${text}</span>
      ${extra}
    </div>`;
}

function playerBlock(m, st) {
  if (st === 'canceled') {
    return plate('i-clock', 'Матч отменен или перенесен',
      'Следите за каталогом — новая дата появится там.');
  }
  if (st === 'live') {
    // эфир стартует сам, без звука (браузеры разрешают только muted-автозапуск);
    // звук зритель включает кнопкой громкости в плеере
    const src = withAutoplayMuted(vkEmbedUrl(m.stream_url));
    if (src) {
      return `<iframe src="${esc(src)}" allow="autoplay; encrypted-media; fullscreen; picture-in-picture" allowfullscreen referrerpolicy="no-referrer" title="Прямая трансляция"></iframe>`;
    }
    const liveLink = httpUrl(m.stream_url);
    if (liveLink) {
      return plate('i-play', 'Идет прямая трансляция',
        'Плеер откроется в VK Видео.',
        `<a class="btn" href="${esc(liveLink)}" target="_blank" rel="noopener">${icon('i-play')} Смотреть в VK</a>`);
    }
    return plate('i-cam', 'Эфир начинается', 'Плеер появится здесь с минуты на минуту — обновите страницу.');
  }
  if (st === 'finished') {
    const src = vkEmbedUrl(m.highlights_url) || vkEmbedUrl(m.stream_url);
    if (src) {
      return `<iframe src="${esc(src)}" allow="encrypted-media; fullscreen; picture-in-picture" allowfullscreen referrerpolicy="no-referrer" title="Запись матча"></iframe>`;
    }
    const link = httpUrl(m.highlights_url) || httpUrl(m.stream_url);
    if (link) {
      return plate('i-play', 'Матч завершен',
        'Запись доступна в VK Видео.',
        `<a class="btn" href="${esc(link)}" target="_blank" rel="noopener">${icon('i-play')} Смотреть запись</a>`);
    }
    return plate('i-scissors', 'Матч завершен', 'Если мы снимали эту игру — запись и хайлайты скоро появятся здесь.');
  }
  return plate('i-cam', 'Трансляция появится на этой странице',
    'Закажите съемку — и перед началом игры здесь включится плеер. Ссылку можно отправить родным заранее.',
    `<a class="btn" href="#request">${icon('i-send')} Заказать съемку</a>`);
}

function render(m) {
  const st = displayStatus(m);
  document.title = `${m.team_home} — ${m.team_away} · ${BRAND.name}`;
  const facts = [
    `<span>${icon('i-clock')}${esc(formatMatchDate(m.starts_at))}</span>`,
    m.venue ? `<span>${icon('i-pin')}${esc(m.venue)}${m.address ? `, ${esc(m.address)}` : ''}</span>` : '',
    `<span>${icon(`i-${esc(m.sport)}`)}${esc([sportLabel(m.sport), m.age_group, m.league].filter(Boolean).join(' · '))}</span>`,
  ].filter(Boolean).join('');

  const orderable = st === 'upcoming' || st === 'today';
  root.innerHTML = `
    <section class="match-hero">
      <div class="wrap">
        <span class="badge badge-${st}">${st === 'live' ? '<span class="live-dot"></span>' : ''}${STATUS_LABELS[st]}</span>
        <h1 class="match-title">${teamBadgePair(m.team_home, m.team_away, 34)} ${esc(m.team_home)} — ${esc(m.team_away)}</h1>
        <div class="facts">${facts}</div>
      </div>
    </section>
    <section class="wrap" style="padding-bottom: 26px">
      <div class="player-frame">${playerBlock(m, st)}</div>
      <div class="match-actions">
        ${orderable ? `<a class="btn" href="#request">${icon('i-send')} Заказать съемку этого матча</a>` : ''}
        <button class="btn btn-ghost" id="btn-share" type="button">${icon('i-share')} Поделиться</button>
        <a class="btn btn-ghost" href="/#matches">Все матчи</a>
      </div>
    </section>`;

  if (st === 'live') {
    const liveFrame = root.querySelector('.player-frame iframe');
    if (liveFrame) nudgeVkMutedPlay(liveFrame); // страховка muted-автозапуска
  }

  document.getElementById('btn-share').addEventListener('click', async () => {
    const url = location.href;
    const title = `${m.team_home} — ${m.team_away} · ${BRAND.name}`;
    try {
      if (navigator.share) {
        await navigator.share({ title, url });
      } else {
        await navigator.clipboard.writeText(url);
        toast('Ссылка скопирована — отправьте ее родным');
      }
    } catch { /* пользователь закрыл шаринг — не ошибка */ }
  });

  requestSection.hidden = false;
  const form = mountRequestForm(document.getElementById('request-form'), {
    onChangeRequest: () => { location.href = '/#matches'; },
  });
  if (orderable) form.setSelection(m);
}

function renderNotFound(unavailable) {
  document.title = `Матч не найден · ${BRAND.name}`;
  root.innerHTML = `
    <section class="section">
      <div class="wrap">
        <div class="state-plate" style="padding: 60px 24px">
          <b>${unavailable ? 'Не получилось загрузить матч' : 'Такого матча в каталоге нет'}</b>
          ${unavailable
            ? `Попробуйте обновить страницу или позвоните нам: <a href="${esc(BRAND.phoneHref)}">${esc(BRAND.phone)}</a>`
            : 'Возможно, он был снят с публикации. Загляните в каталог — там все ближайшие игры.'}
          <div style="margin-top:20px"><a class="btn" href="/#matches">Открыть каталог</a></div>
        </div>
      </div>
    </section>`;
}

async function init() {
  fillBrand();
  initNav();
  initThemeToggle();
  const id = matchIdFromUrl();
  if (!Number.isInteger(id) || id <= 0) return renderNotFound(false);
  const result = await loadMatch(id);
  if (result.state === 'ok') render(result.match);
  else renderNotFound(result.state === 'unavailable');
}

init();
