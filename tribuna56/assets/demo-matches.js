// Демо-каталог для локального превью без настроенного API (localhost или ?demo=1).
// Детерминирован от текущей даты: матчи всегда «в ближайшие выходные»,
// никакого Math.random() — картинка живая, но воспроизводимая.

export function demoMatches(now = new Date()) {
  const base = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  const toSat = (6 - base.getDay() + 7) % 7;
  const sat = new Date(base.getTime() + toSat * 86400_000);
  const sun = new Date(sat.getTime() + 86400_000);
  const at = (d, h, m = 0) =>
    new Date(d.getFullYear(), d.getMonth(), d.getDate(), h, m).toISOString();

  let id = 90000;
  const M = (sport, league, age, home, away, venue, when, extra = {}) => ({
    id: ++id,
    sport,
    league,
    age_group: age,
    team_home: home,
    team_away: away,
    venue,
    address: null,
    starts_at: when,
    duration_min: 90,
    status: 'scheduled',
    stream_url: null,
    highlights_url: null,
    ...extra,
  });

  return [
    // сегодня — один живой эфир для витрины
    M('hockey', 'Товарищеский матч', '2013 г.р.', 'Юбилейный', 'Айсберг',
      'ЛД «Звёздный»', at(base, Math.max(now.getHours(), 9)), { status: 'live' }),
    M('hockey', 'Первенство области', '2012 г.р.', 'Юниор-2012', 'Сармат-2012',
      'ЛД «Звёздный»', at(sat, 9, 0)),
    M('hockey', 'Первенство области', '2013 г.р.', 'Металлург-2013', 'Белые Тигры',
      'ЛД «Айсберг»', at(sat, 11, 30)),
    M('football', 'Первенство города', '2011 г.р.', 'Газовик-2011', 'Факел-2011',
      'Стадион «Газовик»', at(sat, 13, 0)),
    M('basketball', 'КЭС-Баскет, дивизион «Запад»', '2010 г.р.', 'Надежда', 'Спарта',
      'СК «Олимпийский»', at(sat, 15, 30)),
    M('football', 'Кубок области, 1/4 финала', '2013 г.р.', 'Смена-2013', 'Прогресс',
      'Стадион «Оренбург»', at(sun, 10, 0)),
    M('volleyball', 'Первенство области', '2011 г.р.', 'Юность', 'Импульс',
      'СК «Маяк»', at(sun, 12, 0)),
    M('hockey', 'Первенство ПФО, группа Б', '2014 г.р.', 'Сарматы-2014', 'Южный Урал',
      'ЛД «Звёздный»', at(sun, 14, 30)),
  ];
}
