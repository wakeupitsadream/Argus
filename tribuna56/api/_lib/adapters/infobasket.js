// Адаптер: баскетбол РФБ (Оренбургская область) через открытый JSON API
// reg.infobasket.su. Формат проверен вживую 25.08.2026 (см. SOURCES.md).
//
// Конвейер: страница соревнований областной федерации (обычный HTML,
// без WAF) → id соревнований → GetCalendar JSON по каждому → будущие
// детские (10–16 лет) игры. Город по арене подтверждает админ в очереди.

export const id = 'infobasket';
export const label = 'РФБ Оренбуржье (infobasket)';

const COMPS_URL = 'https://orenburg.russiabasket.ru/competitions';
const CAL_URL = (comp) => `https://reg.infobasket.su/Comp/GetCalendar/?comps=${comp}&format=json`;
const HEADERS = {
  'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36',
  'Accept-Language': 'ru-RU,ru;q=0.9',
};

// --- чистые функции (покрыты тестами) ---

// HTML страницы соревнований → уникальные id
export function parseCompIds(html) {
  const ids = new Set();
  const re = /\/competitions\/(\d+)/g;
  let m;
  while ((m = re.exec(String(html)))) ids.add(Number(m[1]));
  return [...ids];
}

// Детская лига? → age_group или null (взрослые чемпионаты отсекаем)
export function youthAgeGroup(leagueName) {
  const s = String(leagueName || '');
  const m = s.match(/20(1[0-6])\s*г\.?\s*р\.?/i);
  if (m) return `20${m[1]} г.р.`;
  if (/юнош|девуш|юниор/i.test(s) && !/мужчин|женщин|ветеран/i.test(s)) return 'юноши/девушки';
  return null;
}

// dd.mm.yyyy + HH:MM (местное) → ISO с оренбургским смещением
export function gameStartsAt(game) {
  const m = String(game.GameDate || '').match(/^(\d{2})\.(\d{2})\.(\d{4})$/);
  if (!m) return null;
  const time = game.HasTime && /^\d{2}:\d{2}$/.test(game.GameTime || '') ? game.GameTime : '12:00';
  return `${m[3]}-${m[2]}-${m[1]}T${time}:00+05:00`;
}

export function gameToNormalized(game, { now }) {
  const ageGroup = youthAgeGroup(game.LeagueNameRu);
  if (!ageGroup) return null;
  const startsAt = gameStartsAt(game);
  if (!startsAt || Date.parse(startsAt) <= now.getTime()) return null; // только будущие
  if (!game.ShortTeamNameAru || !game.ShortTeamNameBru) return null;
  return {
    sourceKey: `ib-${game.GameID}`,
    sport: 'basketball',
    league: [String(game.LeagueNameRu || '').replace(/\s+/g, ' ').trim(), game.CompNameRu]
      .filter(Boolean).join(', '),
    ageGroup,
    teamHome: game.ShortTeamNameAru,
    teamAway: game.ShortTeamNameBru,
    venue: game.ArenaRu || null,
    address: null,
    startsAt,
    raw: {
      game_id: game.GameID,
      league: game.LeagueNameRu,
      region: game.RegionRu,
      date: game.GameDate,
      time: game.GameTime,
    },
  };
}

// --- сам адаптер ---

export async function fetchMatches({ now }) {
  const compsResp = await fetch(COMPS_URL, { headers: HEADERS });
  if (!compsResp.ok) throw new Error(`competitions ${compsResp.status}`);
  const compIds = parseCompIds(await compsResp.text()).slice(0, 12);

  const out = [];
  for (const comp of compIds) {
    const r = await fetch(CAL_URL(comp), { headers: HEADERS });
    if (!r.ok) continue; // одно соревнование не валит остальные
    let games;
    try { games = await r.json(); } catch { continue; }
    if (!Array.isArray(games)) continue;
    for (const g of games) {
      const n = gameToNormalized(g, { now });
      if (n) out.push(n);
    }
  }
  return out;
}
