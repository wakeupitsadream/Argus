// Переиспользуемые рендеры разделов. Каждый принимает контейнер и рисует из data.js.
import { BRAND, ZONES, PRICING, CLUBS, PROMOS, MENU, RULES } from './data.js';
import { formatRub, plural, rateWindow } from './pricing.js';

export function renderZones(el) {
  el.innerHTML = Object.values(ZONES).map((z) => `
    <article class="zone-card">
      <h3>${z.name}</h3>
      <p class="z-tag">${z.tagline}</p>
      ${z.specs.length
        ? `<dl>${z.specs.map(([k, v]) => `<div><dt>${k}</dt><dd>${v}</dd></div>`).join('')}</dl>`
        : ''}
      ${z.note ? `<p class="z-note">${z.note}</p>` : ''}
    </article>`).join('');
}

export function renderPromos(el) {
  el.innerHTML = PROMOS.map((p) => {
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
  }).join('');
}

export function menuItemHtml(it) {
  return `<div class="menu-item">
    <span class="m-name">${it.name}${it.note ? `<span class="m-note">${it.note}</span>` : ''}${it.sample ? '<span class="sample-chip">пример</span>' : ''}</span>
    <span class="m-price">${it.dayPrice
      ? `${formatRub(it.dayPrice)} <small>день</small> · ${formatRub(it.nightPrice)} <small>ночь</small>`
      : formatRub(it.price)}</span>
  </div>`;
}

export function renderMenu(el) {
  el.innerHTML = MENU.sections.map((s) => `
    <div class="menu-col">
      <h3>${s.name}</h3>
      ${s.items.map(menuItemHtml).join('')}
    </div>`).join('');
}

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

export function renderPrices({ dayEl, nightEl, dayBtn, nightBtn }) {
  dayEl.innerHTML = priceTable([
    { name: '1 час', prices: PRICING.hourly.day.prices },
    { name: 'Пакет «День»', note: '10:00–18:00', prices: PRICING.packages.find((p) => p.id === 'day').prices, pkg: true },
  ]);
  nightEl.innerHTML = priceTable([
    { name: '1 час', prices: PRICING.hourly.night.prices },
    ...PRICING.nightBundles.map((b) => ({ name: b.name[0].toUpperCase() + b.name.slice(1), prices: b.prices })),
    ...PRICING.packages.filter((p) => p.id !== 'day').map((p) => ({
      name: `Пакет «${p.name}»`,
      note: `${p.from}:00–${p.to}:00`,
      prices: p.prices,
      pkg: true,
    })),
  ]);
  const setWindow = (win) => {
    dayBtn.setAttribute('aria-pressed', String(win === 'day'));
    nightBtn.setAttribute('aria-pressed', String(win === 'night'));
    dayEl.hidden = win !== 'day';
    nightEl.hidden = win !== 'night';
  };
  dayBtn.addEventListener('click', () => setWindow('day'));
  nightBtn.addEventListener('click', () => setWindow('night'));
  setWindow(rateWindow(new Date().getHours()));
}

export function renderClubCards(el) {
  el.innerHTML = CLUBS.map((c) => {
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

export function renderRules(el) {
  el.innerHTML = `
    <details>
      <summary>В клубе запрещено — ${RULES.length} ${plural(RULES.length, 'пункт', 'пункта', 'пунктов')}</summary>
      <p class="rules-intro">Правила действуют в обоих клубах сети. Полная версия — на ресепшене.</p>
      <div class="rules-body"><ol>${RULES.map((r) => `<li>${r}</li>`).join('')}</ol></div>
    </details>`;
}

// Карта Leaflet (вендоренный). Тайлы CARTO подтягиваются в проде; без них — тёмное поле с пинами.
let mapStarted = false;
export function initMapLazy(boxSelector = '.map-box') {
  const mapBox = document.querySelector(boxSelector);
  if (!mapBox) return;
  const start = () => {
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
  };
  new IntersectionObserver((entries, obs) => {
    if (entries[0].isIntersecting) {
      start();
      if (mapStarted) obs.disconnect();
    }
  }, { rootMargin: '200px' }).observe(mapBox);
  window.addEventListener('load', () => {
    const r = mapBox.getBoundingClientRect();
    if (r.top < window.innerHeight + 200) start();
  });
}
