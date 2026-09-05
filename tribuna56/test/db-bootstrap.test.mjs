import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SCHEMA_STATEMENTS, ensureSchema, isMissingRelation, isAlreadyExists } from '../api/_lib/schema.js';
import { neonExec, __setNeonForTests } from '../api/_lib/db.js';

const makeErr = (code, msg) => Object.assign(new Error(msg), { code });

// SQL → операторы: срезать --комментарии, разбить по ';' вне $$-тел.
function splitSql(text) {
  const clean = text.replace(/--[^\n]*/g, '');
  const stmts = [];
  let cur = '';
  let inDollar = false;
  for (let i = 0; i < clean.length; i++) {
    if (clean.startsWith('$$', i)) { inDollar = !inDollar; cur += '$$'; i++; continue; }
    if (clean[i] === ';' && !inDollar) { stmts.push(cur); cur = ''; continue; }
    cur += clean[i];
  }
  stmts.push(cur);
  return stmts.map((s) => s.replace(/\s+/g, ' ').trim()).filter(Boolean);
}

test('schema.js и db/schema.sql — один и тот же DDL (постатейно)', async () => {
  const sqlFile = await readFile(new URL('../db/schema.sql', import.meta.url), 'utf8');
  const fromFile = splitSql(sqlFile);
  const fromJs = SCHEMA_STATEMENTS.map((s) => s.replace(/--[^\n]*/g, '').replace(/\s+/g, ' ').trim());
  assert.deepEqual(fromJs, fromFile);
});

test('каждый оператор схемы идемпотентен по форме', () => {
  for (const s of SCHEMA_STATEMENTS) {
    const t = s.replace(/\s+/g, ' ').trim();
    assert.ok(
      /^(create (unique )?(table|index) if not exists|create or replace function|drop trigger if exists|alter table)/.test(t)
      || /^create trigger matches_touch/.test(t), // защищен drop trigger строкой выше
      `не идемпотентен: ${t.slice(0, 60)}`,
    );
  }
});

test('isMissingRelation: 42P01 или текст ошибки; прочее — нет', () => {
  assert.ok(isMissingRelation(makeErr('42P01', 'x')));
  assert.ok(isMissingRelation(new Error('relation "matches" does not exist')));
  assert.ok(!isMissingRelation(makeErr('42501', 'permission denied')));
  assert.ok(!isMissingRelation(new Error('connection refused')));
  assert.ok(!isMissingRelation(null));
});

test('isAlreadyExists: duplicate_table/object/function и текст', () => {
  assert.ok(isAlreadyExists(makeErr('42P07', 'x')));
  assert.ok(isAlreadyExists(makeErr('42710', 'x')));
  assert.ok(isAlreadyExists(new Error('trigger "matches_touch" already exists')));
  assert.ok(!isAlreadyExists(makeErr('42601', 'syntax error')));
});

test('ensureSchema: прогоняет всё по порядку, «уже есть» глотает, прочее бросает', async () => {
  const ran = [];
  await ensureSchema(async (t) => {
    ran.push(t);
    if (ran.length === 2) throw makeErr('42P07', 'already exists'); // гонка — не валимся
  });
  assert.deepEqual(ran, SCHEMA_STATEMENTS);

  await assert.rejects(
    () => ensureSchema(async () => { throw makeErr('42501', 'permission denied'); }),
    /permission denied/,
  );
});

test('neonExec: пустая база → авто-схема → повтор запроса; бутстрап один на инстанс', async () => {
  const ddl = [];
  let ready = false;
  __setNeonForTests({
    query: async (text) => {
      if (/^\s*(create|drop|alter)/i.test(text)) { ddl.push(text); ready = true; return []; }
      if (!ready) throw makeErr('42P01', 'relation "matches" does not exist');
      return [{ id: 1 }];
    },
  });
  // два параллельных запроса к пустой базе — схема разворачивается один раз
  const [a, b] = await Promise.all([
    neonExec({ text: 'select * from matches', params: [] }),
    neonExec({ text: 'select * from requests', params: [] }),
  ]);
  assert.deepEqual(a, [{ id: 1 }]);
  assert.deepEqual(b, [{ id: 1 }]);
  assert.equal(ddl.length, SCHEMA_STATEMENTS.length);
});

test('neonExec: провал бутстрапа не залипает — следующий запрос пробует снова', async () => {
  let allowDdl = false;
  let ready = false;
  __setNeonForTests({
    query: async (text) => {
      if (/^\s*(create|drop|alter)/i.test(text)) {
        if (!allowDdl) throw makeErr('42501', 'permission denied');
        ready = true;
        return [];
      }
      if (!ready) throw makeErr('42P01', 'relation "matches" does not exist');
      return [{ ok: true }];
    },
  });
  await assert.rejects(() => neonExec({ text: 'select 1 from matches', params: [] }), /permission denied/);
  allowDdl = true; // права починили — без передеплоя должно ожить
  assert.deepEqual(await neonExec({ text: 'select 1 from matches', params: [] }), [{ ok: true }]);
});

test('neonExec: чужие ошибки не триггерят бутстрап', async () => {
  const ddl = [];
  __setNeonForTests({
    query: async (text) => {
      if (/^\s*(create|drop|alter)/i.test(text)) { ddl.push(text); return []; }
      throw makeErr('28P01', 'password authentication failed');
    },
  });
  await assert.rejects(() => neonExec({ text: 'select 1', params: [] }), /authentication/);
  assert.equal(ddl.length, 0);
});
