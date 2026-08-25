import test from 'node:test';
import assert from 'node:assert/strict';
import { parseCompIds, youthAgeGroup, gameStartsAt, gameToNormalized } from '../api/_lib/adapters/infobasket.js';

// Фрагменты — реальные ответы источников, снятые 25.08.2026 через /api/probe.

const COMPS_HTML = `
<a href="/competitions/139830?apiUrl=https%3a%2f%2freg.infobasket.su">Чемпионат Оренбургской области мужчины</a>
<a href="/competitions/112616?apiUrl=...">Чемпионат Оренбурга женщины</a>
<a href="/competitions/138308?apiUrl=...">"Кубок Оренбуржья" среди юношей 2014 г.р.</a>
<a href="/competitions/138308?x=1">дубль</a>
<a href="/competitions">Соревнования</a>`;

const REAL_GAME = {
  GameID: 1063937,
  GameDate: '07.05.2026',
  HasTime: true,
  GameTime: '08:30',
  GameTimeMsk: '06:30',
  ShortTeamNameAru: 'Надежда-Оренбург',
  ShortTeamNameBru: 'Кременкульбаскет',
  RegionRu: 'Оренбург',
  ArenaRu: 'СКК "Оренбуржье"',
  CompNameRu: 'Группа Б',
  LeagueNameRu: '"Кубок Оренбуржья" среди юношей 2014 г.р.',
};

test('parseCompIds: уникальные id из HTML страницы соревнований', () => {
  assert.deepEqual(parseCompIds(COMPS_HTML).sort(), [112616, 138308, 139830]);
});

test('youthAgeGroup: детские лиги проходят, взрослые — нет', () => {
  assert.equal(youthAgeGroup('"Кубок Оренбуржья" среди юношей 2014 г.р.'), '2014 г.р.');
  assert.equal(youthAgeGroup('Первенство области среди девушек 2012 г.р.'), '2012 г.р.');
  assert.equal(youthAgeGroup('Первенство среди юношей'), 'юноши/девушки');
  assert.equal(youthAgeGroup('Чемпионат Оренбургской области мужчины'), null);
  assert.equal(youthAgeGroup('Чемпионат Оренбурга женщины'), null);
});

test('gameStartsAt: дата dd.mm.yyyy + местное время → ISO с +05:00', () => {
  assert.equal(gameStartsAt(REAL_GAME), '2026-05-07T08:30:00+05:00');
  assert.equal(gameStartsAt({ GameDate: '01.10.2026', HasTime: false }), '2026-10-01T12:00:00+05:00');
  assert.equal(gameStartsAt({ GameDate: 'мусор' }), null);
});

test('gameToNormalized: прошедшая игра отсекается, будущая — нормализуется', () => {
  const nowAfter = { now: new Date('2026-08-25T12:00:00+05:00') };
  assert.equal(gameToNormalized(REAL_GAME, nowAfter), null); // май уже прошел

  const future = { ...REAL_GAME, GameID: 555, GameDate: '03.10.2026', GameTime: '14:00' };
  const n = gameToNormalized(future, nowAfter);
  assert.equal(n.sourceKey, 'ib-555');
  assert.equal(n.sport, 'basketball');
  assert.equal(n.ageGroup, '2014 г.р.');
  assert.equal(n.teamHome, 'Надежда-Оренбург');
  assert.equal(n.teamAway, 'Кременкульбаскет');
  assert.equal(n.venue, 'СКК "Оренбуржье"');
  assert.equal(n.startsAt, '2026-10-03T14:00:00+05:00');
  assert.match(n.league, /Кубок Оренбуржья/);
});

test('gameToNormalized: взрослая лига → null', () => {
  const adult = { ...REAL_GAME, GameDate: '03.10.2026', LeagueNameRu: 'Чемпионат Оренбурга мужчины' };
  assert.equal(gameToNormalized(adult, { now: new Date('2026-08-25T12:00:00+05:00') }), null);
});
