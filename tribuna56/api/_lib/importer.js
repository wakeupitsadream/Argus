// Оркестратор импорта: адаптеры → нормализация → дедупликация → очередь
// подтверждения. Ничего не публикует сам: новое и изменившееся попадает
// в import_queue и ждет решения админа.
//
// Дедупликация в три ступени:
//  1) точная по (source, source_key) против matches: без изменений — skip,
//     изменилось время — в очередь с kind:'update' (approve патчит матч);
//  2) очередь: kind:'new' вставляется с ignore-duplicates — отклоненные
//     строки остаются «надгробиями» и не воскресают; kind:'update' —
//     merge-duplicates: строка возвращается в pending с новым payload;
//  3) fuzzy против матчей других источников/ручных — предупреждение админу
//     (possible_duplicate_of), решение за человеком.

import { ADAPTERS } from './adapters/index.js';
import { sbSelect, sbInsert } from './supabase.js';
import { isSameMatch, fallbackSourceKey } from './dedupe.js';
import { SLOT } from '../../assets/data.js';

const dbToNorm = (m) => ({
  sport: m.sport, teamHome: m.team_home, teamAway: m.team_away, startsAt: m.starts_at,
});

function queueRow(source, sourceKey, kind, normalized, existingMatchId, possibleDuplicateOf) {
  return {
    source,
    source_key: sourceKey,
    kind,
    status: 'pending',
    payload: {
      normalized,
      existing_match_id: existingMatchId ?? null,
      possible_duplicate_of: possibleDuplicateOf ?? null,
    },
  };
}

// Матч каталога из normalized — используется при approve.
export function normalizedToRow(n, source) {
  return {
    sport: n.sport || 'other',
    league: n.league || null,
    age_group: n.ageGroup || null,
    team_home: n.teamHome,
    team_away: n.teamAway,
    venue: n.venue || null,
    address: n.address || null,
    starts_at: new Date(n.startsAt).toISOString(),
    duration_min: SLOT.defaultDurationMin,
    status: 'scheduled',
    source,
    source_key: n.sourceKey,
    published: true,
  };
}

export async function runImport({ now = new Date(), adapters = ADAPTERS } = {}) {
  const report = { bySource: [], queuedNew: 0, queuedUpdates: 0 };

  // Существующие матчи от «вчера» — база для дедупликации.
  // Этот запрос заодно keep-alive для бесплатного Supabase (см. README).
  const existing = await sbSelect('matches',
    'select=id,sport,team_home,team_away,starts_at,venue,source,source_key' +
    `&starts_at=gte.${new Date(now.getTime() - 86400_000).toISOString()}&limit=1000`) || [];

  for (const adapter of adapters) {
    const entry = { source: adapter.id, label: adapter.label, fetched: 0, queued: 0, updates: 0, skipped: 0, errors: [] };
    try {
      const list = await adapter.fetchMatches({ now }) || [];
      entry.fetched = list.length;
      const newRows = [];
      const updateRows = [];

      for (const n of list) {
        if (!n || !n.teamHome || !n.teamAway || Number.isNaN(Date.parse(n.startsAt))) {
          entry.skipped++;
          continue;
        }
        const sourceKey = String(n.sourceKey || fallbackSourceKey(n)).slice(0, 300);
        const normalized = { ...n, sourceKey };

        const same = existing.find((m) => m.source === adapter.id && m.source_key === sourceKey);
        if (same) {
          const timeChanged = Math.abs(Date.parse(same.starts_at) - Date.parse(n.startsAt)) > 60_000;
          const venueChanged = Boolean(n.venue) && Boolean(same.venue) && n.venue !== same.venue;
          if (!timeChanged && !venueChanged) {
            entry.skipped++;
            continue;
          }
          updateRows.push(queueRow(adapter.id, sourceKey, 'update', normalized, same.id));
          entry.updates++;
          continue;
        }

        const fuzzy = existing.find((m) => m.source !== adapter.id && isSameMatch(dbToNorm(m), normalized));
        newRows.push(queueRow(adapter.id, sourceKey, 'new', normalized, null, fuzzy ? fuzzy.id : null));
        entry.queued++;
      }

      if (newRows.length) {
        const inserted = await sbInsert('import_queue', newRows,
          { onConflict: 'source,source_key', ignoreDuplicates: true }) || [];
        entry.queued = inserted.length; // фактически новые: надгробия и уже стоящие в очереди не считаем
        report.queuedNew += inserted.length;
      } else {
        entry.queued = 0;
      }
      if (updateRows.length) {
        await sbInsert('import_queue', updateRows,
          { onConflict: 'source,source_key', ignoreDuplicates: false });
        report.queuedUpdates += updateRows.length;
      }
    } catch (e) {
      entry.errors.push(String((e && e.message) || e));
    }
    report.bySource.push(entry);
  }

  return report;
}
