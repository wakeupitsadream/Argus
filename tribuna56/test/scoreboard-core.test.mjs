import test from 'node:test';
import assert from 'node:assert/strict';
import {
  initState, clockElapsed, clockRunning, startClock, stopClock, adjustClock,
  setClockMs, setPeriod, setScore, addPenalty, removePenalty, activePenalties,
  penaltyLeft, setSport, periodLabel, fmtClock, sanitizeState,
  displayedClockMs, adjustDisplayedClock, setDisplayedClockMs, periodExpired, isCountdown,
} from '../assets/scoreboard-core.js';

const T0 = 1_000_000;

test('часы: старт → идут, стоп → замерли, повторный старт продолжает', () => {
  let s = initState('hockey', T0);
  assert.equal(clockElapsed(s, T0 + 5000), 0);
  s = startClock(s, T0);
  assert.equal(clockRunning(s), true);
  assert.equal(clockElapsed(s, T0 + 65_000), 65_000);
  s = stopClock(s, T0 + 65_000);
  assert.equal(clockRunning(s), false);
  assert.equal(clockElapsed(s, T0 + 999_000), 65_000);
  s = startClock(s, T0 + 100_000);
  assert.equal(clockElapsed(s, T0 + 130_000), 95_000);
});

test('часы: коррекция ±, не уходит в минус, ход сохраняется', () => {
  let s = startClock(initState('hockey', T0), T0);
  s = adjustClock(s, -10_000, T0 + 30_000); // 30с − 10с = 20с, часы продолжают идти
  assert.equal(clockRunning(s), true);
  assert.equal(clockElapsed(s, T0 + 40_000), 30_000);
  s = stopClock(s, T0 + 40_000);
  s = adjustClock(s, -999_000, T0 + 41_000);
  assert.equal(clockElapsed(s, T0 + 50_000), 0);
  s = setClockMs(s, 300_000, T0 + 60_000);
  assert.equal(clockElapsed(s, T0 + 70_000), 300_000);
  assert.equal(clockRunning(s), false);
});

test('счет: +1/−1 с ограничениями', () => {
  let s = initState('hockey', T0);
  s = setScore(s, 'home', 1, T0);
  s = setScore(s, 'home', 1, T0);
  s = setScore(s, 'away', -1, T0);
  assert.equal(s.home.score, 2);
  assert.equal(s.away.score, 0);
});

test('удаление: тикает только с часами, пауза замораживает', () => {
  let s = startClock(initState('hockey', T0), T0);
  s = addPenalty(s, 'away', 2, '7', T0 + 60_000); // на 1:00 игрового
  let g = clockElapsed(s, T0 + 90_000);
  assert.equal(penaltyLeft(s.penalties[0], g), 90_000); // прошло 30с из 2:00
  s = stopClock(s, T0 + 90_000);
  g = clockElapsed(s, T0 + 500_000); // часы стоят — штраф стоит
  assert.equal(penaltyLeft(s.penalties[0], g), 90_000);
  s = startClock(s, T0 + 500_000);
  g = clockElapsed(s, T0 + 590_000); // еще 90с игры — штраф вышел
  assert.equal(penaltyLeft(s.penalties[0], g), 0);
  assert.equal(activePenalties(s, 'away', T0 + 590_000).length, 0);
});

test('удаление переживает смену периода (переезжает с остатком)', () => {
  let s = startClock(initState('hockey', T0), T0);
  s = addPenalty(s, 'home', 2, '', T0 + 10_000);
  s = setPeriod(s, 2, T0 + 40_000); // отсидел 30с, остаток 1:30; часы в 0 и стоят
  assert.equal(clockElapsed(s, T0 + 999_999), 0);
  assert.equal(clockRunning(s), false);
  const left = penaltyLeft(s.penalties[0], 0);
  assert.equal(left, 90_000);
  s = startClock(s, T0 + 100_000);
  assert.equal(penaltyLeft(s.penalties[0], clockElapsed(s, T0 + 130_000)), 60_000);
});

