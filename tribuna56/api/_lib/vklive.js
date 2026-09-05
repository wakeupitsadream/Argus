// Автодетект эфира в группе VK (vk.com/tribuna_plus): если задан
// VK_SERVICE_TOKEN, при обращениях к каталогу — не чаще раза в минуту
// на инстанс — спрашиваем у VK свежие видео группы. Началась трансляция →
// ближайший запланированный матч каталога переводится в live со ссылкой
// на эфир; трансляция закончилась → матч переводится в finished (и попадает
// в архив портфолио). Любая ошибка VK молча пропускается — каталог важнее.
//
// Токен: СЕРВИСНЫЙ ключ приложения VK (группа публичная, ее открытые видео
// сервисному ключу видны). Ручное управление из админки всегда сильнее:
// автодетект трогает только матчи в статусах scheduled (→live) и live
// с нашей же авто-ссылкой (→finished).

import { sbSelect, sbUpdate } from './supabase.js';

export const VK_GROUP_ID = Number(process.env.VK_GROUP_ID) || 241086908; // vk.com/tribuna_plus

// Окно привязки эфира к матчу: стрим включают незадолго до начала игры
// или во время нее (матч ~1.5-2 часа).
const BEFORE_MS = 45 * 60_000;       // эфир не раньше чем за 45 мин до матча
const AFTER_MS = 3 * 3600_000;       // и не позже чем через 3 часа после начала

export const vkVideoUrl = (groupId, videoId) => `https://vk.com/video-${groupId}_${videoId}`;

// Ближайший подходящий запланированный матч для начавшегося эфира.
export function pickMatchForLive(matches, nowMs) {
  const candidates = (matches || []).filter((m) => {
    if (m.status !== 'scheduled') return false;
    const t = Date.parse(m.starts_at);
    return Number.isFinite(t) && nowMs >= t - BEFORE_MS && nowMs <= t + AFTER_MS;
  });
  candidates.sort((a, b) =>
    Math.abs(Date.parse(a.starts_at) - nowMs) - Math.abs(Date.parse(b.starts_at) - nowMs));
  return candidates[0] || null;
}

// Чистая логика переходов: (матчи, видео группы) → что обновить в БД.
// videos: [{id, live, live_status}] из video.get; live_status:
// 'started' — идет, 'finished'/'failed' — закончилась, 'waiting'/'upcoming' — анонс.
export function planLiveTransitions(matches, videos, groupId, nowMs) {
  const setLive = [];
  const setFinished = [];
  const byUrl = new Map((videos || []).map((v) => [vkVideoUrl(groupId, v.id), v]));

  const started = (videos || []).filter((v) => v.live && v.live_status === 'started');
  for (const v of started) {
    const url = vkVideoUrl(groupId, v.id);
    // этот эфир уже привязан к какому-то матчу — ничего не делаем
    if ((matches || []).some((m) => m.status === 'live' && m.stream_url === url)) continue;
    const match = pickMatchForLive(matches, nowMs);
    if (match) setLive.push({ id: match.id, stream_url: url });
  }

  for (const m of matches || []) {
    if (m.status !== 'live' || !m.stream_url) continue;
    const v = byUrl.get(m.stream_url);
    // завершаем ТОЛЬКО по явному сигналу VK о конце эфира: видео не из
    // нашей группы или выпавшее из свежей выборки не трогаем (это мог
    // быть ручной LIVE админа с другой ссылкой)
    if (v && v.live && (v.live_status === 'finished' || v.live_status === 'failed')) {
      setFinished.push(m.id);
    }
  }
  return { setLive, setFinished };
}

let lastCheckAt = 0;
let warned = false;

// Вызывается из /api/matches при каждом запросе каталога; сам решает,
// пора ли (throttle 60с на инстанс). Никогда не бросает.
export async function maybeCheckVkLive(now = Date.now()) {
  const token = process.env.VK_SERVICE_TOKEN;
  if (!token) return;
  if (now - lastCheckAt < 60_000) return;
  lastCheckAt = now; // и при ошибке тоже: не молотим VK на каждый запрос

  try {
    const vkUrl = `https://api.vk.com/method/video.get?owner_id=-${VK_GROUP_ID}&count=20&v=5.199&access_token=${encodeURIComponent(token)}`;
    const r = await fetch(vkUrl);
    const j = await r.json();
    if (j.error) {
      if (!warned) console.warn('[vklive] VK API:', j.error.error_code, j.error.error_msg);
      warned = true;
      return;
    }
    warned = false;
    const videos = (j.response && j.response.items) || [];
    if (!videos.some((v) => v.live)) return; // эфиров нет и не было — тишина

    // матчи вокруг «сейчас»: кандидаты на live + текущие live для finished
    const p = new URLSearchParams();
    p.set('select', 'id,status,starts_at,stream_url');
    p.set('published', 'is.true');
    p.append('starts_at', `gte.${new Date(now - 12 * 3600_000).toISOString()}`);
    p.append('starts_at', `lte.${new Date(now + 6 * 3600_000).toISOString()}`);
    const matches = await sbSelect('matches', p.toString());

    const { setLive, setFinished } = planLiveTransitions(matches, videos, VK_GROUP_ID, now);
    for (const u of setLive) {
      console.log('[vklive] эфир начался → матч', u.id, u.stream_url);
      await sbUpdate('matches', `id=eq.${u.id}&status=eq.scheduled`,
        { status: 'live', stream_url: u.stream_url });
    }
    for (const id of setFinished) {
      console.log('[vklive] эфир завершен → матч', id);
      await sbUpdate('matches', `id=eq.${id}&status=eq.live`, { status: 'finished' });
    }
  } catch (e) {
    if (!warned) console.warn('[vklive] сбой проверки:', e && e.message);
    warned = true;
  }
}
