# ARGUS — технический план (TECH)

Источник истины для инженерных решений. Пункты со словом «проверить» закрываются на этапе 0 и заменяются фактами (см. раздел «Проверено» в конце).

## Часть 2. Технический план

### 2.1. Стек и сборка

C11 + PSPSDK (тулчейн pspdev, готовая сборка для Ubuntu x86_64). CMake с тулчейн-файлом `${PSPDEV}/psp/share/pspdev.cmake` и макросом `create_pbp_file(TARGET argus TITLE "ARGUS" ICON_PATH … BACKGROUND_PATH … VERSION … MEMSIZE 1)` — см. `src/base/CreatePBP.cmake` в клоне pspsdk. Рендер — напрямую через sceGu/sceGum (фиксированный конвейер), Python 3.12 для инструментов (pillow, numpy, fonttools). Никаких сторонних движков и Lua: путь «C + sceGu» имеет больше всего примеров в обучении моделей и полный контроль над картинкой.

### 2.2. Референсы кода для переиспользования

Клон pspsdk в scratchpad сессии (`pspsdk/src/`): `samples/gu/cube` (инициализация GU, двойная буферизация, дисплей-лист), `gu/zbufferfog` (аппаратный туман), `gu/ortho` (ортокамера), `gu/lights` и `gu/celshading` (освещение по нормалям, проход контуров), `gu/rendertarget` (рендер в текстуру), `gu/sprite` и `gu/blend` (билборды, аддитив), `gu/text`, `gu/vsync30FPS` (запасной режим 30 fps), `gu/common/callbacks.c` (кнопка Home и корректный выход), `gu/common/vram.c` (аллокатор VRAM), `audio/polyphonic` (pspaudiolib, callback-микшер), `savedata/utility` (системное сохранение), `controller/basic` (ввод), `base/CreatePBP.cmake` и `base/pspdev.cmake` (сборка EBOOT).

### 2.3. Дерево репозитория

```
psp/
  CLAUDE.md  Makefile (обёртка)  CMakeLists.txt (PSP-таргет)
  docs/          GDD.md (часть 1 этого плана), TECH.md (часть 2–3)
  core/          чистый C11 без PSP-заголовков, собирается host-gcc
  platform/psp/  sceGu, аудио, ввод, файлы — единственное место с <psp*.h>
  tools/         Python: meshgen.py levelc.py fontgen.py mkassets.py run_emu.py imgdiff.py
  assets/        meshes/*.toml palettes.toml strings.csv font/*.ttf audio/*.wav sfx.toml xmb/
  levels/        hub.toml mirrors_1.toml … (TOML: ASCII-карта высот + сущности)
  tests/         unit/ autoplay/ golden/ minitest.h platform_stub.c
  ci/            session-start.sh build-ppsspp.sh workflows/*.yml
  build/         (gitignore) assets/ psp/ ARGUS/ memstick/ shots/
```

Граница core↔platform: core получает `input_state_t`, отдаёт `frame_t` — плоский список команд рендера (меши с матрицами, спрайты, текст, параметры тумана, неба, оверлея). platform только исполняет `frame_t`. Вся логика и «что нарисовано» проверяются на хосте.