test('коррекция часов двигает и остаток штрафа (инвариант игрового времени)', () => {
  // штраф 2:00 с 0:00 истекает на 2:00 игрового времени; часы 1:30 → −1:00 =
  // 0:30 игрового → до истечения снова 1:30
  let s = startClock(initState('hockey', T0), T0);
  s = addPenalty(s, 'home', 2, '', T0);
  s = adjustClock(s, -60_000, T0 + 90_000);
  const left = penaltyLeft(s.penalties[0], clockElapsed(s, T0 + 90_000));
  assert.equal(left, 90_000);
});

test('коррекция назад «воскрешает» только что истекший штраф', () => {
  let s = startClock(initState('hockey', T0), T0);
  s = addPenalty(s, 'home', 2, '', T0); // истекает на 2:00
  // часы дошли до 2:05 — штраф уже скрыт с табло
  assert.equal(activePenalties(s, 'home', T0 + 125_000).length, 0);
  s = adjustClock(s, -10_000, T0 + 125_000); // оператор корректирует: 2:05 → 1:55
  const act = activePenalties(s, 'home', T0 + 125_000);
  assert.equal(act.length, 1);
  assert.equal(penaltyLeft(act[0], clockElapsed(s, T0 + 125_000)), 5_000);
});

test('activePenalties: сортировка по остатку, чужая команда не попадает', () => {
  let s = startClock(initState('hockey', T0), T0);
  s = addPenalty(s, 'away', 5, '9', T0);
  s = addPenalty(s, 'away', 2, '4', T0);
  s = addPenalty(s, 'home', 2, '', T0);
  const act = activePenalties(s, 'away', T0 + 1000);
  assert.equal(act.length, 2);
  assert.equal(act[0].number, '4'); // 2 минуты выйдут раньше
  assert.equal(removePenalty(s, act[0].id, T0).penalties.length, 2);
});

test('setSport: футбол — 2 тайма по 30, штрафы сбрасываются', () => {
  let s = addPenalty(startClock(initState('hockey', T0), T0), 'home', 2, '', T0);
  s = setSport(s, 'football', T0 + 1000);
  assert.equal(s.periodMin, 30);
  assert.equal(s.penalties.length, 0);
  assert.equal(periodLabel(s), '1-й тайм');
  assert.equal(periodLabel({ ...s, period: 3 }), 'ДВ');
});

test('periodLabel: хоккей и овертаймы', () => {
  const s = initState('hockey', T0);
  assert.equal(periodLabel(s), '1-й период');
  assert.equal(periodLabel({ ...s, period: 4 }), 'ОТ');
  assert.equal(periodLabel({ ...s, period: 5 }), 'ОТ2');
});

test('fmtClock', () => {
  assert.equal(fmtClock(0), '0:00');
  assert.equal(fmtClock(65_000), '1:05');
  assert.equal(fmtClock(1_200_000), '20:00');
  assert.equal(fmtClock(-5), '0:00');
});

test('sanitizeState: мусор не роняет, поля ограничены', () => {
  assert.equal(sanitizeState(null).sport, 'hockey');
  assert.equal(sanitizeState('x').home.name, 'Хозяева');
  const s = sanitizeState({
    sport: 'quidditch',
    home: { name: 'A'.repeat(100), score: '5' },
    away: { name: '', score: -3 },
    period: 99,
    clock: { baseMs: 1e12, startedAt: -5 },
    penalties: [
      { team: 'left', totalMs: 1 },
      { team: 'home', totalMs: 1e9, baseLeftMs: 1e9, number: 77777 },
    ],
    tournament: 'T'.repeat(200),
  });
  assert.equal(s.sport, 'hockey');
  assert.equal(s.home.name.length, 40);
  assert.equal(s.home.score, 5);
  assert.equal(s.away.score, 0);
  assert.equal(s.period, 9);
  assert.equal(s.clock.startedAt, 0);
  assert.equal(s.penalties.length, 1); // team:'left' отброшен
  assert.equal(s.penalties[0].totalMs, 600_000);
  assert.equal(s.penalties[0].number, '777');
  assert.equal(s.tournament.length, 80);
});

