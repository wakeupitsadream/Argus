// Браузерный смоук «Трибуны+» на вшитом seed-каталоге (без БД): каталог,
// форма, валидация, страница матча, админка, 375px, чистая консоль.
// Запуск: поднять статику (npm run dev) и `node tools/verify-browser.mjs`.
// Нужен playwright с chromium: локальный, глобальный npm или /opt/pw-browsers.
import { createRequire } from 'node:module';
import { SEED_MATCHES } from '../assets/seed-matches.js';

let require;
try {
  require = createRequire(import.meta.url);
  require.resolve('playwright');
} catch {
  require = createRequire('/opt/node22/lib/node_modules/');
}
const { chromium } = require('playwright');

const BASE = process.env.BASE_URL || 'http://127.0.0.1:3456';
const results = [];
const check = (name, cond) => {
  results.push([cond ? 'PASS' : 'FAIL', name]);
  if (!cond) process.exitCode = 1;
};

const upcoming = SEED_MATCHES
  .filter((m) => m.status === 'scheduled' && Date.parse(m.starts_at) > Date.now())
  .sort((a, b) => Date.parse(a.starts_at) - Date.parse(b.starts_at));
if (!upcoming.length) {
  console.error('FAIL  seed-каталог пуст или все матчи в прошлом — обновите assets/seed-matches.js');
  process.exit(1);
}
const sports = new Set(upcoming.map((m) => m.sport));

const consoleErrors = [];
const pageErrors = [];

async function mockApi(page) {
  await page.route('**/api/matches*', (r) => r.fulfill({ status: 404, contentType: 'text/plain', body: 'nf' }));
  await page.route('**/api/request', (r) => r.fulfill({ status: 200, contentType: 'application/json', body: '{"ok":true}' }));
  await page.route('**/api/availability*', (r) => r.fulfill({ status: 200, contentType: 'application/json', body: '{"ok":true,"free":true}' }));
  await page.route('**/api/admin/matches*', (r) => r.fulfill({
    status: 200, contentType: 'application/json',
    body: JSON.stringify({ ok: true, matches: [{ id: 1, sport: 'hockey', league: 'Первенство', age_group: '2012 г.р.', team_home: 'Юниор', team_away: 'Сармат', venue: 'ЛД', starts_at: new Date(Date.now() + 86400000).toISOString(), duration_min: 90, status: 'scheduled', stream_url: null, highlights_url: null, source: 'manual', source_key: null, published: true }] }),
  }));
  await page.route('**/api/admin/requests*', (r) => r.fulfill({ status: 200, contentType: 'application/json', body: '{"ok":true,"requests":[]}' }));
  await page.route('**/api/admin/import*', (r) => r.fulfill({ status: 200, contentType: 'application/json', body: '{"ok":true,"queue":[]}' }));
  page.on('pageerror', (e) => pageErrors.push(String(e)));
  page.on('console', (m) => {
    if (m.type() === 'error' && !/fonts\.g|net::|Failed to load resource/.test(m.text())) {
      consoleErrors.push(m.text());
    }
  });
}

const browser = await chromium.launch();

// ---------- Главная (desktop) ----------
{
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  await mockApi(page);
  await page.goto(BASE, { waitUntil: 'networkidle' });
  await page.waitForSelector('.match-card', { timeout: 8000 });
  const cards = await page.$$('.match-card');
  check(`каталог: seed-матчи отрисованы (${cards.length}/${upcoming.length})`, cards.length >= Math.min(3, upcoming.length));
  check('каталог: пометка о ручной сверке расписания', (await page.textContent('#catalog')).includes('сверено вручную'));
  check('hero: счетчик матчей обновился', /матч/.test(await page.textContent('#fact-matches')));

  if (sports.size > 1) {
    const firstSport = upcoming[0].sport;
    await page.click(`.chip[data-sport="${firstSport}"]`);
    const filtered = await page.$$('.match-card');
    check(`фильтр по «${firstSport}» сузил список (${filtered.length})`, filtered.length > 0 && filtered.length < cards.length);
    await page.click('.chip[data-sport="all"]');
  }

  // выбор матча из каталога
  await page.click('.match-card [data-act="order"]');
  const sel = await page.textContent('#rf-selection');
  check('форма: матч подставился в заявку', /—/.test(sel) && !/Матч не выбран/.test(sel));
  await page.waitForSelector('#rf-slot .slot-badge', { timeout: 5000 });
  check('форма: бейдж доступности даты', (await page.textContent('#rf-slot')).includes('Дата свободна'));

  // пустая отправка → ошибки валидации
  await page.click('#rf-submit');
  await page.waitForTimeout(200);
  const errFields = await page.$$eval('.field.is-error', (els) => els.length);
  check(`валидация: подсвечены пустые поля (${errFields})`, errFields >= 2);
  check('валидация: согласие подсвечено', await page.$eval('#rf-consent-label', (el) => el.classList.contains('is-error')));

  // заполняем и отправляем
  await page.fill('#rf-name', 'Тест Тестович');
  await page.fill('#rf-phone', '+7 912 345-67-89');
  await page.check('#rf-consent');
  await page.click('#rf-submit');
  await page.waitForSelector('#rf-done:not([hidden])', { timeout: 5000 });
  check('заявка: экран успеха с расчетом', (await page.textContent('#rf-done-text')).includes('Расчет'));

  // еще одна заявка + кастомный матч
  await page.click('#rf-again');
  await page.click('#btn-custom');
  check('кастом: поля своего матча показаны', !(await page.$eval('#rf-custom', (el) => el.hidden)));
  await page.click('#rf-submit');
  await page.waitForTimeout(200);
  check('кастом: пустые команды/дата подсвечены',
    await page.$eval('[data-f="cTeams"]', (el) => el.classList.contains('is-error')));
  await page.fill('#rf-c-teams', 'Юниор — Сармат');
  await page.fill('#rf-c-date', 'суббота 14:00');
  await page.click('#rf-submit');
  await page.waitForSelector('#rf-done:not([hidden])', { timeout: 5000 });
  check('кастом: заявка ушла', true);

  // калькулятор: пакетная скидка
  await page.click('#rf-again');
  await page.check('.svc-option[data-svc="highlights"] input');
  const calc = (await page.textContent('#rf-calc')).replace(/ /g, ' ');
  check('калькулятор: пакетная скидка отображена', calc.includes('Эфир + хайлайты') && calc.includes('5 000'));
  await page.close();
}

