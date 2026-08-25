# Источники расписаний: разведка и роадмап адаптеров

Результаты разведки (август 2026) машиночитаемых источников расписаний
детских матчей (10–16 лет) Оренбурга и области. Реестр адаптеров
(`api/_lib/adapters/index.js`) сейчас пуст — каталог ведется через админку;
адаптеры подключаются по этому роадмапу.

⚠️ Перед написанием каждого адаптера: один тестовый fetch домена именно из
Vercel-функции — российские спортивные сайты могут блокировать не-RU IP.
Если блокируют — регион функции fra1/arn1 или легкий RU-прокси.

## Приоритет 1 — футбол: oofs56.ru (платформа РФС) ⭐

Оренбургский областной футбольный союз. Та же платформа, что mosff.ru и
yflrussia.ru — сервер-рендеренный HTML, парсится обычным fetch без headless
(доказано существующим парсером платформы: github.com/thefrol/protocol-parsers).

Конвейер адаптера:
1. `GET https://oofs56.ru/tournaments` → турниры с «юнош…»/годами рождения → id
2. `GET https://oofs56.ru/tournament/{id}/calendar` → дата, время, тур, стадион, пары команд
3. Опционально `GET /match/{id}` (протокол), `GET /team/{id}/calendar`

Тот же парсер без изменений читает **yflrussia.ru** (ЮФЛ Приволжье,
команды «Оренбург» 2010–2011 г.р.): `tournament/1062800` (U-15) и др.

## Приоритет 2 — баскетбол: Sportoteka JSON API ⭐

Бэкенд russiabasket.ru (и orenburg.russiabasket.ru) — открытый JSON:

```
GET https://basket2.sportoteka.org/api/abc/comps/calendar
    ?tag={tag}&lang=ru&CalendarType=2&Sorting=game.scheduledTime+asc
    &MaxResultCount=50&Season=2027
```

Заголовки: `Origin: https://russiabasket.ru`, `Referer: https://russiabasket.ru/`,
обычный UA. `tag` соревнования снять один раз из Network-вкладки страницы
russiabasket.ru/competitions/424/orenburg/schedule. Ответ: `items[].comp`,
`scores[]` (team1id/team2id, scheduledTime, gameStatus).

## Приоритет 3 — хоккей

- **pfo.fhr.ru / junior.fhr.ru** — окружной портал ФХР (Первенства ПФО,
  «Юниор-Газпром добыча Оренбург», «Сарматы», «Южный Урал» Орск).
  Турнир: `/tournaments/{slug}/calendar/`, матч: `/games/{id}/`.
  Проверить рендер: SSR-HTML либо `window.__NUXT__`/`__NEXT_DATA__` JSON.
- **r-hockey.ru** — агрегатор детского хоккея, статичный HTML, простой fetch:
  `r-hockey.ru/stat/volga/2026/28366/calendar` (Первенство ПФО 2011),
  `r-hockey.ru/team/{id}/calendar`, каталог лиг `/league/{сезон}/children`.
  Хороший фолбэк, если порталы ФХР окажутся сложными.
- **fh56.ru/calendar** — федерация хоккея Оренбургской области (областные
  первенства по годам рождения). Старый самописный CMS, server-rendered.
  ⚠️ Старый домен fh-oren.ru захвачен спамерами — не использовать.
- registry.fhr.ru — закрытая учетная система, публичного API нет. НЕ источник.

## Не годятся / низкий приоритет

- **Волейбол**: волейбол-оренбург.рф (WordPress, расписания файлами/новостями),
  volleyoren.ru (устаревший) — машиночитаемого календаря матчей нет.
- **КЭС-Баскет**: kes-basket.ru/orenburgskaya-oblast — новости и PDF, таблиц нет.
- **Минспорт области** (minsport.orb.ru): календарный план — PDF уровня
  «мероприятие+сроки», без матчей. Годится как справочник существующих первенств.
- **VK-группы лиг**: расписания часто картинками; если понадобится —
  `VK_SERVICE_TOKEN` (env, уже предусмотрен) + wall.get, но это хрупко.
- Наградион (oofs.nagradion.ru): есть анонимный JSON
  `POST /_anon/match_feed/load_props` (tournaments[]={id}), но проверить,
  не легаси ли после переезда ООФС на oofs56.ru.
