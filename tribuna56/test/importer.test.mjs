import test from 'node:test';
import assert from 'node:assert/strict';
import { runImport } from '../api/_lib/importer.js';

const NOW = new Date('2026-09-10T12:00:00+05:00');

function adapter(id, matches, { throws = false } = {}) {
  return {
    id,
    label: id,
    async fetchMatches() {
      if (throws) throw new Error('источник недоступен');
      return matches;
    },
  };
}

const N1 = {
  sourceKey: 'ext-1',
  sport: 'football',
  league: 'Первенство области',
  ageGroup: '2012 г.р.',
  teamHome: 'Газовик-2012',
  teamAway: 'Факел-2012',
  venue: 'Стадион «Газовик»',
  startsAt: '2026-09-12T12:00:00+05:00',
  raw: {},
};

async function withSb(existingRows, fn) {
  const savedUrl = process.env.SUPABASE_URL;
  const savedKey = process.env.SUPABASE_SERVICE_KEY;
  process.env.SUPABASE_URL = 'https://sb.test';
  process.env.SUPABASE_SERVICE_KEY = 'k';
  const calls = [];
  const orig = globalThis.fetch;
  globalThis.fetch = async (url, opts = {}) => {
    const u = String(url);
    calls.push({ url: u, opts });
    let body = '[]';
    if (u.includes('/rest/v1/matches') && (opts.method || 'GET') === 'GET') {
      body = JSON.stringify(existingRows);
    } else if (u.includes('/rest/v1/import_queue')) {
      body = opts.body; // эхо: «вставилось всё»
    }
    return { ok: true, status: 200, text: async () => body, json: async () => JSON.parse(body) };
  };
  try {
    return await fn(calls);
  } finally {
    globalThis.fetch = orig;
    if (savedUrl === undefined) delete process.env.SUPABASE_URL; else process.env.SUPABASE_URL = savedUrl;
    if (savedKey === undefined) delete process.env.SUPABASE_SERVICE_KEY; else process.env.SUPABASE_SERVICE_KEY = savedKey;
  }
}

const queueCalls = (calls) => calls.filter((c) => c.url.includes('/rest/v1/import_queue'));

test('пустой реестр: без ошибок, ноль в очереди, keep-alive запрос был', async () => {
  await withSb([], async (calls) => {
    const report = await runImport({ now: NOW, adapters: [] });
    assert.deepEqual(report.bySource, []);
    assert.equal(report.queuedNew, 0);
    assert.equal(calls.length, 1); // select существующих матчей
  });
});

test('новый матч попадает в очередь с ignore-duplicates', async () => {
  await withSb([], async (calls) => {
    const report = await runImport({ now: NOW, adapters: [adapter('src', [N1])] });
    assert.equal(report.queuedNew, 1);
    const [q] = queueCalls(calls);
    assert.ok(q.url.includes('on_conflict=source%2Csource_key'));
    assert.match(q.opts.headers.Prefer, /resolution=ignore-duplicates/);
    const row = JSON.parse(q.opts.body)[0];
    assert.equal(row.kind, 'new');
    assert.equal(row.source_key, 'ext-1');
    assert.equal(row.payload.normalized.teamHome, 'Газовик-2012');
  });
});

test('упавший адаптер не валит остальные', async () => {
  await withSb([], async () => {
    const report = await runImport({
      now: NOW,
      adapters: [adapter('bad', [], { throws: true }), adapter('good', [N1])],
    });
    assert.equal(report.bySource.length, 2);
    assert.equal(report.bySource[0].errors.length, 1);
    assert.equal(report.bySource[1].errors.length, 0);
    assert.equal(report.queuedNew, 1);
  });
});

test('существующий source_key без изменений → skip, очередь не трогаем', async () => {
  const existing = [{
    id: 7, sport: 'football', team_home: 'Газовик-2012', team_away: 'Факел-2012',
    starts_at: new Date('2026-09-12T12:00:00+05:00').toISOString(),
    venue: 'Стадион «Газовик»', source: 'src', source_key: 'ext-1',
  }];
  await withSb(existing, async (calls) => {
    const report = await runImport({ now: NOW, adapters: [adapter('src', [N1])] });
    assert.equal(report.bySource[0].skipped, 1);
    assert.equal(report.queuedNew, 0);
    assert.equal(queueCalls(calls).length, 0);
  });
});

test('сдвиг времени → kind:update с merge-duplicates (строка вернется в pending)', async () => {
  const existing = [{
    id: 7, sport: 'football', team_home: 'Газовик-2012', team_away: 'Факел-2012',
    starts_at: new Date('2026-09-12T15:00:00+05:00').toISOString(),
    venue: 'Стадион «Газовик»', source: 'src', source_key: 'ext-1',
  }];
  await withSb(existing, async (calls) => {
    const report = await runImport({ now: NOW, adapters: [adapter('src', [N1])] });
    assert.equal(report.queuedUpdates, 1);
    const [q] = queueCalls(calls);
    assert.match(q.opts.headers.Prefer, /resolution=merge-duplicates/);
    const row = JSON.parse(q.opts.body)[0];
    assert.equal(row.kind, 'update');
    assert.equal(row.status, 'pending');
    assert.equal(row.payload.existing_match_id, 7);
  });
});

test('fuzzy-совпадение с ручным матчем → possible_duplicate_of', async () => {
  const existing = [{
    id: 12, sport: 'football', team_home: 'ГАЗОВИК 2012', team_away: 'Факел—2012',
    starts_at: new Date('2026-09-12T13:00:00+05:00').toISOString(), // Δ 1 час
    venue: null, source: 'manual', source_key: null,
  }];
  await withSb(existing, async (calls) => {
    const report = await runImport({ now: NOW, adapters: [adapter('src', [N1])] });
    assert.equal(report.queuedNew, 1);
    const row = JSON.parse(queueCalls(calls)[0].opts.body)[0];
    assert.equal(row.payload.possible_duplicate_of, 12);
  });
});

test('битые записи адаптера пропускаются', async () => {
  await withSb([], async () => {
    const report = await runImport({
      now: NOW,
      adapters: [adapter('src', [null, { ...N1, startsAt: 'мусор' }, { ...N1, teamHome: '' }])],
    });
    assert.equal(report.bySource[0].skipped, 3);
    assert.equal(report.queuedNew, 0);
  });
});
