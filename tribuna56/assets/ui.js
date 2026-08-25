// Общий UI: бургер-меню, липкая CTA, тосты, подстановка бренда,
// экранирование и иконки для шаблонных строк.

import { BRAND } from './data.js';

export function esc(s) {
  return String(s ?? '').replace(/[&<>"']/g, (c) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  }[c]));
}

export const icon = (id, cls = 'icon') =>
  `<svg class="${cls}" aria-hidden="true"><use href="#${id}"/></svg>`;

export function initNav() {
  const burger = document.getElementById('burger');
  const nav = document.getElementById('main-nav');
  if (!burger || !nav) return;
  burger.addEventListener('click', () => {
    const open = nav.classList.toggle('is-open');
    burger.setAttribute('aria-expanded', String(open));
  });
  nav.addEventListener('click', (e) => {
    if (e.target.closest('a')) {
      nav.classList.remove('is-open');
      burger.setAttribute('aria-expanded', 'false');
    }
  });
}

// Липкая мобильная CTA прячется, пока видна секция заявки (кнопка не нужна там, куда ведет).
export function initStickyCta() {
  const cta = document.getElementById('sticky-cta');
  const target = document.getElementById('request');
  if (!cta) return;
  if (!target || !('IntersectionObserver' in window)) return;
  const io = new IntersectionObserver(([entry]) => {
    cta.classList.toggle('is-hidden', entry.isIntersecting);
  }, { rootMargin: '0px 0px -20% 0px' });
  io.observe(target);
}

// Переключатель темы. Начальный data-theme ставит инлайн-скрипт в <head>
// (до первой отрисовки); здесь — кнопка, сохранение выбора и meta theme-color.
// SVG инлайном: кнопка не зависит от спрайта конкретной страницы.
const SUN = '<svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><circle cx="12" cy="12" r="4.2"/><path d="M12 2.5v2.6M12 18.9v2.6M2.5 12h2.6M18.9 12h2.6M5 5l1.8 1.8M17.2 17.2L19 19M19 5l-1.8 1.8M6.8 17.2L5 19"/></svg>';
const MOON = '<svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M20.2 14.8A8.3 8.3 0 019.2 3.8a8.3 8.3 0 1011 11z"/></svg>';
const THEME_COLORS = { light: '#f2f6fc', dark: '#0a1220' };

export function initThemeToggle() {
  const btn = document.getElementById('theme-toggle');
  if (!btn) return;
  const current = () => (document.documentElement.dataset.theme === 'dark' ? 'dark' : 'light');
  const render = () => {
    const dark = current() === 'dark';
    btn.innerHTML = dark ? SUN : MOON;
    btn.setAttribute('aria-label', dark ? 'Светлая тема' : 'Темная тема');
    btn.title = dark ? 'Светлая тема' : 'Темная тема';
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.content = THEME_COLORS[current()];
  };
  btn.addEventListener('click', () => {
    const next = current() === 'dark' ? 'light' : 'dark';
    document.documentElement.dataset.theme = next;
    try { localStorage.setItem('t56_theme', next); } catch { /* приватный режим */ }
    render();
  });
  render();
}

let toastTimer = null;
export function toast(msg) {
  const el = document.getElementById('toast');
  if (!el) return;
  el.textContent = msg;
  el.classList.add('is-on');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove('is-on'), 4000);
}

// Контакты и год — из data.js, чтобы бренд менялся одним файлом.
export function fillBrand() {
  for (const el of document.querySelectorAll('[data-bind="phone"]')) el.textContent = BRAND.phone;
  for (const el of document.querySelectorAll('[data-bind="phone-link"]')) {
    el.href = BRAND.phoneHref;
    if (!el.querySelector('*')) el.textContent = BRAND.phone;
  }
  for (const el of document.querySelectorAll('[data-bind="telegram"]')) el.href = BRAND.telegram;
  for (const el of document.querySelectorAll('[data-bind="vk"]')) el.href = BRAND.vk;
  const year = document.getElementById('year');
  if (year) year.textContent = String(new Date().getFullYear());
}
