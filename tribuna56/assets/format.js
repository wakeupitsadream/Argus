// Даты по Оренбургу (UTC+5) и разбор ссылок VK Видео.
// Чистые функции без DOM: используются в браузере, serverless-функциях и тестах.

export const TZ = 'Asia/Yekaterinburg'; // UTC+5, совпадает с оренбургским временем

// 'YYYY-MM-DD' даты по оренбургскому времени — ключ группировки и дедупликации.
export function dateKey(iso) {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return null;
  return new Intl.DateTimeFormat('en-CA', {
    timeZone: TZ, year: 'numeric', month: '2-digit', day: '2-digit',
  }).format(d);
}

export function formatTime(iso) {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return '';
  return new Intl.DateTimeFormat('ru-RU', {
    timeZone: TZ, hour: '2-digit', minute: '2-digit',
  }).format(d);
}

// «суббота, 14 сентября» / «Сегодня» / «Завтра». now передается для тестируемости.
export function formatDayLabel(iso, nowIso) {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return '';
  const key = dateKey(iso);
  if (nowIso) {
    const now = new Date(nowIso);
    if (key === dateKey(now.toISOString())) return 'Сегодня';
    if (key === dateKey(new Date(now.getTime() + 86400_000).toISOString())) return 'Завтра';
  }
  const label = new Intl.DateTimeFormat('ru-RU', {
    timeZone: TZ, weekday: 'long', day: 'numeric', month: 'long',
  }).format(d);
  return label.charAt(0).toUpperCase() + label.slice(1);
}

// «сб, 14 сентября · 12:00» — компактная строка для карточек и Telegram-заявок.
export function formatMatchDate(iso) {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return '';
  const day = new Intl.DateTimeFormat('ru-RU', {
    timeZone: TZ, weekday: 'short', day: 'numeric', month: 'long',
  }).format(d);
  return `${day} · ${formatTime(iso)}`;
}

const VK_HOSTS = new Set([
  'vk.com', 'www.vk.com', 'm.vk.com', 'vk.ru', 'www.vk.ru',
  'vkvideo.ru', 'www.vkvideo.ru', 'm.vkvideo.ru',
]);

// Ссылка VK Видео → src для iframe-плеера. Непонятная ссылка → null (покажем кнопку «Смотреть в VK»).
export function vkEmbedUrl(url) {
  const raw = String(url || '').trim();
  if (!raw) return null;
  let parsed;
  try {
    parsed = new URL(raw.includes('://') ? raw : `https://${raw}`);
  } catch {
    return null;
  }
  if (!VK_HOSTS.has(parsed.hostname)) return null;
  // Готовый embed-src (из кнопки «Поделиться» VK) пропускаем как есть.
  if (parsed.pathname.includes('video_ext.php')) {
    parsed.protocol = 'https:';
    return parsed.href;
  }
  // vk.com/video-123_456, vkvideo.ru/video-123_456, ?z=video-123_456…
  const m = parsed.href.match(/video(-?\d+)_(\d+)/);
  if (!m) return null;
  return `https://vk.com/video_ext.php?oid=${m[1]}&id=${m[2]}&hd=2`;
}
