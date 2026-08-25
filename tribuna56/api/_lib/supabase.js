// Обертка над Supabase PostgREST обычным fetch — без npm-зависимостей.
// env: SUPABASE_URL, SUPABASE_SERVICE_KEY (service_role, ТОЛЬКО в env Vercel).
// Браузер сюда не ходит никогда — только serverless-функции.

export class SbError extends Error {
  constructor(status, text) {
    super(`Supabase ${status}: ${String(text).slice(0, 300)}`);
    this.status = status;
  }
}

export function sbConfigured() {
  return Boolean(process.env.SUPABASE_URL && process.env.SUPABASE_SERVICE_KEY);
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
  return sbFetch(`${table}?${query}`);
}

// opts: {returning: 'representation'|'minimal', onConflict: 'col1,col2', ignoreDuplicates: bool}
export function sbInsert(table, rows, opts = {}) {
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
  return sbFetch(`${table}?${query}`, {
    method: 'PATCH',
    headers: { Prefer: 'return=representation' },
    body: JSON.stringify(patch),
  });
}

export function sbDelete(table, query) {
  return sbFetch(`${table}?${query}`, {
    method: 'DELETE',
    headers: { Prefer: 'return=representation' },
  });
}
