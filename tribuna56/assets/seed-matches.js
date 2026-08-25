// Вшитый каталог РЕАЛЬНЫХ матчей Оренбурга — фолбэк, пока не подключена
// база (и подстраховка при её недоступности). Данные собраны вручную из
// открытых источников (федерации, лиги, r-hockey, VK) — у каждой записи
// комментарий с источником. Обновляется правкой этого файла; после
// подключения Supabase эти же матчи загружаются из db/seed-matches.sql.
//
// ЗАПОЛНЯЕТСЯ ТОЛЬКО ПРОВЕРЕННЫМИ МАТЧАМИ — ничего выдуманного.

export const SEED_GENERATED_AT = '2026-08-25';

let id = 900000;
const M = (sport, league, age_group, team_home, team_away, venue, starts_at, extra = {}) => ({
  id: ++id,
  sport,
  league,
  age_group,
  team_home,
  team_away,
  venue,
  address: null,
  starts_at,
  duration_min: 90,
  status: 'scheduled',
  stream_url: null,
  highlights_url: null,
  ...extra,
});

export const SEED_MATCHES = [
  // Заполняется результатами разведки — см. коммит.
];
