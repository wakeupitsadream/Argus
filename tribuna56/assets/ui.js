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
