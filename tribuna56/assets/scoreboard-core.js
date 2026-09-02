// Ядро табло: чистые функции без DOM и без Date.now() внутри (время всегда
// аргументом — тестируемость). Состояние — плоский JSON, живет в БД
// (удаленный пульт) или в localStorage (локальный режим в OBS).
//
// Часы: baseMs — наигранное на момент последнего старта/паузы,
// startedAt — epoch запуска (0 = стоят). Текущее = baseMs + (now - startedAt).
// Оверлей тикает сам по себе — опрос состояния нужен только при изменениях.
//
// Удаления привязаны к ИГРОВЫМ часам (стоят часы — стоит и штраф):
// left = baseLeftMs - (gameElapsed - setAtGameMs). Перед мутацией часов штрафы
// «материализуются» (пересчет baseLeftMs, БЕЗ отбрасывания недавно истекших:
// коррекция часов назад должна уметь «воскресить» штраф). Инвариант — момент
// истечения в игровом времени: сдвиг часов сдвигает и остаток штрафа.

export const SPORTS = {
  hockey: { label: 'Хоккей', periods: 3, periodMin: 20, periodWord: 'период', otWord: 'ОТ', penalties: true },
  football: { label: 'Футбол', periods: 2, periodMin: 30, periodWord: 'тайм', otWord: 'ДВ', penalties: false },
};

export function initState(sport = 'hockey', now = 0) {
  const cfg = SPORTS[sport] || SPORTS.hockey;
  return {
    v: 1,
    sport: SPORTS[sport] ? sport : 'hockey',
    tournament: '',
    home: { name: 'Хозяева', score: 0 },
    away: { name: 'Гости', score: 0 },
    period: 1,
    periodMin: cfg.periodMin,
    clock: { baseMs: 0, startedAt: 0 },
    penalties: [],
    rev: 0,       // монотонный счетчик правок — база синхронизации пультов
    updatedAt: now,
  };
}

export const clockRunning = (s) => Boolean(s.clock && s.clock.startedAt);

export function clockElapsed(s, now) {
  const c = s.clock || { baseMs: 0, startedAt: 0 };
  const ms = c.baseMs + (c.startedAt ? Math.max(0, now - c.startedAt) : 0);
  return Math.max(0, ms);
}

// Разница (g − якорь) НЕ зажимается в ноль: при коррекции часов назад
// игровое время может оказаться раньше якоря — остаток тогда растет
// (инвариант: штраф истекает в фиксированный момент игрового времени).
export function penaltyLeft(p, gameElapsedMs) {
  return Math.max(0, p.baseLeftMs - (gameElapsedMs - p.setAtGameMs));
}

// Активные штрафы команды (не истекшие), ближайшие к выходу — первыми.
export function activePenalties(s, team, now) {
  const g = clockElapsed(s, now);
  return (s.penalties || [])
    .filter((p) => p.team === team && penaltyLeft(p, g) > 0)
    .sort((a, b) => penaltyLeft(a, g) - penaltyLeft(b, g));
}

// Пересчет baseLeftMs к текущему моменту — вызывается перед мутацией часов.
// Остаток храним СО ЗНАКОМ: только что истекший штраф не выбрасывается,
// коррекция часов назад вернет его на табло. Чистим истекшие «давно» (>2 мин).
function materializePenalties(s, now) {
  const g = clockElapsed(s, now);
  s.penalties = (s.penalties || [])
    .map((p) => ({ ...p, baseLeftMs: p.baseLeftMs - Math.max(0, g - p.setAtGameMs), setAtGameMs: g }))
    .filter((p) => p.baseLeftMs > -120_000);
}

// Все мутации возвращают НОВОЕ состояние (структурное копирование одного уровня).
function clone(s) {
  return {
    ...s,
    home: { ...s.home },
    away: { ...s.away },
    clock: { ...s.clock },
    penalties: (s.penalties || []).map((p) => ({ ...p })),
  };
}

export function setScore(s, team, delta, now) {
  const n = clone(s);
  n[team].score = Math.min(999, Math.max(0, (n[team].score || 0) + delta));
  n.updatedAt = now;
  return n;
}

export function startClock(s, now) {
  const n = clone(s);
  if (!n.clock.startedAt) n.clock.startedAt = now;
  n.updatedAt = now;
  return n;
}

export function stopClock(s, now) {
  const n = clone(s);
  if (n.clock.startedAt) {
    materializePenalties(n, now);
    n.clock.baseMs = clockElapsed(n, now);
    n.clock.startedAt = 0;
  }
  n.updatedAt = now;
  return n;
}

// deltaMs может быть отрицательным; ход часов (идут/стоят) сохраняется.
export function adjustClock(s, deltaMs, now) {
  const n = clone(s);
  materializePenalties(n, now); // якорь остается на старом g: остаток штрафа
  const was = clockRunning(n);  // корректируется вместе с часами (−10с часам = +10с штрафу)
  n.clock.baseMs = Math.max(0, clockElapsed(n, now) + deltaMs);
  n.clock.startedAt = was ? now : 0;
  n.updatedAt = now;
  return n;
}

