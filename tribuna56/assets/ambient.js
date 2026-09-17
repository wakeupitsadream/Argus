// Ambient-превью портфолио/архива: видео в карточке тихо играет «за мутным
// стеклом» (размытый muted-плеер под кнопкой), клик — обычный просмотр.
// Бережно к ресурсам: только десктоп без reduced-motion/Save-Data, плеер
// монтируется когда карточка видна на экране и выгружается когда ушла
// (IntersectionObserver), одновременно не больше MAX_MOUNTED плееров,
// качество hd=1 — фону больше не нужно.

import { vkEmbedUrl, withAutoplayMuted } from './format.js';
import { nudgeVkMutedPlay } from './vkplayer.js';

export const MAX_MOUNTED = 6; // больше плееров разом тяжело даже десктопу

// src фонового плеера карточки: автозапуск без звука, цикл, низкое качество.
export function ambientSrc(vkUrl) {
  const src = withAutoplayMuted(vkEmbedUrl(vkUrl));
  return src ? `${src.replace('hd=2', 'hd=1')}&loop=1` : null;
}

// ---------- DOM (вызывается только в браузере) ----------

function eligible() {
  if (typeof window === 'undefined') return false;
  if (!window.matchMedia('(min-width: 900px)').matches) return false;
  if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) return false;
  if (navigator.connection && navigator.connection.saveData) return false;
  return true;
}

let io = null;
const mounted = new Set(); // кадры с живым превью
const visible = new Set(); // кадры в вьюпорте (кандидаты на превью)
const unnudges = new WeakMap();

function mount(frame) {
  if (mounted.has(frame) || mounted.size >= MAX_MOUNTED) return;
  const src = ambientSrc(frame.dataset.ambient);
  if (!src) return;
  const f = document.createElement('iframe');
  f.className = 'ambient-frame';
  f.src = src;
  f.setAttribute('allow', 'autoplay; encrypted-media');
  f.setAttribute('referrerpolicy', 'no-referrer');
  f.setAttribute('tabindex', '-1');
  f.setAttribute('aria-hidden', 'true');
  f.title = '';
  unnudges.set(frame, nudgeVkMutedPlay(f)); // страховка muted-автозапуска
  frame.prepend(f); // под кнопкой «Смотреть» (она позже в DOM — рисуется выше)
  frame.classList.add('has-ambient');
  mounted.add(frame);
}

function unmount(frame) {
  const f = frame.querySelector('.ambient-frame');
  if (f) f.remove();
  frame.classList.remove('has-ambient');
  const un = unnudges.get(frame);
  if (un) { un(); unnudges.delete(frame); }
  mounted.delete(frame);
}

// освободился слот — отдаем его следующей видимой карточке
function fill() {
  for (const frame of visible) {
    if (mounted.size >= MAX_MOUNTED) break;
    if (frame.isConnected && frame.dataset.ambient) mount(frame);
  }
}

// карточки перерисованы (innerHTML) — старые узлы забываем
function prune() {
  for (const frame of [...mounted, ...visible]) {
    if (frame.isConnected) continue;
    unmount(frame);
    visible.delete(frame);
    if (io) io.unobserve(frame);
  }
}

function onIntersect(entries) {
  for (const e of entries) {
    const frame = e.target;
    // карточку перерисовали или клик заменил превью настоящим плеером
    if (!frame.isConnected || !frame.dataset.ambient) {
      io.unobserve(frame);
      visible.delete(frame);
      unmount(frame);
      continue;
    }
    if (e.isIntersecting) { visible.add(frame); mount(frame); }
    else { visible.delete(frame); unmount(frame); } // ушла с экрана — не тянем видео впустую
  }
  fill();
}

// Клик «Смотреть»: превью снято навсегда, слот достается другой карточке.
export function releaseAmbient(frame) {
  if (!frame) return;
  delete frame.dataset.ambient;
  visible.delete(frame);
  unmount(frame);
  if (io) io.unobserve(frame);
  fill();
}

// Навесить ambient-превью на все .frame[data-ambient] внутри rootEl.
// Безопасно вызывать повторно после каждой перерисовки портфолио.
export function initAmbientPreviews(rootEl) {
  if (!rootEl || !eligible() || typeof IntersectionObserver === 'undefined') return;
  io = io || new IntersectionObserver(onIntersect, { threshold: 0.25 });
  prune();
  for (const frame of rootEl.querySelectorAll('.frame[data-ambient]')) {
    io.observe(frame);
  }
}