test('sanitizeState: разметка вырезается (XSS в пульт не проходит), rev/updatedAt клампятся', () => {
  const now = 1_000_000;
  const s = sanitizeState({
    home: { name: '<img src=x onerror=alert(1)>' },
    penalties: [{ team: 'home', id: '"><svg onload=x>', number: '<b>', baseLeftMs: -999_999 }],
    tournament: 'Кубок <script>',
    rev: 9e15,
    updatedAt: 9e15,
    clock: { baseMs: 0, startedAt: 9e15 },
  }, now);
  assert.ok(!s.home.name.includes('<') && !s.home.name.includes('>'));
  assert.match(s.penalties[0].id, /^[a-zA-Z0-9_]+$/);
  assert.equal(s.penalties[0].number, 'b');
  assert.equal(s.penalties[0].baseLeftMs, -600_000); // знак разрешен, но ограничен
  assert.ok(!s.tournament.includes('<'));
  assert.equal(s.rev, 1e9);
  assert.ok(s.updatedAt <= now + 60_000);
  assert.ok(s.clock.startedAt <= now + 60_000); // «часы из будущего» не разносят табло
});

test('sanitizeState: showClock — по умолчанию true, false сохраняется', () => {
  assert.equal(sanitizeState({}).showClock, true);
  assert.equal(sanitizeState({ showClock: false }).showClock, false);
  assert.equal(sanitizeState({ showClock: 'нет' }).showClock, true); // мусор → дефолт
});


test('хоккей: время на убывание — 20:00 → 0:00, футбол — вверх', () => {
  let h = startClock(initState('hockey', T0), T0);
  assert.equal(isCountdown(h), true);
  assert.equal(displayedClockMs(h, T0), 20 * 60000);            // старт периода: 20:00
  assert.equal(displayedClockMs(h, T0 + 90_000), 18.5 * 60000); // прошло 1:30 → 18:30
  assert.equal(displayedClockMs(h, T0 + 25 * 60000), 0);        // пересидели — на нуле
  assert.equal(periodExpired(h, T0 + 20 * 60000), true);

  let f = startClock(initState('football', T0), T0);
  assert.equal(isCountdown(f), false);
  assert.equal(displayedClockMs(f, T0 + 90_000), 90_000);       // футбол считает вверх
});

test('коррекция и «Выставить» — в экранных величинах (для хоккея — остаток)', () => {
  let h = startClock(initState('hockey', T0), T0);
  // прошло 60с (на экране 19:00); «+10с» экрану → 19:10, т.е. наиграно 50с
  h = adjustDisplayedClock(h, 10_000, T0 + 60_000);
  assert.equal(displayedClockMs(h, T0 + 60_000), 19 * 60000 + 10_000);
  assert.equal(clockElapsed(h, T0 + 60_000), 50_000);
  // сверка с табло арены: «Выставить 12:34» = остаток 12:34
  h = setDisplayedClockMs(h, 12 * 60000 + 34_000, T0 + 100_000);
  assert.equal(displayedClockMs(h, T0 + 100_000), 12 * 60000 + 34_000);
  // футбол: те же операции работают в прошедшем времени
  let f = setDisplayedClockMs(initState('football', T0), 5 * 60000, T0);
  assert.equal(displayedClockMs(f, T0), 5 * 60000);
  f = adjustDisplayedClock(f, -60_000, T0);
  assert.equal(displayedClockMs(f, T0), 4 * 60000);
});

test('обратный отсчет не ломает штрафы (внутри всё по наигранному)', () => {
  let h = startClock(initState('hockey', T0), T0);
  h = addPenalty(h, 'away', 2, '7', T0 + 60_000);
  // «+30с» экрану (остаток растет) — штрафу это ДОБАВЛЯЕТ 30с к отбытию? Нет:
  // elapsed уменьшился на 30с, штраф привязан к игровому времени → остаток штрафа +30с
  h = adjustDisplayedClock(h, 30_000, T0 + 90_000);
  const left = penaltyLeft(h.penalties[0], clockElapsed(h, T0 + 90_000));
  assert.equal(left, 120_000); // сидел 30с, откатили 30с игрового — снова полный
});
