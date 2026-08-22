// Общий UI всех страниц: бургер-меню, активный пункт навигации, липкая CTA.

export function initHeader() {
  const path = location.pathname.split('/').pop() || 'index.html';
  document.querySelectorAll('.topnav a, .mobnav a.mn-link').forEach((a) => {
    const href = a.getAttribute('href');
    if (href === path) a.setAttribute('aria-current', 'page');
  });

  const burger = document.getElementById('burger');
  const mobnav = document.getElementById('mobnav');
  if (!burger || !mobnav) return;
  const close = () => {
    burger.setAttribute('aria-expanded', 'false');
    mobnav.hidden = true;
    document.body.style.overflow = '';
  };
  burger.addEventListener('click', () => {
    const open = burger.getAttribute('aria-expanded') === 'true';
    if (open) close();
    else {
      burger.setAttribute('aria-expanded', 'true');
      mobnav.hidden = false;
      document.body.style.overflow = 'hidden';
    }
  });
  mobnav.addEventListener('click', (e) => {
    if (e.target.closest('a')) close();
  });
}

// Липкая CTA: видна после hero, прячется у футера и на самом блоке цели.
export function initStickyCta({ hideOver = [] } = {}) {
  const sticky = document.getElementById('sticky-cta');
  if (!sticky) return;
  const flags = new Map();
  const apply = () => {
    const blocked = [...flags.values()].some(Boolean);
    sticky.classList.toggle('is-visible', !blocked);
  };
  const watch = (el, key) => {
    if (!el) return;
    flags.set(key, true);
    new IntersectionObserver(([e]) => {
      flags.set(key, e.isIntersecting);
      apply();
    }, { threshold: 0.05 }).observe(el);
  };
  watch(document.querySelector('.hero') || document.querySelector('.page-head'), 'top');
  watch(document.querySelector('.footer'), 'footer');
  hideOver.forEach((sel, i) => watch(document.querySelector(sel), `extra${i}`));
}

let toastTimer = null;
export function toast(text) {
  let el = document.querySelector('.toast');
  if (!el) {
    el = document.createElement('div');
    el.className = 'toast';
    document.body.append(el);
  }
  el.textContent = text;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.remove(), 4200);
}
