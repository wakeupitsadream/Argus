import test from 'node:test';
import assert from 'node:assert/strict';
import { translateSelect, translateInsert, translateUpdate, translateDelete } from '../api/_lib/pg-translate.js';

// Кейсы — реальные запросы хендлеров сайта (grep sbSelect/sbInsert/sbUpdate/sbDelete).

test('select: один матч по id с published', () => {
  const { text, params } = translateSelect('matches', 'select=id,sport,team_home&id=eq.5&published=is.true&limit=1');
  assert.equal(text, 'SELECT "matches"."id", "matches"."sport", "matches"."team_home" FROM "matches" WHERE "matches"."id" = $1 AND "matches"."published" IS TRUE LIMIT 1');
  assert.deepEqual(params, ['5']);
});

test('select: каталог с повторным фильтром starts_at, order и limit', () => {
  const p = new URLSearchParams();
  p.set('select', '*');
  p.set('published', 'is.true');
  p.set('order', 'starts_at.asc');
  p.set('limit', '100');
  p.append('sport', 'eq.hockey');
  p.append('starts_at', 'gte.2026-08-26T00:00:00.000Z');
  p.append('starts_at', 'lte.2026-09-26T00:00:00.000Z');
  const { text, params } = translateSelect('matches', p.toString());
  assert.match(text, /"matches"\."starts_at" >= \$2 AND "matches"\."starts_at" <= \$3/);
  assert.match(text, /ORDER BY "matches"\."starts_at" ASC LIMIT 100$/);
  assert.deepEqual(params, ['hockey', '2026-08-26T00:00:00.000Z', '2026-09-26T00:00:00.000Z']);
});

test('select: embed matches(starts_at,duration_min) для занятости', () => {
  const { text, params } = translateSelect('requests',
    'select=matches(starts_at,duration_min)&status=eq.confirmed&match_id=not.is.null');
  assert.match(text, /row_to_json/);
  assert.match(text, /_e\."starts_at", _e\."duration_min" FROM "matches" _e WHERE _e\."id" = "requests"\."match_id"/);
  assert.match(text, /"requests"\."match_id" IS NOT NULL/);
  assert.deepEqual(params, ['confirmed']);
});

test('select: *,matches(*) — заявки с вложенным матчем', () => {
  const { text } = translateSelect('requests', 'select=*,matches(*)&order=created_at.desc&limit=300');
  assert.match(text, /^SELECT "requests"\.\*, \(SELECT row_to_json\(_t\) FROM \(SELECT _e\.\* FROM "matches" _e/);
  assert.match(text, /AS "matches" FROM "requests" ORDER BY "requests"\."created_at" DESC LIMIT 300$/);
});

test('insert: заявка с text[], jsonb и null, returning minimal', () => {
  const { text, params } = translateInsert('requests', [{
    match_id: null,
    custom_match: { teams: 'А — Б', date_text: 'суббота' },
    services: ['stream', 'highlights'],
    name: 'Анна',
    price_quote: 5000,
  }], { returning: 'minimal' });
  assert.match(text, /^INSERT INTO "requests" \("match_id", "custom_match", "services", "name", "price_quote"\) VALUES \(\$1, \$2::jsonb, \$3::text\[\], \$4, \$5\)$/);
  assert.equal(params[0], null);
  assert.equal(params[1], JSON.stringify({ teams: 'А — Б', date_text: 'суббота' }));
  assert.equal(params[2], '{"stream","highlights"}');
  assert.deepEqual(params.slice(3), ['Анна', 5000]);
});

test('insert: очередь импорта, ignore-duplicates возвращает только вставленные', () => {
  const { text } = translateInsert('import_queue',
    [{ source: 's', source_key: 'k', kind: 'new', status: 'pending', payload: { a: 1 } }],
    { onConflict: 'source,source_key', ignoreDuplicates: true });
  assert.match(text, /ON CONFLICT \("source", "source_key"\) DO NOTHING RETURNING \*$/);
});

test('insert: merge-duplicates обновляет все неконфликтные колонки', () => {
  const { text } = translateInsert('import_queue',
    [{ source: 's', source_key: 'k', kind: 'update', status: 'pending', payload: {} }],
    { onConflict: 'source,source_key' });
  assert.match(text, /DO UPDATE SET "kind" = EXCLUDED\."kind", "status" = EXCLUDED\."status", "payload" = EXCLUDED\."payload" RETURNING \*$/);
});

test('update: атомарный approve с двумя фильтрами', () => {
  const { text, params } = translateUpdate('import_queue',
    'id=eq.abc-123&status=eq.pending',
    { status: 'approved', decided_at: '2026-08-26T10:00:00Z' });
  assert.equal(text, 'UPDATE "import_queue" SET "status" = $1, "decided_at" = $2 WHERE "import_queue"."id" = $3 AND "import_queue"."status" = $4 RETURNING *');
  assert.deepEqual(params, ['approved', '2026-08-26T10:00:00Z', 'abc-123', 'pending']);
});

test('delete матча по id', () => {
  const { text, params } = translateDelete('matches', 'id=eq.7');
  assert.equal(text, 'DELETE FROM "matches" WHERE "matches"."id" = $1 RETURNING *');
  assert.deepEqual(params, ['7']);
});

test('защита: кривой идентификатор, фильтр и пустой where — ошибка', () => {
  assert.throws(() => translateSelect('matches; drop', 'select=*'));
  assert.throws(() => translateSelect('matches', 'select=*&id=like.5'));
  assert.throws(() => translateUpdate('matches', '', { a: 1 }));
  assert.throws(() => translateDelete('matches', 'order=id.asc'));
});