| Модуль | Ключевые функции |
|---|---|
| `core/math3.h`, `core/ease.h` | `v3_*`, `m4_mul`, `m4_look_at`, `m4_ortho`, `project_to_screen`; `ease_in_out_cubic`, `ease_out_expo`, `tween_start/update/done` |
| `core/game.h`, `core/screens.h` | `game_init`, `game_tick(input)` (1/60 с), `game_build_frame(frame_t*)`; стек экранов `screen_push/pop/replace` |
| `core/level.h`, `core/walk.h` | `level_load(blob,len)`, `level_cell(x,z)`, `level_entities()`; `walk_move(level,pos,delta,radius)`, `walk_height_at`, `walk_can_stand(cell,cam_angle)` |
| `core/entity.h`, `core/puzzles.h` | `entity_interact`, `signal_set/get`, `links_propagate()`; `beam_trace`, `segment_rotate`, `block_push`, `water_set_level`, `memory_input`, `sleeper_update` |
| `core/observe.h`, `core/camera.h` | `observe_is_watched(cam,e,look_active)`; `camera_set_angle(i)`, `camera_lock`, `camera_update(target,dt)`, `camera_matrices` |
| `core/quest.h`, `core/world.h`, `core/save.h` | битсет 256 флагов мира, `quest_on_event`, `quest_reward_check`; `save_serialize/deserialize` + CRC32 |
| `core/i18n.h`, `core/text_layout.h` | `i18n_set_lang`, `str(STR_ID)`, `utf8_next`, `text_layout(font,utf8,quads[])`, `font_kern(a,b)` |
| `core/audio_synth.h`, `core/particles.h`, `core/frame.h` | `synth_note_on(voice,freq,wave,adsr)`, `ambient_set(id,fade)`, `audio_mix(int16*,n)`; `particles_emit/update`; `frame_push_mesh/sprite/text`, `frame_set_env(fog,sky,desat)` |
| `core/autoplay.h`, `core/platform.h` | `autoplay_load`, `autoplay_step(frame_no,input*,cmd*)`; `plat_read_file`, `plat_write_file`, `plat_time_us`, `plat_log` |
| `platform/psp/gu_render.h`, `gu_mesh.h` | `r_init`, `r_begin`, `r_draw_frame(frame_t*)`, `r_end`, `r_tex_upload`, `r_screenshot_bmp(name)`; `mesh_load(path,palette)`, `mesh_recolor(palette,desat)` |
| `platform/psp/{audio,input,fs,save,sys}_psp.h` | `audio_start` (pspaudiolib-колбэк → `audio_mix`), `input_poll`, `fs_init(argv0)`, `save_write/read`, `sys_setup_callbacks` (как `callbacks.c`) |

### 2.4. Рендер-конвейер

Буферы: ширина 512, экран 480×272, цвет 8888 (градиенты без полос), Z 16-бит, выделение VRAM только через свой `vram_alloc` (копия `gu/vram.c`).

| Объект VRAM | Байт |
|---|---|
| Два кадровых буфера 8888 | 1 114 112 |
| Z-буфер 16-бит | 278 528 |
| Атлас шрифта T8 512×256 + CLUT | 132 096 |
| Спрайты свечения и частиц 4444, 4×64×64 | 32 768 |
| Текстура зерна T8 32×32 | 1 024 |
| **Итого / запас из 2 097 152** | **≈1 558 500 / ≈538 000** |

Запасной вариант при нехватке VRAM — буферы 5650 (экономия 557 056 байт) с включённым дизерингом.

Решения:
1. **Плоское затенение.** Статические меши хранят по три вершины на треугольник (без индексов), цвет грани запечён при загрузке: `color = palette[slot] · ao · (ambient + max(0, N·L))`. При `GU_SMOOTH` с одинаковыми цветами вершин заливка плоская; лёгкий градиент от AO внутри грани — желаемый эффект. Память: 4 000 треугольников × 3 × 16 байт ≈ 192 КБ на остров.
2. **Два формата вершин**, оба float: `VTX_STATIC {u32 color; float x,y,z}` — `GU_COLOR_8888|GU_VERTEX_32BITF` для островов; `VTX_LIT {u32 color; float nx,ny,nz; float x,y,z}` — с аппаратным светом (`sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &dir)`, `sceGuAmbient`, `sceGuColorMaterial(GU_AMBIENT_AND_DIFFUSE)`) для вращающихся и движущихся объектов. Порядок полей строго: texcoord → color → normal → position.
3. **Цвета не в ассетах.** На диске меш хранит `slot(0..7) + ao`, а не RGB; цвет резолвится при загрузке из палитры региона. Смена региона, финала, обесцвечивание — повторный резолв без правки ассетов.
4. **Туман** — `sceGuFog(near, far, sky)` + `GU_FOG` только в проходе непрозрачной геометрии. Работоспособность тумана при ортопроекции проверить в PPSSPP и на железе на этапе 0; запасной вариант — пересчёт цветов вершин на CPU по расстоянию при смене ракурса.
5. **Камера** — `sceGumOrtho` + `sceGumLookAt`, 4 угла yaw 45/135/225/315°, наклон 30–35°. Свет задаётся в мировых координатах (сверить с примером `gu/lights`).
6. **Небо** — 2D-квад с цветами вершин верх/низ, поверх — тайл «зерна» 32×32 с альфой 3–5% и диски-луны; глубина выключена.
7. **Свечение, лучи, частицы** — `GU_SPRITES` с мягкой радиальной текстурой, аддитивное смешивание, запись в Z и туман выключены.
8. **Обесцвечивание в режиме взгляда** — этап 1: полноэкранный квад к серому (alpha 35–45% с easing) плюс туман и небо к серому; этап 3: второй буфер вершин с серой палитрой и переключение указателя (истинная серость, +≈0,5 МБ RAM на остров). Рендер в текстуру с CLUT-трюками не делать — хрупко.

