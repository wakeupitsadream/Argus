// Слой БД с единым интерфейсом sbSelect/Insert/Update/Delete и двумя
// бэкендами (выбор по env, менять код хендлеров не нужно):
//  - Supabase: PostgREST обычным fetch (SUPABASE_URL + SUPABASE_SERVICE_KEY);
//  - Neon: перевод тех же запросов в SQL (pg-translate.js) + драйвер
//    @neondatabase/serverless (DATABASE_URL).
// Браузер сюда не ходит никогда — только serverless-функции.

import { translateSelect, translateInsert, translateUpdate, translateDelete } from './pg-translate.js';
import { neonConfigured, neonExec } from './db.js';

export class SbError extends Error {
  constructor(status, text) {
    super(`Supabase ${status}: ${String(text).slice(0, 300)}`);
    this.status = status;
  }
}

function supabaseConfigured() {
  return Boolean(process.env.SUPABASE_URL && process.env.SUPABASE_SERVICE_KEY);
}

// Supabase в приоритете, если задан явно; иначе Neon по DATABASE_URL.
const useNeon = () => !supabaseConfigured() && neonConfigured();

export function sbConfigured() {
  return supabaseConfigured() || neonConfigured();
}

async function sbFetch(path, { method = 'GET', headers = {}, body } = {}) {
  const key = process.env.SUPABASE_SERVICE_KEY;
  const url = `${process.env.SUPABASE_URL}/rest/v1/${path}`;
  const r = await fetch(url, {
    method,
    headers: {
      apikey: key,
      Authorization: `Bearer ${key}`,
      'Content-Type': 'application/json',
      ...headers,
    },
    body,
  });
  if (!r.ok) throw new SbError(r.status, await r.text().catch(() => ''));
  if (r.status === 204) return null;
  const text = await r.text();
  return text ? JSON.parse(text) : null;
}

// query — строка PostgREST-фильтров ('select=*&id=eq.5'). Возвращает массив строк.
export function sbSelect(table, query) {
  if (useNeon()) return neonExec(translateSelect(table, query));
  return sbFetch(`${table}?${query}`);
}

// opts: {returning: 'representation'|'minimal', onConflict: 'col1,col2', ignoreDuplicates: bool}
export async function sbInsert(table, rows, opts = {}) {
  if (useNeon()) {
    const res = await neonExec(translateInsert(table, rows, opts));
    return opts.returning === 'minimal' ? null : res;
  }
  const prefer = [`return=${opts.returning || 'representation'}`];
  if (opts.onConflict) {
    prefer.push(opts.ignoreDuplicates ? 'resolution=ignore-duplicates' : 'resolution=merge-duplicates');
  }
  const path = opts.onConflict ? `${table}?on_conflict=${encodeURIComponent(opts.onConflict)}` : table;
  return sbFetch(path, {
    method: 'POST',
    headers: { Prefer: prefer.join(',') },
    body: JSON.stringify(rows),
  });
}

export function sbUpdate(table, query, patch) {
  if (useNeon()) return neonExec(translateUpdate(table, query, patch));
  return sbFetch(`${table}?${query}`, {
    method: 'PATCH',
    headers: { Prefer: 'return=representation' },
    body: JSON.stringify(patch),
  });
}

export function sbDelete(table, query) {
  if (useNeon()) return neonExec(translateDelete(table, query));
  return sbFetch(`${table}?${query}`, {
    method: 'DELETE',
    headers: { Prefer: 'return=representation' },
  });
}
