// Свободен ли оператор на дату матча. Считается по ПОДТВЕРЖДЕННЫМ заявкам
// (status=confirmed) на каталожные матчи. Любая проблема → «свободно»:
// проверка информирует, а не блокирует заявку.

import { isSlotFree } from '../assets/slots.js';
import { SLOT } from '../assets/data.js';
import { sbConfigured, sbSelect } from './_lib/supabase.js';
import { tooMany, clientIp } from './_lib/ratelimit.js';

export default async function handler(req, res) {
  if (req.method !== 'GET') {
    res.setHeader('Allow', 'GET');
    return res.status(405).json({ ok: false, error: 'method' });
  }
  res.setHeader('Cache-Control', 'no-store');

  const q = req.query || {};
  const startsAt = String(q.starts_at || '');
  if (!startsAt || Number.isNaN(Date.parse(startsAt))) {
    return res.status(400).json({ ok: false, error: 'validation' });
  }

  if (tooMany(`avail:${clientIp(req)}`, 30, 60_000) || !sbConfigured()) {
    return res.status(200).json({ ok: true, free: true });
  }

  try {
    const rows = await sbSelect('requests',
      'select=matches(starts_at,duration_min)&status=eq.confirmed&match_id=not.is.null');
    const busy = (rows || []).map((r) => r.matches).filter(Boolean);
    const free = isSlotFree(busy, startsAt, Number(q.duration_min) || SLOT.defaultDurationMin);
    return res.status(200).json({ ok: true, free });
  } catch (e) {
    console.warn('[availability] Supabase недоступен:', e && e.message);
    return res.status(200).json({ ok: true, free: true });
  }
}