Порядок кадра: clear (цвет неба-верх, Z=0, `GU_GEQUAL`, `sceGuDepthRange(65535,0)`) → небо, луны, зерно (2D) → непрозрачное: остров, механизмы, персонаж, NPC (туман, свет, cull) → водная плоскость (альфа, без записи Z) → аддитив: свечения, лучи, частицы → оверлей взгляда и виньетка → 2D-текст (мировые точки проецируются на CPU) → debug-оверлей → занавес затемнения. Один `sceGuStart … sceGuFinish; sceGuSync; sceDisplayWaitVblankStart; sceGuSwapBuffers` на кадр.

Бюджет при 60 fps: ≈6 000 треугольников на кадр (остров 2 500–4 000, механизмы 800, Око 200, NPC 300, частицы 400, текст 200), лимит 8 000 и ≤100 draw-call, запас ≥1,5×. Узкое место скорее CPU (матрицы на вызов), чем GPU.

### 2.5. Форматы данных

| Данные | Исходник → инструмент → бинарь |
|---|---|
| Меши примитивов | `assets/meshes/*.toml` → `meshgen.py` → `.msh`: заголовок 64 Б (`"AMSH"`, версия, формат, count, bbox), далее N×{float3 pos, float3 normal, u8 slot, u8 ao, u16 pad}, вершины с offset 64 (16-выравнено), LE |
| Острова | `levels/*.toml` (ASCII-карта высот тайлов + `[[entity]]` с компонентами) → `levelc.py` → `island_N.msh` + `island_N.lvl`: клетки {u8 height×0.25, u8 flags, u8 segment, u8 pad}, инстансы, сущности {type, name_id, cell, params i32[8]}, связи {src,out,dst,in}, порталы, камера {angle, lock, bounds}, palette_id, ambient_id |
| AO | правила в `levelc.py`: соседние высоты тайлов, низ стен темнее; без трассировки |
| Палитры | `palettes.toml` → `.pal` + `palette_ids.h`: 8 слотов RGBA на регион + серая палитра, туман near/far |
| Строки | `strings.csv` (id, ru, en) → `strings_ru.bin`, `strings_en.bin` + `strings_ids.h` (enum): таблица offset u32 + UTF-8 |
| Шрифт | Manrope/Inter (OFL) → `fontgen.py` (Pillow растр, fontTools для кернинга GPOS) → `font.tex` (T8 альфа 512×256, ASCII + кириллица + «»—…, 2 кегля) + `font.fnt` (глифы и таблица кернинга для bsearch) |
| Звук | WAV 22 кГц моно → `.pcm` s16; `sfx.toml` (волна, ноты аккорда региона, ADSR) → `.sfx`; микшер pspaudiolib 44,1 кГц стерео, 1024 сэмпла на колбэк |
| Сохранение | старт: плоский `save.bin` рядом с EBOOT (магия, версия, CRC32, флаги мира, позиция, язык; запись во временный файл + переименование); этап 3: `sceUtilitySavedata` в режимах AUTOSAVE/AUTOLOAD с иконкой |

Пути только через `fs_init(argv[0])` — каталог EBOOT вычисляется, никаких зашитых `ms0:/` (значение `argv[0]` под PPSSPP headless проверить на этапе 0).

### 2.6. Игровая логика

