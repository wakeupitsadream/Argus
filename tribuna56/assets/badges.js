// Эмблемки клубов: SVG-щит с монограммой. Детерминированы от названия
// (никакого Math.random) — один и тот же клуб всегда одного цвета.
// Чистые функции без DOM.

// Палитра щитов — спокойные «клубные» пары фон/кант, читаются в обеих темах.
const PALETTE = [
  ['#0d5cc7', '#083f85'], // синий
  ['#b02a37', '#7d1d27'], // бордовый
  ['#146c43', '#0d4a2e'], // зеленый
  ['#b8860b', '#8a6508'], // золото
  ['#5f3dc4', '#432c8c'], // фиолетовый
  ['#0e7490', '#0a5568'], // морской
  ['#c2410c', '#8f2f09'], // оранжевый
  ['#334155', '#1e293b'], // графит
];

export function teamHash(name) {
  const s = String(name || '').toLowerCase().replace(/ё/g, 'е').replace(/\s+/g, ' ').trim();
  let h = 5381;
  for (let i = 0; i < s.length; i++) h = ((h * 33) ^ s.charCodeAt(i)) >>> 0;
  return h;
}

// «Юниор-2012» → «Ю», «Белые Тигры» → «БТ»
export function teamInitials(name) {
  const words = String(name || '')
    .replace(/[«»"']/g, ' ')
    .split(/[\s-]+/)
    .filter((w) => /^[А-Яа-яЁёA-Za-z]/.test(w));
  if (!words.length) return '?';
  const initials = words.slice(0, 2).map((w) => w[0].toUpperCase()).join('');
  return initials || '?';
}

// SVG-щит (inline, size px). aria-hidden: имя команды всегда есть текстом рядом.
export function teamBadge(name, size = 22) {
  const [bg, edge] = PALETTE[teamHash(name) % PALETTE.length];
  const initials = teamInitials(name);
  const fontSize = initials.length > 1 ? 9 : 11;
  return `<svg class="club-badge" width="${size}" height="${size}" viewBox="0 0 24 26" aria-hidden="true">` +
    `<path d="M12 1l9 3v9c0 6-4.2 10-9 12C7.2 23 3 19 3 13V4z" fill="${bg}" stroke="${edge}" stroke-width="1.4"/>` +
    `<path d="M12 3.2l6.8 2.3v7.3c0 4.7-3.2 7.9-6.8 9.6z" fill="#ffffff" fill-opacity=".08"/>` +
    `<text x="12" y="15.5" text-anchor="middle" font-family="Arial, sans-serif" font-weight="800" font-size="${fontSize}" fill="#fff">${initials}</text>` +
    `</svg>`;
}

// Пара эмблем «хозяева + гости» с легким наложением.
export function teamBadgePair(home, away, size = 22) {
  return `<span class="badge-pair">${teamBadge(home, size)}${teamBadge(away, size)}</span>`;
}
