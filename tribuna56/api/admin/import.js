// Админка: очередь импорта. GET — очередь, POST — запустить импорт сейчас,
// PATCH — approve (создать/обновить матч) или reject («надгробие» остается
// в очереди и не дает матчу воскреснуть при следующем прогоне).

import { checkAdmin } from '../_lib/auth.js';
import { readBody, methodNotAllowed } from '../_lib/http.js';
import { sbConfigured, sbSelect, sbInsert, sbUpdate } from '../_lib/supabase.js';
import { runImport, normalizedToRow } from '../_lib/importer.js';

const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

async function approve(item) {
  const n = item.payload && item.payload.normalized;
  if (!n) throw new Error('payload без normalized');

  let matchId = null;
  if (item.kind === 'update' && item.payload.existing_match_id) {
    matchId = item.payload.existing_match_id;
    const patch = {
      starts_at: new Date(n.startsAt).toISOString(),
      venue: n.venue || null,
    };
    const rows = await sbUpdate('matches', `id=eq.${matchId}`, patch);
    if (!rows || !rows.length) throw new Error(`матч #${matchId} для обновления не найден`);
  } else {
    const rows = await sbInsert('matches', [normalizedToRow(n, item.source)]);
    matchId = rows[0].id;
  }

  await sbUpdate('import_queue', `id=eq.${item.id}`, {
    status: 'approved',
    match_id: matchId,
    decided_at: new Date().toISOString(),
  });
  return matchId;
}

export default async function handler(req, res) {
  if (!checkAdmin(req, res)) return;
  if (!sbConfigured()) return res.status(502).json({ ok: false, error: 'db_not_configured' });

  const q = req.query || {};

  try {
    if (req.method === 'GET') {
      const p = new URLSearchParams();
      p.set('select', '*');
      p.set('order', 'created_at.asc');
      p.set('limit', '200');
      p.append('status', `eq.${['pending', 'approved', 'rejected'].includes(q.status) ? q.status : 'pending'}`);
      const rows = await sbSelect('import_queue', p.toString());
      return res.status(200).json({ ok: true, queue: rows || [] });
    }

    if (req.method === 'POST') {
      const report = await runImport();
      return res.status(200).json({ ok: true, report });
    }

    if (req.method === 'PATCH') {
      const id = String(q.id || '');
      const { action } = readBody(req);
      if (!UUID_RE.test(id) || !['approve', 'reject'].includes(action)) {
        return res.status(400).json({ ok: false, error: 'validation' });
      }
      const rows = await sbSelect('import_queue', `select=*&id=eq.${id}&limit=1`);
      const item = rows && rows[0];
      if (!item) return res.status(404).json({ ok: false, error: 'not_found' });
      if (item.status !== 'pending') return res.status(400).json({ ok: false, error: 'already_decided' });

      if (action === 'reject') {
        await sbUpdate('import_queue', `id=eq.${id}`, {
          status: 'rejected',
          decided_at: new Date().toISOString(),
        });
        return res.status(200).json({ ok: true });
      }
      const matchId = await approve(item);
      return res.status(200).json({ ok: true, match_id: matchId });
    }

    return methodNotAllowed(res, 'GET, POST, PATCH');
  } catch (e) {
    console.warn('[admin/import]', e && e.message);
    return res.status(502).json({ ok: false, error: 'db' });
  }
}