1. **Цикл** — логика фиксирована 60 Гц, рендер каждый vblank, аккумулятор с потолком 3 тика. В autoplay-режиме строго 1 тик = 1 кадр (детерминизм). Если на железе нет 60 fps — двойной vblank по образцу `vsync30FPS` и 2 тика на кадр.
2. **Экраны** — стек: BOOT → TITLE → GAME {EXPLORE, LOOK, INTERACT, CAM_ROTATE, TRAVEL} ⇄ PAUSE → ENDING_A/B → CREDITS; переходы через общий занавес с easing.
3. **Сущности** — пулы фиксированного размера (`MAX_ENTITIES 128`), tagged union по типу: EMITTER, MIRROR, PRISM, RECEIVER, SLEEP_WALL, LEVER, SEGMENT, BLOCK, PLATE, WATER, FLOAT_BLOCK, MEMORY_PANEL, SLEEPER, ECHO_NPC, SMALL_EYE, BIG_EYE, STONE_TEXT, DOOR, TRIGGER. Связи — таблица сигналов в данных: `links_propagate()` разносит булевы выходы по входам; «глаз открыт» = вход RECEIVER→EYE истинен. Любая головоломка собирается в TOML без правок C.
4. **Семейства** — луч: целочисленный марш по сетке в 4 направлениях, зеркало 0..3, призма делит, спящая стена проходима только при `observe_is_watched`; вращение: SEGMENT держит 4 предрассчитанных карты тайлов, анимация 0,6 с с блокировкой ввода; блоки и плиты: сокобан по клеткам с tween; вода: WATER.level меняет высоту плавучих тайлов и проходимость; память: панель хранит последовательность, ошибка = сброс и тон; «замри»: SLEEPER идёт по путевым точкам только когда не в кадре (флаг inverted для статуй библиотеки).
5. **Квесты и флаги** — битсет 256 флагов мира, события `EV_EYE_OPENED`, `EV_ITEM`, `EV_TALK`; квест = таблица условий → награда в данных.
6. **Коллизии** — сетка с высотами, не навмеш: персонаж — круг радиуса 0,3, движение резолвится по осям раздельно (скольжение вдоль стен), переход разрешён при |Δh| ≤ 0,3 или на клетке-лестнице (интерполяция по рампе = автоподъём). Невозможная геометрия — «рёбра-связки» между клетками, действительные только при заданном угле камеры.
7. **Камера** — цель-игрок с экспоненциальным сглаживанием, зажим по границам острова, поворот 0,5 с кубическим easing, режим взгляда — ортомасштаб ×0,85 за 0,25 с, `lock` из данных уровня отключает L/R.

### 2.7. Тестирование без приставки

1. **Хост** — `tests/minitest.h` (~50 строк: `TEST`, `CHECK`, `CHECK_EQ`, `CHECK_NEAR`), сборка `cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined`. Тесты на `walk`, `beam`, `segment`, `blocks`, `water`, `memory`, `observe`, `save` (round-trip + CRC), `i18n`, `text_layout`, `audio_synth`, `ease`, плюс уровневые: загрузить `.lvl`, выполнить взаимодействия, проверить, что глаз открылся. ASan ловит выравнивание и переполнения раньше железа.
2. **Autoplay (debug-сборка)** — файл `autoplay.txt` рядом с EBOOT: `@120 press X`, `@200 stick -1.0 0.0`, `@300 look on`, `@400 shot mirrors_1_solved`, `@401 assert flag EYE_M1=1`, `@402 assert fps>=58`, `@403 assert heap_free>=4000000`, `@500 quit`. Скриншот: чтение кадрового буфера из VRAM → BMP через `sceIoWrite` в `shots/`; `run_emu.py` конвертирует в PNG. Работает и на приставке. Дублирующий путь — скриншот средствами самого PPSSPPHeadless.
3. **Золотые скриншоты** — только из программного рендера PPSSPP (детерминирован при том же бинаре и кадре); `imgdiff.py`: провал, если >0,5% пикселей отличаются более чем на 8/255; ≤10 канонических кадров. Основная проверка — `assert` по флагам, картинки — для просмотра Claude (Read показывает PNG) и регрессий стиля.
4. **Debug-оверлей (SELECT)** — fps, мс CPU и GPU, треугольники и draw-call, `sceKernelTotalFreeMemSize/MaxFreeMemSize`, VRAM, число сущностей, кадр autoplay.

