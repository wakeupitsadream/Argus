-- «Трибуна 56» — схема БД Supabase (Postgres).
-- Применяется вручную: Supabase Dashboard → SQL Editor → вставить целиком → Run.
-- Повторный запуск на чистом проекте; для миграций правьте точечно.

-- ========== Матчи каталога ==========
create table matches (
  id           bigint generated always as identity primary key,
  sport        text not null,               -- id вида спорта из assets/data.js (hockey|football|...)
  league       text,                        -- турнир/лига, свободный текст
  age_group    text,                        -- «2012 г.р.», «U-14» — свободный текст
  team_home    text not null,
  team_away    text not null,
  venue        text,                        -- арена/зал
  address      text,
  starts_at    timestamptz not null,        -- вводится по Оренбургу (UTC+5), хранится в UTC
  duration_min int not null default 90,
  status       text not null default 'scheduled'
               check (status in ('scheduled','live','finished','canceled')),
  stream_url     text,                      -- ссылка VK Видео; embed строит клиент
  highlights_url text,
  source       text not null default 'manual',  -- 'manual' либо id адаптера импортёра
  source_key   text,                        -- стабильный ключ матча в источнике (для дедупликации)
  published    boolean not null default true,
  created_at   timestamptz not null default now(),
  updated_at   timestamptz not null default now()
);

create unique index matches_source_uidx on matches (source, source_key)
  where source_key is not null;
create index matches_list_idx on matches (published, starts_at);

-- ========== Заявки ==========
create table requests (
  id           uuid primary key default gen_random_uuid(),
  match_id     bigint references matches(id) on delete set null,
  custom_match jsonb,                       -- {sport, teams, venue, date_text} — «моего матча нет в списке»
  services     text[] not null,             -- подмножество ['stream','highlights','personal']
  player_note  text,                        -- номер/фамилия игрока для персональной съемки
  name         text not null,
  phone        text not null,
  contact_channel text,                     -- phone|telegram|whatsapp
  comment      text,
  price_quote  int,                         -- сумма, пересчитанная сервером (assets/pricing.js)
  status       text not null default 'new'
               check (status in ('new','confirmed','done','declined')),
  created_at   timestamptz not null default now()
);

create index requests_status_idx on requests (status, created_at desc);

-- ========== Очередь импорта ==========
-- Отклоненные строки не удаляются: unique(source, source_key) — «надгробие»,
-- не дающее отвергнутому матчу воскреснуть при следующем прогоне импортёра.
create table import_queue (
  id          uuid primary key default gen_random_uuid(),
  source      text not null,
  source_key  text not null,
  kind        text not null default 'new' check (kind in ('new','update')),
  payload     jsonb not null,               -- normalized match + raw + possible_duplicate_of
  status      text not null default 'pending'
              check (status in ('pending','approved','rejected')),
  match_id    bigint references matches(id) on delete set null,
  created_at  timestamptz not null default now(),
  decided_at  timestamptz,
  unique (source, source_key)
);

-- ========== Табло для OBS-трансляций ==========
-- Одна строка = одно табло. token задает пульт при первой записи;
-- дальнейшие записи только с тем же token. Чтение — публичное (через API).
create table scoreboards (
  id          text primary key check (id ~ '^[a-z0-9]{4,24}$'),
  token       text not null,
  data        jsonb not null default '{}'::jsonb,
  updated_at  timestamptz not null default now()
);

-- ========== updated_at автоматом ==========
create function set_updated_at() returns trigger language plpgsql as
$$ begin new.updated_at = now(); return new; end $$;

create trigger matches_touch before update on matches
  for each row execute function set_updated_at();

-- ========== RLS: deny-all для anon ==========
-- Актуально для Supabase (PostgREST + anon-ключ). Для Neon блок не нужен,
-- но выполнять его безопасно: владелец таблиц RLS не подчиняется.
-- Политик НЕ создаем: включенный RLS без политик закрывает таблицы для anon-ключа.
-- Сервер ходит только с service_role (env Vercel), который RLS обходит.
alter table matches      enable row level security;
alter table requests     enable row level security;
alter table import_queue enable row level security;
alter table scoreboards  enable row level security;
