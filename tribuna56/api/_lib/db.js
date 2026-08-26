// Транспорт Neon: официальный HTTP-драйвер @neondatabase/serverless.
// env: DATABASE_URL (инъектится интеграцией Neon в Vercel Marketplace).
// Импорт ленивый: без DATABASE_URL зависимость вообще не загружается.

let neonSql = null;

export function neonConfigured() {
  return Boolean(process.env.DATABASE_URL);
}

async function getNeon() {
  if (!neonSql) {
    const { neon } = await import('@neondatabase/serverless');
    neonSql = neon(process.env.DATABASE_URL);
  }
  return neonSql;
}

// {text, params} → массив строк (нормализуем между версиями драйвера)
export async function neonExec({ text, params }) {
  const sql = await getNeon();
  const res = await sql.query(text, params);
  if (Array.isArray(res)) return res;
  return (res && res.rows) || [];
}