### 2.8. Бутстрап веб-сессии и CI

`ci/session-start.sh` (идемпотентен; подключается как SessionStart-хук через skill `session-start-hook`): если нет `~/pspdev/bin/psp-gcc` — скачать тулчейн из релиза `latest` pspdev/pspdev (curl с ретраями), распаковать в `~/pspdev`; экспортировать `PSPDEV` и `PATH` через `$CLAUDE_ENV_FILE`; `pip install pillow numpy fonttools` в venv; PPSSPPHeadless — скачать готовый бинарь из релиза `tools-v1` собственного репозитория (собирается один раз CI-джобом), при отсутствии — `ci/build-ppsspp.sh` в фоне; самопроверка версий.

`Makefile`: `assets` (mkassets.py, инкрементально), `psp` (cmake + ninja с тулчейн-файлом → `build/ARGUS/{EBOOT.PBP, data/}`), `test`, `run-emu SCRIPT=… SECONDS=…` (сборка `build/memstick/PSP/GAME/ARGUS/`, запуск, сбор `shots/`), `golden-check`, `golden-update`, `dist` (zip), `clean`.

GitHub Actions: `psp-build.yml` — контейнер `pspdev/pspdev:latest`, `make assets psp`, артефакт `ARGUS.zip`, релиз по тегу `v*`; `psp-test.yml` — ubuntu-latest: `make test`, smoke-прогон PPSSPP по autoplay, скриншоты как артефакт.

### 2.9. CLAUDE.md для `psp/` — правила для любой модели

1. `core/` не включает `<psp*.h>`; сначала `make test`, потом `make psp`.
2. Всё, что читает GPU (вершины, индексы, текстуры, CLUT, дисплей-лист), выровнено на 16 байт; после записи CPU — `sceKernelDcacheWritebackRange` или `WritebackAll` до отрисовки.
3. Не менять память, на которую ссылается текущий кадр; временные вершины — только через `sceGuGetMemory`.
4. Экран 480×272, ширина буфера 512, Z 16-бит, `sceGuDepthRange(65535,0)` + `GU_GEQUAL` + clear depth 0.
5. Текстуры — степень двойки ≤512×512; после изменения — `sceGuTexFlush`; VRAM только через `vram_alloc`, бюджет в разделе 2.4.
6. Цвет везде `0xAABBGGRR`.
7. Только два формата вершин из `gu_vertex.h`; порядок полей texcoord → color → normal → position; новые структуры не изобретать.
8. Один `sceGuStart … sceGuFinish; sceGuSync; WaitVblank; SwapBuffers` на кадр.
9. Каждый проход рендера сам выставляет всё состояние (blend, depth mask, fog, lighting, texture) и не полагается на предыдущий.
10. Логика — фиксированный шаг 1/60; движение, цвет, масштаб только через `tween` + `ease`; никаких линейных переходов камеры и UI.
11. Прыжка нет; перемещение только через `walk_move` по сетке; физики нет.
12. В геймплее нет `malloc`; пулы фиксированного размера с явными лимитами; выделение только при загрузке уровня.
13. Пути только через `fs_path()` от `argv[0]`; никаких `ms0:/` в коде; бинарные файлы — магия + версия, LE.
14. `PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU)`; аудио-колбэк короткий, без malloc, IO и блокировок.
15. Строки только через `STR_*` из сгенерированного заголовка, UTF-8, обе колонки в `strings.csv`.
16. Уровни и головоломки — в TOML; новый экземпляр головоломки не требует правок C; новый тип компонента — только с unit-тестом.
17. Бюджеты: ≤8 000 треугольников и ≤100 draw-call на кадр, ≤16 МБ кучи; при превышении — профилировать, не добавлять.
18. Перед коммитом: `make test` и `make run-emu` со smoke-скриптом; золотые кадры обновлять отдельным коммитом.
19. Не угадывать API: сверяться с `$PSPDEV/psp/sdk/include`; непроверенное на железе помечать «проверить» в коде и коммите.
20. Стиль: C11, 4 пробела, snake_case, префикс модуля, `static` по умолчанию, без VLA и рекурсии в геймплее, host-сборка с `-Wall -Wextra -Werror`.
21. Отладочный вывод — `dbg_log` и оверлей, не `pspDebugScreenPrintf` в игровых сборках; `build/` и `memstick/` не коммитить.

