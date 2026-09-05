// Авто-развертывание схемы на Neon: db.js при «relation … does not exist»
// прогоняет эти DDL и повторяет исходный запрос — ручной шаг «выполните
// schema.sql в консоли» не нужен.
//
// ИСТОЧНИК ПРАВДЫ — db/schema.sql: этот массив обязан совпадать с ним
// по коду (test/db-bootstrap.test.mjs сверяет оба файла постатейно).
// Все выражения идемпотентны: повторный прогон и гонка инстансов безопасны.

export const SCHEMA_STATEMENTS = [
  `create table if not exists matches (
    id           bigint generated always as identity primary key,
    sport        text not null,
    league       text,
    age_group    text,
    team_home    text not null,
    team_away    text not null,
    venue        text,
    address      text,
    starts_at    timestamptz not null,
    duration_min int not null default 90,
    status       text not null default 'scheduled'
                 check (status in ('scheduled','live','finished','canceled')),
    stream_url     text,
    highlights_url text,
    source       text not null default 'manual',
    source_key   text,
    published    boolean not null default true,
    created_at   timestamptz not null default now(),
    updated_at   timestamptz not null default now()
  )`,

  `create unique index if not exists matches_source_uidx on matches (source, source_key)
    where source_key is not null`,
  `create index if not exists matches_list_idx on matches (published, starts_at)`,

  `create table if not exists requests (
    id           uuid primary key default gen_random_uuid(),
    match_id     bigint references matches(id) on delete set null,
    custom_match jsonb,
    services     text[] not null,
    player_note  text,
    name         text not null,
    phone        text not null,
    contact_channel text,
    comment      text,
    price_quote  int,
    status       text not null default 'new'
                 check (status in ('new','confirmed','done','declined')),
    created_at   timestamptz not null default now()
  )`,

  `create index if not exists requests_status_idx on requests (status, created_at desc)`,

  `create table if not exists import_queue (
    id          uuid primary key default gen_random_uuid(),
    source      text not null,
    source_key  text not null,
    kind        text not null default 'new' check (kind in ('new','update')),
    payload     jsonb not null,
    status      text not null default 'pending'
                check (status in ('pending','approved','rejected')),
    match_id    bigint references matches(id) on delete set null,
    created_at  timestamptz not null default now(),
    decided_at  timestamptz,
    unique (source, source_key)
  )`,

  `create table if not exists scoreboards (
    id          text primary key check (id ~ '^[a-z0-9]{4,24}$'),
    token       text not null,
    data        jsonb not null default '{}'::jsonb,
    updated_at  timestamptz not null default now()
  )`,

  `create or replace function set_updated_at() returns trigger language plpgsql as
  $$ begin new.updated_at = now(); return new; end $$`,

  `drop trigger if exists matches_touch on matches`,

  `create trigger matches_touch before update on matches
    for each row execute function set_updated_at()`,

  `alter table matches      enable row level security`,
  `alter table requests     enable row level security`,
  `alter table import_queue enable row level security`,
  `alter table scoreboards  enable row level security`,
];

// Postgres 42P01 undefined_table; сообщение — на случай драйвера без .code.
export function isMissingRelation(e) {
  if (!e) return false;
  if (e.code === '42P01') return true;
  return /relation "[^"]+" does not exist/.test(String(e.message || ''));
}

// «Уже существует» при гонке двух инстансов — не ошибка:
// 42P07 duplicate_table, 42710 duplicate_object (триггер), 42723 duplicate_function.
const EXISTS_CODES = new Set(['42P07', '42710', '42723', '42P06', '42701']);
export function isAlreadyExists(e) {
  if (!e) return false;
  if (EXISTS_CODES.has(e.code)) return true;
  return /already exists/i.test(String(e.message || ''));
}

// exec: (text) => Promise — выполняет один SQL-оператор.
export async function ensureSchema(exec) {
  for (const text of SCHEMA_STATEMENTS) {
    try {
      await exec(text);
    } catch (e) {
      if (!isAlreadyExists(e)) throw e;
    }
  }
}