// ---------- Главная (375px) ----------
{
  const page = await browser.newPage({ viewport: { width: 375, height: 740 } });
  await mockApi(page);
  await page.goto(BASE, { waitUntil: 'networkidle' });
  await page.waitForSelector('.match-card');
  const { sw, cw } = await page.evaluate(() => ({
    sw: document.documentElement.scrollWidth, cw: document.documentElement.clientWidth,
  }));
  check(`375px: нет горизонтального скролла (${sw}/${cw})`, sw <= cw);
  check('375px: бургер виден', await page.$eval('#burger', (el) => getComputedStyle(el).display !== 'none'));
  await page.click('#burger');
  check('375px: меню открывается', await page.$eval('#main-nav', (el) => el.classList.contains('is-open')));
  check('375px: липкая CTA есть', Boolean(await page.$('#sticky-cta')));
  await page.close();
}

// ---------- Страница матча ----------
{
  const target = upcoming[0];
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  await mockApi(page);
  await page.goto(`${BASE}/match.html?id=${target.id}`, { waitUntil: 'networkidle' });
  await page.waitForSelector('.match-title');
  check('матч: команды в заголовке', (await page.textContent('.match-title')).includes(target.team_home));
  check('матч: плашка ожидания эфира', (await page.textContent('.player-plate')).includes('Трансляция появится'));
  check('матч: заявка предзаполнена', (await page.textContent('#rf-selection')).includes(target.team_home));
  check('матч: title обновлен', (await page.title()).includes(target.team_home));

  await page.goto(`${BASE}/match.html?id=123456789`, { waitUntil: 'networkidle' });
  await page.waitForSelector('.state-plate');
  check('несуществующий матч: «не найден»/«не загрузить»', /матча|загрузить/i.test(await page.textContent('.state-plate')));
  const { sw, cw } = await page.evaluate(() => ({
    sw: document.documentElement.scrollWidth, cw: document.documentElement.clientWidth,
  }));
  check('матч: нет горизонтального скролла', sw <= cw);
  await page.close();
}

// ---------- Админка ----------
{
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  await mockApi(page);
  await page.goto(`${BASE}/admin.html`, { waitUntil: 'networkidle' });
  check('админка: гейт показан', !(await page.$eval('#gate', (el) => el.hidden)));
  await page.fill('#gate-token', 'test-token');
  await page.click('#gate-form button[type=submit]');
  await page.waitForSelector('#app:not([hidden])', { timeout: 5000 });
  check('админка: вход по токену', true);
  check('админка: таблица матчей отрисована', (await page.textContent('#pane-matches')).includes('Юниор'));
  await page.click('[data-tab="import"]');
  check('админка: вкладка импорта с пустой очередью', (await page.textContent('#pane-import')).includes('Очередь пуста'));
  await page.click('[data-tab="requests"]');
  check('админка: вкладка заявок', (await page.textContent('#pane-requests')).includes('Заявок'));
  await page.close();
}

await browser.close();

for (const [st, name] of results) console.log(`${st}  ${name}`);
console.log(`\nconsole errors: ${consoleErrors.length}`, consoleErrors.slice(0, 5));
console.log(`page errors: ${pageErrors.length}`, pageErrors.slice(0, 5));
if (consoleErrors.length || pageErrors.length) process.exitCode = 1;
console.log(process.exitCode ? '\n=== FAIL ===' : '\n=== ALL PASS ===');