---

## Часть 3. Этапы, приёмка, риски

| Этап | Задачи | Критерии приёмки |
|---|---|---|
| 0. Бутстрап (неделя 1) | скелет, `session-start.sh`, CMake + Makefile, `minitest`, `meshgen` + `levelc` (первый остров из тайлов), палитра, туман, небо, 4 ракурса камеры, debug-оверлей, BMP-скриншот, `run_emu.py`, CI, проверка на приставке | `make test` зелёный; `make psp` < 2 мин; `make run-emu` даёт PNG острова с туманом; артефакт CI; на приставке 5 минут без падения, оверлей ≥59 fps на острове 5 000 треугольников |
| 1. Вертикальный срез (недели 2–3) | хаб + 1 остров Сада Зеркал, Око и `walk`, поворот камеры, режим взгляда, луч и зеркала ×3, открытие глаза с тоном, отголосок с квестом, шрифт RU/EN, сохранение, эмбиент, autoplay-скрипты, первые золотые кадры | три головоломки решаются autoplay с `assert`; ≥59 fps на железе; куча < 12 МБ; переключение языка; сохранение переживает перезапуск |
| 2. Контент (недели 4–7) | остальные регионы, 6 семейств, 18 + 4 головоломки, 6 квестов, павлиний хвост, оба финала, все строки, XMB-иконка и фон | сквозной autoplay-прогон проходит; на каждую головоломку host-тест; ручное прохождение 1–2 часа |
| 3. Полировка (недели 8–9) | переходы, частицы, отражения (зеркальная копия геометрии под водой), буферы обесцвечивания, `sceUtilitySavedata`, suspend/resume (`scePowerRegisterCallback`), 333 МГц (проверить на CFW), релиз | ни одной локации < 55 fps на PSP-1000; запас кучи ≥ 4 МБ; золотые кадры стабильны; релиз по тегу из CI |

| Риск | Сигнал | Запасной вариант |
|---|---|---|
| Нет 60 fps на железе | оверлей: GPU > 16 мс или CPU > 14 мс | 30 fps через двойной vblank + 2 тика на кадр; меньше draw-call; 16-битные вершины |
| Не хватает RAM | `MaxFreeMemSize` < 4 МБ | острова грузятся по одному; убрать буферы обесцвечивания; шрифт T4; PCM 11 кГц |
| Туман и свет ведут себя иначе при орто | сравнение PPSSPP ↔ железо на этапе 0 | пересчёт цветов вершин на CPU при смене ракурса |
| Флаги и пути PPSSPP headless | этап 0, задача-ворота | скриншот по таймауту средствами эмулятора; запуск из структуры memstick |
| Скачивание тулчейна ломается | падение `session-start` | зеркало архива в релизе `tools-v1` своего репо |
| Баги только на железе (кэш, выравнивание) | зависание при старте острова | чек-лист железа в конце каждого этапа; правила 2–3 CLAUDE.md; ASan на хосте |

## Верификация

1. `make test` — юнит-тесты `core/` на хосте с ASan.
2. `make psp` — сборка `build/ARGUS/EBOOT.PBP` тулчейном pspdev.
3. `make run-emu SCRIPT=tests/autoplay/smoke.txt` — прогон в PPSSPPHeadless, скриншоты в `build/shots/*.png`, просмотр через Read.
4. Артефакт GitHub Actions → карта памяти → запуск на PSP; на каждом этапе чек-лист железа (5 минут без падения, оверлей fps и памяти).

## Проверено на этапе 0 (2026-09-21)

Факты ниже подтверждены запуском в этой сессии или первоисточниками (ссылки в скобках); слово «проверить» в разделах выше для этих пунктов снято.

