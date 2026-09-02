// Табло для OBS: одно «табло» = строка в scoreboards.
//  GET  ?id=abc123          → {ok, data, updatedAt} — публичное чтение (оверлей).
//  POST {id, token, data}   → создать/обновить. Первая запись фиксирует token
//                             (пульт генерирует его сам), дальше token обязан
//                             совпадать — чужой пульт получит 403.
// БД не настроена/упала → {ok:false, error:'unavailable'} со статусом 200:
// пульт подскажет локальный режим, оверлей просто не обновится.

import { sbConfigured, sbSelect, sbInsert, sbUpdate } from './_lib/supabase.js';
import { tooMany, clientIp } from './_lib/ratelimit.js';
import { sanitizeState } from '../assets/scoreboard-core.js';

const ID_RE = /^[a-z0-9]{4,24}$/;
const TOKEN_RE = /^[a-z0-9]{8,64}$/;
const noCache = (res) => res.setHeader('Cache-Control', 'no-store');

export default async function handler(req, res) {
  noCache(res);

  if (!sbConfigured()) {
    console.warn('[scoreboard] БД не настроена (DATABASE_URL/SUPABASE_*)');
    return res.status(200).json({ ok: false, error: 'unavailable' });
  }

  try {
    if (req.method === 'GET') {
      const id = String((req.query || {}).id || '').toLowerCase();
      if (!ID_RE.test(id)) return res.status(400).json({ ok: false, error: 'bad_id' });
      const rows = await sbSelect('scoreboards', `select=data,updated_at&id=eq.${id}&limit=1`);
      if (!rows || !rows.length) return res.status(404).json({ ok: false, error: 'not_found' });
      // serverNow: клиенты считают offset и ведут часы по времени сервера —
      // телефон пульта и машина OBS могут расходиться на десятки секунд.
      return res.status(200).json({
        ok: true,
        data: sanitizeState(rows[0].data, Date.now()),
        updatedAt: rows[0].updated_at,
        serverNow: Date.now(),
      });
    }

    if (req.method === 'POST') {
      // пульт шлет апдейт на каждый клик (с дебаунсом) — лимит щедрый
      if (tooMany(`sb:${clientIp(req)}`, 240, 60_000)) {
        return res.status(200).json({ ok: false, error: 'rate' });
      }
      const b = req.body || {};
      const id = String(b.id || '').toLowerCase();
      const token = String(b.token || '');
      if (!ID_RE.test(id)) return res.status(400).json({ ok: false, error: 'bad_id' });
      if (!TOKEN_RE.test(token)) return res.status(400).json({ ok: false, error: 'bad_token' });
      if (JSON.stringify(b.data || {}).length > 8000) {
        return res.status(400).json({ ok: false, error: 'too_big' });
      }
      const data = sanitizeState(b.data, Date.now());

      const rows = await sbSelect('scoreboards', `select=token,data&id=eq.${id}&limit=1`);
      if (rows && rows.length) {
        if (rows[0].token !== token) return res.status(403).json({ ok: false, error: 'forbidden' });
        // защита от out-of-order: отставший дебаунс-POST не должен затирать
        // более свежую запись (rev — монотонный счетчик правок пульта)
        const storedRev = Number((rows[0].data || {}).rev) || 0;
        if (data.rev <= storedRev) {
          return res.status(200).json({ ok: true, stale: true, serverNow: Date.now() });
        }
        await sbUpdate('scoreboards', `id=eq.${id}`, { data, updated_at: new Date().toISOString() });
      } else {
        // гонка двух первых записей: INSERT .. DO NOTHING, затем перечитать
        // и сверить token — id не может быть «захвачен» тихо
        await sbInsert('scoreboards', [{ id, token, data }],
          { returning: 'minimal', onConflict: 'id', ignoreDuplicates: true });
        const check = await sbSelect('scoreboards', `select=token&id=eq.${id}&limit=1`);
        if (!check || !check.length || check[0].token !== token) {
          return res.status(403).json({ ok: false, error: 'forbidden' });
        }
      }
      return res.status(200).json({ ok: true, serverNow: Date.now() });
    }

    res.setHeader('Allow', 'GET, POST');
    return res.status(405).json({ ok: false, error: 'method' });
  } catch (e) {
    console.warn('[scoreboard] БД недоступна:', e && e.message);
    return res.status(200).json({ ok: false, error: 'unavailable' });
  }
}
