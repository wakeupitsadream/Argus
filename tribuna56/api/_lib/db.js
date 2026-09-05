// Транспорт Neon: официальный HTTP-драйвер @neondatabase/serverless.
// env: DATABASE_URL (инъектится интеграцией Neon в Vercel Marketplace).
// Импорт ленивый: без DATABASE_URL зависимость вообще не загружается.
//
// Свежая база пуста: при «relation … does not exist» схема разворачивается
// автоматически (api/_lib/schema.js) и запрос повторяется — прогонять
// db/schema.sql вручную на Neon не нужно.

import { ensureSchema, isMissingRelation } from './schema.js';

let neonSql = null;
let bootstrapPromise = null;

export function neonConfigured() {
  return Boolean(process.env.DATABASE_URL);
}

export function __setNeonForTests(sqlLike) {
  neonSql = sqlLike;
  bootstrapPromise = null;
}

async function getNeon() {
  if (!neonSql) {
    const { neon } = await import('@neondatabase/serverless');
    neonSql = neon(process.env.DATABASE_URL);
  }
  return neonSql;
}

// Прямой вызов драйвера. API менялось между мажорами: в 1.x параметризованный
// вызов строкой — sql.query(text, params), в 0.10.x метода query нет, но
// принимается sql(text, params). Поддерживаем оба; результат нормализуем
// (массив строк или {rows}).
async function rawExec(sql, text, params) {
  const res = typeof sql.query === 'function'
    ? await sql.query(text, params)
    : await sql(text, params);
  if (Array.isArray(res)) return res;
  return (res && res.rows) || [];
}

// {text, params} → массив строк.
export async function neonExec({ text, params }) {
  const sql = await getNeon();
  try {
    return await rawExec(sql, text, params);
  } catch (e) {
    if (!isMissingRelation(e)) throw e;
    if (!bootstrapPromise) {
      console.warn('[db] таблиц нет — разворачиваю схему автоматически');
      bootstrapPromise = ensureSchema((ddl) => rawExec(sql, ddl)).catch((err) => {
        bootstrapPromise = null; // не вышло — следующий запрос попробует снова
        throw err;
      });
    }
    await bootstrapPromise;
    return rawExec(sql, text, params);
  }
}