export function setClockMs(s, ms, now) {
  return adjustClock(s, ms - clockElapsed(s, now), now);
}

// Новый период: часы в 0 и стоят, недосиженные штрафы переезжают.
export function setPeriod(s, period, now) {
  const n = clone(s);
  materializePenalties(n, now);
  n.period = Math.min(9, Math.max(1, period));
  n.clock = { baseMs: 0, startedAt: 0 };
  n.penalties = n.penalties.map((p) => ({ ...p, setAtGameMs: 0 }));
  n.updatedAt = now;
  return n;
}

export function addPenalty(s, team, minutes, number, now) {
  const n = clone(s);
  materializePenalties(n, now);
  const g = clockElapsed(n, now);
  n.pseq = (n.pseq || 0) + 1; // монотонный счетчик: id уникален даже в одну мс
  n.penalties.push({
    id: `p${n.pseq}_${now.toString(36)}`,
    team,
    number: String(number || '').slice(0, 3),
    totalMs: minutes * 60000,
    baseLeftMs: minutes * 60000,
    setAtGameMs: g,
  });
  n.updatedAt = now;
  return n;
}

export function removePenalty(s, id, now) {
  const n = clone(s);
  n.penalties = n.penalties.filter((p) => p.id !== id);
  n.updatedAt = now;
  return n;
}

export function setSport(s, sport, now) {
  const cfg = SPORTS[sport];
  if (!cfg) return s;
  const n = clone(s);
  n.sport = sport;
  n.periodMin = cfg.periodMin;
  n.period = 1;
  n.clock = { baseMs: 0, startedAt: 0 };
  n.penalties = [];
  n.updatedAt = now;
  return n;
}

// «1-й период», «2-й тайм», овертайм — «ОТ»/«ДВ».
export function periodLabel(s) {
  const cfg = SPORTS[s.sport] || SPORTS.hockey;
  if (s.period > cfg.periods) {
    const extra = s.period - cfg.periods;
    return extra > 1 ? `${cfg.otWord}${extra}` : cfg.otWord;
  }
  return `${s.period}-й ${cfg.periodWord}`;
}

export function fmtClock(ms) {
  const total = Math.floor(Math.max(0, ms) / 1000);
  const m = Math.floor(total / 60);
  const sec = total % 60;
  return `${m}:${String(sec).padStart(2, '0')}`;
}

// Санитизация состояния, пришедшего извне (API/localStorage): чужой или
// битый JSON не должен ни уронить, ни атаковать страницы. Результат
// собирается ТОЛЬКО из белого списка полей; в текстах вырезаются символы
// разметки (innerHTML пульта), номера — только буквы/цифры.
const stripMarkup = (v, max) => String(v == null ? '' : v).replace(/[<>&"'`]/g, '').slice(0, max);
const num = (v, min, max, dflt = 0) => {
  const x = Number(v);
  return Number.isFinite(x) ? Math.min(max, Math.max(min, x)) : dflt;
};

export function sanitizeState(raw, now = 0) {
  const base = initState('hockey', now);
  if (!raw || typeof raw !== 'object') return base;
  // будущее «чуть-чуть» допустимо (перекос часов устройств), далекое — нет
  const tMax = now > 0 ? now + 60_000 : 8.64e15;
  const side = (t) => {
    const x = raw[t] && typeof raw[t] === 'object' ? raw[t] : {};
    return {
      name: stripMarkup(x.name, 40).trim() || base[t].name,
      score: Math.floor(num(x.score, 0, 999)),
    };
  };
  const c = raw.clock && typeof raw.clock === 'object' ? raw.clock : {};
  return {
    v: 1,
    sport: SPORTS[raw.sport] ? raw.sport : 'hockey',
    tournament: stripMarkup(raw.tournament, 80).trim(),
    home: side('home'),
    away: side('away'),
    period: Math.floor(num(raw.period, 1, 9, 1)),
    periodMin: Math.floor(num(raw.periodMin, 1, 90, 20)),
    clock: {
      baseMs: num(c.baseMs, 0, 6 * 3600_000),
      startedAt: num(c.startedAt, 0, tMax),
    },
    penalties: (Array.isArray(raw.penalties) ? raw.penalties : []).slice(0, 8)
      .filter((p) => p && (p.team === 'home' || p.team === 'away'))
      .map((p, i) => ({
        id: String(p.id == null ? `p${i}` : p.id).replace(/[^a-zA-Z0-9_]/g, '').slice(0, 24) || `p${i}`,
        team: p.team,
        number: String(p.number == null ? '' : p.number).replace(/[^0-9A-Za-zА-Яа-яЁё]/g, '').slice(0, 3),
        totalMs: num(p.totalMs, 0, 600_000, 120_000),
        baseLeftMs: num(p.baseLeftMs, -600_000, 600_000),
        setAtGameMs: num(p.setAtGameMs, 0, 6 * 3600_000),
      })),
    pseq: Math.floor(num(raw.pseq, 0, 9999)),
    rev: Math.floor(num(raw.rev, 0, 1e9)),
    updatedAt: num(raw.updatedAt, 0, tMax),
  };
}