1. **Тулчейн.** Релиз pspdev `v20260901` (ежемесячные релизы, ассет `pspdev-ubuntu-latest-x86_64.tar.gz`, ~170 МБ, внутри каталог `pspdev/`), psp-gcc 15.2.0; ставится за ~10 с распаковкой в `~/pspdev`. CMake-тулчейн `~/pspdev/psp/share/pspdev.cmake` определяет `PSP`, включает `CreatePBP.cmake`. `PSP_LARGE_MEMORY` в CMake не существует — большая память включается через `MEMSIZE 1` в `create_pbp_file` (пишется в PARAM.SFO). Docker-образ `pspdev/pspdev:latest` (и `ghcr.io/pspdev/pspdev`) — Alpine, тулчейн в `/usr/local/pspdev`, без cmake/python — ставятся `apk add`; JS-actions GitHub внутри Alpine не работают, поэтому CI собирает через `docker run` на ubuntu-latest.
2. **Порядок библиотек.** `target_link_libraries(argus PRIVATE pspgum pspgu pspge pspdisplay pspctrl m pspsdk c psputility pspuser pspkernel)` — как в `build.mak`. `psp-fixup-imports` печатает «stubs out of order» только для нестрипнутого ELF, на стрипнутом (что и упаковывается в EBOOT) молчит; EBOOT запускается штатно. При смене набора библиотек — перепроверять.
3. **PPSSPPHeadless.** Готовых Linux-бинарников нет (CI PPSSPP удаляет их из релизов). Сборка из master требует SDL3, которого нет в Ubuntu 24.04; работает `-DHEADLESS=ON -DHEADLESS_CROSS=ON` плюс два патча guard'ов в `headless/Headless.cpp` (`|| !defined(SDL)`), всё в `ci/build-ppsspp.sh`; ~12 минут на 4 ядрах, бинарник 21 МБ, GPU и X не нужны. Флаги: `--graphics=software --timeout=<с> --screenshot-save=<file.png> --memstick=<dir>`; EBOOT.PBP запускается напрямую по пути `<memstick>/PSP/GAME/ARGUS/EBOOT.PBP`. Эмулятор не троттлит: 161 кадр проходит за 0,3 с, поэтому проверки fps в autoplay должны опираться на эмулированное время, а не на стенное.
4. **`argv[0]`** под `--memstick` = `ms0:/PSP/GAME/ARGUS/EBOOT.PBP`; относительные пути через `fs_path()` работают и на чтение (`data/`), и на запись (`shots/*.bmp`, `sceIoMkdir`). На приставке путь тот же по построению.
5. **Туман при ортопроекции** работает (программный рендер PPSSPP): считается от глаза камеры, поэтому дистанции палитры заданы относительно плоскости цели и прибавляются к `CAM_DIST` в `game_build_frame`. Проверка на железе — при первом запуске на приставке.
6. **Обход граней.** `levelc.py` генерирует обход против часовой стрелки снаружи, рендер использует `sceGuFrontFace(GU_CCW)` + `GU_CULL_FACE`; на скриншотах все грани видны, вывернутых нет.
7. **Скриншот из игры**: чтение показанного буфера по некэшированному адресу VRAM (`sceGeEdramGetAddr() | 0x40000000`) → BMP 24 бит через `sceIoWrite`; `tools/run_emu.py` конвертирует в PNG. Эталонные кадры этапа 0 — `docs/img/stage0_hub_a0.png`, `stage0_hub_a1.png`.
8. **Прошивка.** ARK-4 закончен (последний релиз v4.20.69 r206, май 2025; репозиторий архивирован в августе 2026); актуальная CFW — **ARK-5** (PSP и ePSP на Vita), ставится инсталлятором FasterARK поверх OFW 6.60/6.61 на любой модели PSP; есть непостоянный (полу-привязанный) вариант через SAVEDATA без записи во flash. Подписанные homebrew для OFW — легаси 2011 года, не используем.
9. **Библиотеки psp-packages** (ставятся `psp-pacman -Sy <pkg>`, часть уже в тулчейне): `sdl2`, `sdl2-image/mixer/ttf`, `sdl3`, `libpng`, `zlib`, `freetype2`, `libintrafont` (системные шрифты PSP, есть флаги UTF-8 и CP1251, покрытие кириллицы в `ltn*.pgf` не подтверждено — поэтому свой атлас), `oslib` (OSLib Mod 1.5.1), `stb`, `libxmp`, `libmikmod`, `libvorbis`, `pspgl`, `raylib`. Нам на этапах 0–1 не нужны: рендер напрямую через sceGu.
10. **Память.** PSP-1000: пользовательский раздел 24 МБ; на 2000+ с `MEMSIZE=1` — ~52 МБ (по данным сообщества, не измерено). Куча объявлена `PSP_HEAP_SIZE_KB(-1024)`.

