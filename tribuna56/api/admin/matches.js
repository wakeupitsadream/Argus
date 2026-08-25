// Админка: CRUD матчей. Bearer ADMIN_TOKEN. Ошибки здесь показываем явно —
// это инструмент владельца, а не витрина.

import { checkAdmin } from '../_lib/auth.js';
import { readBody, methodNotAllowed } from '../_lib/http.js';
import { sbConfigured, sbSelect, sbInsert, sbUpdate, sbDelete } from '../_lib/supabase.js';
import { SPORT_IDS } from '../../assets/data.js';

const STATUSES = ['scheduled', 'live', 'finished', 'canceled'];

const str = (v, max) => {
  const s = String(v ?? '').trim().slice(0, max);
  return s || null;
};

// Белый список полей + нормализация. requireCore — для создания.
function sanitizeMatch(body, { requireCore = false } = {}) {
  const errors = {};
  const row = {};
  const has = (k) => body[k] !== undefined;

  if (has('sport') || requireCore) {
    row.sport = SPORT_IDS.includes(body.sport) ? body.sport : 'other';
  }
  if (has('league')) row.league = str(body.league, 200);
  if (has('age_group')) row.age_group = str(body.age_group, 60);
  if (has('team_home') || requireCore) {
    row.team_home = str(body.team_home, 120);
    if (!row.team_home) errors.team_home = 'Укажите первую команду';
  }
  if (has('team_away') || requireCore) {
    row.team_away = str(body.team_away, 120);
    if (!row.team_away) errors.team_away = 'Укажите вторую команду';
  }
  if (has('venue')) row.venue = str(body.venue, 200);
  if (has('address')) row.address = str(body.address, 300);
  if (has('starts_at') || requireCore) {
    const t = Date.parse(body.starts_at);
    if (Number.isNaN(t)) errors.starts_at = 'Дата и время матча обязательны';
    else row.starts_at = new Date(t).toISOString();
  }
  if (has('duration_min')) {
    const d = Number(body.duration_min);
    row.duration_min = Number.isFinite(d) ? Math.min(Math.max(Math.round(d), 30), 300) : 90;
  }
  if (has('status')) row.status = STATUSES.includes(body.status) ? body.status : 'scheduled';
  if (has('stream_url')) row.stream_url = str(body.stream_url, 500);
  if (has('highlights_url')) row.highlights_url = str(body.highlights_url, 500);
  if (has('published')) row.published = body.published !== false;

  return { ok: Object.keys(errors).length === 0, row, errors };
}

export default async function handler(req, res) {
  if (!checkAdmin(req, res)) return;
  if (!sbConfigured()) return res.status(502).json({ ok: false, error: 'db_not_configured' });

  const q = req.query || {};
  const id = Number(q.id);

  try {
    if (req.method === 'GET') {
      const p = new URLSearchParams();
      p.set('select', '*');
      p.set('order', 'starts_at.asc');
      p.set('limit', '500');
      if (q.include_past !== '1') {
        p.append('starts_at', `gte.${new Date(Date.now() - 86400_000).toISOString()}`);
      }
      if (q.status && STATUSES.includes(q.status)) p.append('status', `eq.${q.status}`);
      const rows = await sbSelect('matches', p.toString());
      return res.status(200).json({ ok: true, matches: rows || [] });
    }

    if (req.method === 'POST') {
      const { ok, row, errors } = sanitizeMatch(readBody(req), { requireCore: true });
      if (!ok) return res.status(400).json({ ok: false, error: 'validation', fields: errors });
      const rows = await sbInsert('matches', [row]);
      return res.status(200).json({ ok: true, match: rows[0] });
    }

    if (req.method === 'PATCH') {
      if (!Number.isInteger(id) || id <= 0) return res.status(400).json({ ok: false, error: 'validation' });
      const { ok, row, errors } = sanitizeMatch(readBody(req));
      if (!ok) return res.status(400).json({ ok: false, error: 'validation', fields: errors });
      if (!Object.keys(row).length) return res.status(400).json({ ok: false, error: 'validation' });
      const rows = await sbUpdate('matches', `id=eq.${id}`, row);
      if (!rows || !rows.length) return res.status(404).json({ ok: false, error: 'not_found' });
      return res.status(200).json({ ok: true, match: rows[0] });
    }

    if (req.method === 'DELETE') {
      if (!Number.isInteger(id) || id <= 0) return res.status(400).json({ ok: false, error: 'validation' });
      const rows = await sbDelete('matches', `id=eq.${id}`);
      if (!rows || !rows.length) return res.status(404).json({ ok: false, error: 'not_found' });
      return res.status(200).json({ ok: true });
    }

    return methodNotAllowed(res, 'GET, POST, PATCH, DELETE');
  } catch (e) {
    console.warn('[admin/matches]', e && e.message);
    return res.status(502).json({ ok: false, error: 'db' });
  }
}
