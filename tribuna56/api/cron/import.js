// Ежедневный cron Vercel (vercel.json): импорт расписаний + keep-alive
// бесплатного Supabase. Vercel сам шлет Authorization: Bearer CRON_SECRET.
// Появились новые матчи в очереди — владельцу уходит пинг в Telegram.

import { sbConfigured } from '../_lib/supabase.js';
import { sendTelegram } from '../_lib/telegram.js';
import { runImport } from '../_lib/importer.js';
import { plural } from '../../assets/pricing.js';
import { BRAND } from '../../assets/data.js';

export default async function handler(req, res) {
  const secret = process.env.CRON_SECRET;
  const auth = String(req.headers['authorization'] || '');
  if (!secret || auth !== `Bearer ${secret}`) {
    return res.status(401).json({ ok: false, error: 'auth' });
  }
  if (!sbConfigured()) {
    console.warn('[cron/import] Supabase не настроен — импорт пропущен');
    return res.status(200).json({ ok: false, error: 'db_not_configured' });
  }

  try {
    const report = await runImport();
    const fresh = report.queuedNew + report.queuedUpdates;
    if (fresh > 0) {
      await sendTelegram(
        `📥 ${BRAND.name}: импорт нашел ${fresh} ${plural(fresh, 'матч', 'матча', 'матчей')} — ждут подтверждения в админке.`,
      );
    }
    return res.status(200).json({ ok: true, report });
  } catch (e) {
    console.warn('[cron/import] импорт упал:', e && e.message);
    return res.status(200).json({ ok: false, error: 'import_failed' });
  }
}
