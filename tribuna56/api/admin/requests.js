// Админка: заявки со статусами. Bearer ADMIN_TOKEN.

import { checkAdmin } from '../_lib/auth.js';
import { readBody, methodNotAllowed } from '../_lib/http.js';
import { sbConfigured, sbSelect, sbUpdate } from '../_lib/supabase.js';

const STATUSES = ['new', 'confirmed', 'done', 'declined'];
const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

export default async function handler(req, res) {
  if (!checkAdmin(req, res)) return;
  if (!sbConfigured()) return res.status(502).json({ ok: false, error: 'db_not_configured' });

  const q = req.query || {};

  try {
    if (req.method === 'GET') {
      const p = new URLSearchParams();
      p.set('select', '*,matches(*)'); // PostgREST embed: заявка вместе с матчем
      p.set('order', 'created_at.desc');
      p.set('limit', String(Math.min(Number(q.limit) || 300, 500)));
      if (q.status && STATUSES.includes(q.status)) p.append('status', `eq.${q.status}`);
      const rows = await sbSelect('requests', p.toString());
      return res.status(200).json({ ok: true, requests: rows || [] });
    }

    if (req.method === 'PATCH') {
      const id = String(q.id || '');
      if (!UUID_RE.test(id)) return res.status(400).json({ ok: false, error: 'validation' });
      const body = readBody(req);
      if (!STATUSES.includes(body.status)) return res.status(400).json({ ok: false, error: 'validation' });
      const rows = await sbUpdate('requests', `id=eq.${id}`, { status: body.status });
      if (!rows || !rows.length) return res.status(404).json({ ok: false, error: 'not_found' });
      return res.status(200).json({ ok: true, request: rows[0] });
    }

    return methodNotAllowed(res, 'GET, PATCH');
  } catch (e) {
    console.warn('[admin/requests]', e && e.message);
    return res.status(502).json({ ok: false, error: 'db' });
  }
}
