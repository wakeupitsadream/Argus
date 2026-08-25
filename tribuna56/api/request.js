// Заявка с сайта → Supabase + Telegram владельца.
// Паттерн эталона ARGUS: honeypot, rate-limit, молчаливая деградация —
// клиент ВСЕГДА получает «принято», проблемы уходят в console.warn.

import { validateRequest } from '../assets/validate.js';
import { quoteServices, formatRub } from '../assets/pricing.js';
import { formatMatchDate } from '../assets/format.js';
import { BRAND, sportLabel, CONTACT_CHANNELS } from '../assets/data.js';
import { sbConfigured, sbSelect, sbInsert } from './_lib/supabase.js';
import { sendTelegram } from './_lib/telegram.js';
import { tooMany, clientIp } from './_lib/ratelimit.js';

export default async function handler(req, res) {
  if (req.method !== 'POST') {
    res.setHeader('Allow', 'POST');
    return res.status(405).json({ ok: false, error: 'method' });
  }

  let body = req.body;
  if (typeof body === 'string') {
    try { body = JSON.parse(body); } catch { body = {}; }
  }
  body = body || {};

  // honeypot: поле «website» заполняют только боты — молча отвечаем «принято»
  if (body.website) return res.status(200).json({ ok: true });

  const v = validateRequest(body);
  if (!v.ok) return res.status(400).json({ ok: false, error: 'validation', fields: v.errors });

  if (tooMany(`request:${clientIp(req)}`, 5, 60_000)) {
    return res.status(200).json({ ok: true });
  }

  // Матч из каталога — для текста заявки и связки в БД
  let match = null;
  if (v.matchId && sbConfigured()) {
    try {
      const rows = await sbSelect('matches', `select=*&id=eq.${v.matchId}&limit=1`);
      match = (rows && rows[0]) || null;
    } catch (e) {
      console.warn('[request] не удалось прочитать матч:', e && e.message);
    }
  }

  const quote = quoteServices(v.services); // цене с клиента не верим — считаем сами
  const playerNote = String(body.player_note || '').trim().slice(0, 200);
  const comment = String(body.comment || '').trim().slice(0, 500);
  const channel = CONTACT_CHANNELS.find((c) => c.id === body.contact_channel) || null;

  let saved = false;
  if (sbConfigured()) {
    try {
      await sbInsert('requests', [{
        match_id: match ? v.matchId : null,
        custom_match: v.customMatch,
        services: v.services,
        player_note: playerNote || null,
        name: v.name,
        phone: v.phone,
        contact_channel: channel ? channel.id : null,
        comment: comment || null,
        price_quote: quote.total,
      }], { returning: 'minimal' });
      saved = true;
    } catch (e) {
      console.warn('[request] Supabase недоступен, заявка не сохранена:', e && e.message);
    }
  }

  const lines = [
    `🎥 Заявка на съемку — ${BRAND.name}`,
    match ? `Матч #${match.id}: ${match.team_home} — ${match.team_away}` : null,
    match ? [sportLabel(match.sport), match.age_group, match.league].filter(Boolean).join(', ') : null,
    match ? `Когда: ${formatMatchDate(match.starts_at)}` : null,
    match && match.venue ? `Где: ${match.venue}` : null,
    !match && v.matchId ? `Матч: #${v.matchId} (не найден в каталоге!)` : null,
    v.customMatch ? `Матч (вручную): ${v.customMatch.teams}` : null,
    v.customMatch && v.customMatch.sport ? `Спорт: ${sportLabel(v.customMatch.sport)}` : null,
    v.customMatch ? `Когда: ${v.customMatch.date_text}` : null,
    v.customMatch && v.customMatch.venue ? `Где: ${v.customMatch.venue}` : null,
    `Услуги: ${quote.items.map((i) => i.label).join(' + ')}`,
    quote.discount ? `Скидка: −${formatRub(quote.discount)} (${quote.discountLabel})` : null,
    `Расчет: ${formatRub(quote.total)}`,
    playerNote ? `Игрок: ${playerNote}` : null,
    `Имя: ${v.name}`,
    `Телефон: ${v.phone}`,
    channel ? `Связь: ${channel.label}` : null,
    comment ? `Комментарий: ${comment}` : null,
    saved ? null : '⚠️ БД недоступна — заявка есть только в этом сообщении',
  ].filter(Boolean);

  await sendTelegram(lines.join('\n'));
  return res.status(200).json({ ok: true });
}
