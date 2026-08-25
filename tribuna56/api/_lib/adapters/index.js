// Реестр адаптеров источников расписаний. Пустой реестр — норма MVP:
// каталог ведется руками через админку, импорт включается добавлением
// адаптера в массив. Роадмап источников — SOURCES.md в корне проекта.
//
// Контракт адаптера:
//   export const id = 'oofs56';            // попадает в matches.source
//   export const label = 'ООФС (футбол)';
//   export async function fetchMatches({ now }) → NormalizedMatch[]
//     (бросает Error при недоступности источника — оркестратор поймает)
//
// NormalizedMatch:
//   {
//     sourceKey,   // стабильный ключ матча в источнике (или fallbackSourceKey из dedupe.js)
//     sport,       // id из assets/data.js SPORTS
//     league, ageGroup, teamHome, teamAway, venue, address,
//     startsAt,    // ISO с явным смещением Оренбурга: '2026-09-14T12:00:00+05:00'
//     raw,         // исходные данные источника — админ видит их при подтверждении
//   }

import * as infobasket from './infobasket.js';

export const ADAPTERS = [infobasket];