## Решения и допущения вех 1–5 (2026-09-22)

Записано, чтобы следующая сессия не переоткрывала эти вопросы заново.

**Линковка.** В `target_link_libraries` перечисляются только библиотеки приложения.
Явные `-lc -lm -lpspuser -lpspkernel` ломают порядок импорт-стабов: `psp-fixup-imports`
отказывается чинить импорты, и любой вызов `sce*` уходит в мусор (проявлялось как
случайные «Invalid jump» и порча кучи). Остальное добавляют specs `psp-gcc`.

**Кэш данных.** Вершины, которые пишет CPU (`mesh_load`, `mesh_recolor`), сбрасываются
через `plat_gpu_writeback` → `sceKernelDcacheWritebackRange`. Без этого на железе GE
читает старое содержимое; в PPSSPP ошибка не видна вообще.

**Граница core ↔ платформа.** Вся логика строит `frame_t`, платформа его исполняет.
Поэтому весь геймплей проверяется на хосте под ASan, а на приставке остаётся только
рендер, ввод, файлы и звук.

**Переход между островами.** У перехода свой занавес (`game_t.travel`), независимый от
занавеса экранов: иначе пауза поверх смены острова спорила бы за одну шкалу. Уровень
подменяется в момент полной черноты; старый уровень освобождается только после удачной
загрузки нового, поэтому сбой чтения не рушит игру.

**Звук.** Микшер целиком в `core/audio.c` (таблица синуса, без libm в колбэке), платформа
отдаёт буфер через pspaudiolib. Эмбиент-петля региона — сырой PCM 22 050 Гц, играется
с шагом 0,5 на 44 100. Прошлый буфер петли освобождается не сразу, а через 12 кадров:
его ещё может читать поток звука. Колбэк снимается до `game_shutdown` (`audio_psp_stop`).

**Сохранение.** `save_write_file` пишет `save.tmp` и подменяет им `save.bin` через
`plat_rename_file` (на PSP — `sceIoRemove` + `sceIoRename`). Обрыв питания посреди записи
оставляет прежнее сохранение целым. Если переименование не поддерживается, пишем напрямую.

**Автопрогон.** Скрипт `autoplay.txt` читается по возрастанию кадра — разбор отвергает
скрипт, где кадр уменьшился (раньше такие события молча не срабатывали). Команда
`level <имя> [вход]` переносит Око на нужный остров, `set <счётчик> <число>` выставляет
прогресс: без них тест каждого острова начинался бы с пешего пути от хаба.

**Эмулятор в CI.** Релиз `tools-v1` с готовым `PPSSPPHeadless` не опубликован: пуш тегов
из рабочей сессии запрещён (HTTP 403). Поэтому в CI остаются хост-тесты, сборка EBOOT и
проверка всех `.lvl` независимым разборщиком, а прогоны в эмуляторе делаются локально
(`ci/session-start.sh` собирает эмулятор в фоне, если его нет). Чтобы ускорить сессии,
тег `tools-v1` нужно поставить вручную — дальше `tools-build.yml` опубликует бинарь сам.

**Золотые кадры.** `tools/imgdiff.py` считает долю пикселей, отличающихся больше чем на
8 уровней по каналу; порог — 0,5 % кадра. `make golden-check` сравнивает `build/shots`
с `tests/golden`, `make golden-update` обновляет эталоны (отдельным коммитом с причиной).
