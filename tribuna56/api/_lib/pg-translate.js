// Транслятор нашего подмножества PostgREST-запросов в параметризованный SQL —
// для работы тех же хендлеров поверх Neon (обычный Postgres без PostgREST).
// Поддерживается ровно тот набор конструкций, который используют функции
// сайта; всё вне его — ошибка (лучше упасть громко, чем сгенерить не тот SQL).
// Чистые функции: покрыты тестами в test/pg-translate.test.mjs.

const IDENT = /^[a-z_][a-z0-9_]*$/;

function ident(name) {
  if (!IDENT.test(String(name))) throw new Error(`bad identifier: ${name}`);
  return `"${name}"`;
}

// Единственная связь, которую используют embed-запросы: requests → matches.
const RELATIONS = {
  requests: { matches: { local: 'match_id', foreign: 'id' } },
};

function parseSelect(table, selectValue) {
  const cols = [];
  let embed = null;
  for (const part of String(selectValue || '*').split(/,(?![^(]*\))/)) {
    const m = part.match(/^([a-z_][a-z0-9_]*)\(([^)]*)\)$/);
    if (m) {
      const rel = (RELATIONS[table] || {})[m[1]];
      if (!rel) throw new Error(`unknown embed: ${table} → ${m[1]}`);
      embed = {
        table: m[1],
        rel,
        cols: m[2] === '*' ? null : m[2].split(',').map((c) => ident(c.trim())),
      };
    } else if (part.trim() === '*') {
      cols.push(`${ident(table)}.*`);
    } else {
      cols.push(`${ident(table)}.${ident(part.trim())}`);
    }
  }
  if (!cols.length && !embed) cols.push(`${ident(table)}.*`);
  return { cols, embed };
}

// Фильтры вида col=eq.X / is.true / not.is.null / gte.X / lte.X
function parseWhere(table, params, sqlParams) {
  const where = [];
  for (const [key, value] of params) {
    if (['select', 'order', 'limit', 'on_conflict'].includes(key)) continue;
    const col = `${ident(table)}.${ident(key)}`;
    if (value === 'is.true') where.push(`${col} IS TRUE`);
    else if (value === 'is.false') where.push(`${col} IS FALSE`);
    else if (value === 'is.null') where.push(`${col} IS NULL`);
    else if (value === 'not.is.null') where.push(`${col} IS NOT NULL`);
    else if (value.startsWith('eq.')) {
      sqlParams.push(value.slice(3));
      where.push(`${col} = $${sqlParams.length}`);
    } else if (value.startsWith('gte.')) {
      sqlParams.push(value.slice(4));
      where.push(`${col} >= $${sqlParams.length}`);
    } else if (value.startsWith('lte.')) {
      sqlParams.push(value.slice(4));
      where.push(`${col} <= $${sqlParams.length}`);
    } else {
      throw new Error(`unsupported filter: ${key}=${value}`);
    }
  }
  return where;
}

function tail(table, params, sqlParams) {
  let sql = '';
  const where = parseWhere(table, params, sqlParams);
  if (where.length) sql += ` WHERE ${where.join(' AND ')}`;
  const order = params.get('order');
  if (order) {
    const m = String(order).match(/^([a-z_][a-z0-9_]*)\.(asc|desc)$/);
    if (!m) throw new Error(`unsupported order: ${order}`);
    sql += ` ORDER BY ${ident(table)}.${ident(m[1])} ${m[2].toUpperCase()}`;
  }
  const limit = params.get('limit');
  if (limit) {
    const n = Number(limit);
    if (!Number.isInteger(n) || n < 0) throw new Error(`bad limit: ${limit}`);
    sql += ` LIMIT ${n}`;
  }
  return sql;
}

export function translateSelect(table, queryString) {
  const params = new URLSearchParams(queryString);
  const { cols, embed } = parseSelect(table, params.get('select'));
  const sqlParams = [];
  const parts = [...cols];
  if (embed) {
    const sub = embed.cols
      ? `SELECT ${embed.cols.map((c) => `_e.${c}`).join(', ')} FROM ${ident(embed.table)} _e WHERE _e.${ident(embed.rel.foreign)} = ${ident(table)}.${ident(embed.rel.local)}`
      : `SELECT _e.* FROM ${ident(embed.table)} _e WHERE _e.${ident(embed.rel.foreign)} = ${ident(table)}.${ident(embed.rel.local)}`;
    parts.push(`(SELECT row_to_json(_t) FROM (${sub}) _t) AS ${ident(embed.table)}`);
  }
  const text = `SELECT ${parts.join(', ')} FROM ${ident(table)}` + tail(table, params, sqlParams);
  return { text, params: sqlParams };
}

// Значение → параметр с нужным приведением типа в SQL.
function bindValue(value, sqlParams) {
  if (Array.isArray(value)) {
    // text[]-литерал: элементы в кавычках, кавычки/бэкслеши экранируются
    const literal = `{${value.map((v) => `"${String(v).replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`).join(',')}}`;
    sqlParams.push(literal);
    return `$${sqlParams.length}::text[]`;
  }
  if (value !== null && typeof value === 'object') {
    sqlParams.push(JSON.stringify(value));
    return `$${sqlParams.length}::jsonb`;
  }
  sqlParams.push(value);
  return `$${sqlParams.length}`;
}

export function translateInsert(table, rows, opts = {}) {
  const list = Array.isArray(rows) ? rows : [rows];
  if (!list.length) throw new Error('empty insert');
  const columns = Object.keys(list[0]);
  if (!columns.length) throw new Error('empty row');
  const sqlParams = [];
  const tuples = list.map((row) =>
    `(${columns.map((c) => bindValue(row[c] === undefined ? null : row[c], sqlParams)).join(', ')})`);
  let text = `INSERT INTO ${ident(table)} (${columns.map(ident).join(', ')}) VALUES ${tuples.join(', ')}`;
  if (opts.onConflict) {
    const conflictCols = String(opts.onConflict).split(',').map((c) => ident(c.trim()));
    if (opts.ignoreDuplicates) {
      text += ` ON CONFLICT (${conflictCols.join(', ')}) DO NOTHING`;
    } else {
      const updatable = columns.filter((c) => !String(opts.onConflict).split(',').map((s) => s.trim()).includes(c));
      text += ` ON CONFLICT (${conflictCols.join(', ')}) DO UPDATE SET ` +
        updatable.map((c) => `${ident(c)} = EXCLUDED.${ident(c)}`).join(', ');
    }
  }
  if (opts.returning !== 'minimal') text += ' RETURNING *';
  return { text, params: sqlParams };
}

export function translateUpdate(table, queryString, patch) {
  const columns = Object.keys(patch || {});
  if (!columns.length) throw new Error('empty patch');
  const sqlParams = [];
  const sets = columns.map((c) => `${ident(c)} = ${bindValue(patch[c] === undefined ? null : patch[c], sqlParams)}`);
  const params = new URLSearchParams(queryString);
  const where = parseWhere(table, params, sqlParams);
  if (!where.length) throw new Error('update without where');
  const text = `UPDATE ${ident(table)} SET ${sets.join(', ')} WHERE ${where.join(' AND ')} RETURNING *`;
  return { text, params: sqlParams };
}

export function translateDelete(table, queryString) {
  const sqlParams = [];
  const params = new URLSearchParams(queryString);
  const where = parseWhere(table, params, sqlParams);
  if (!where.length) throw new Error('delete without where');
  const text = `DELETE FROM ${ident(table)} WHERE ${where.join(' AND ')} RETURNING *`;
  return { text, params: sqlParams };
}
