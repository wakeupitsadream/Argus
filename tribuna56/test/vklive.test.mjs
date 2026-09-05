import test from 'node:test';
import assert from 'node:assert/strict';
import { pickMatchForLive, planLiveTransitions, vkVideoUrl } from '../api/_lib/vklive.js';

const NOW = Date.parse('2026-09-12T09:00:00Z'); // 14:00 по Оренбургу
const at = (h) => new Date(NOW + h * 3600_000).toISOString();
const G = 241086908;

test('pickMatchForLive: ближайший запланированный в окне [-45мин…+3ч от начала]', () => {
  const matches = [
    { id: 1, status: 'scheduled', starts_at: at(0.25) },  // через 15 мин — ближайший
    { id: 2, status: 'scheduled', starts_at: at(2) },     // через 2 часа — в окне, но дальше
    { id: 3, status: 'scheduled', starts_at: at(5) },     // слишком рано включать
    { id: 4, status: 'scheduled', starts_at: at(-4) },    // начался 4 часа назад — поезд ушел
    { id: 5, status: 'live', starts_at: at(0) },          // уже live — не кандидат
  ];
  assert.equal(pickMatchForLive(matches, NOW).id, 1);
  assert.equal(pickMatchForLive([], NOW), null);
  assert.equal(pickMatchForLive([{ id: 9, status: 'scheduled', starts_at: at(5) }], NOW), null);
});

test('planLiveTransitions: начавшийся эфир привязывается к матчу один раз', () => {
  const matches = [{ id: 7, status: 'scheduled', starts_at: at(0.1), stream_url: null }];
  const videos = [{ id: 456, live: 1, live_status: 'started' }];
  const plan = planLiveTransitions(matches, videos, G, NOW);
  assert.deepEqual(plan.setLive, [{ id: 7, stream_url: vkVideoUrl(G, 456) }]);
  assert.deepEqual(plan.setFinished, []);

  // тот же эфир уже привязан → повторно не трогаем
  const attached = [{ id: 7, status: 'live', starts_at: at(0.1), stream_url: vkVideoUrl(G, 456) }];
  const plan2 = planLiveTransitions(attached, videos, G, NOW);
  assert.deepEqual(plan2.setLive, []);
});

test('planLiveTransitions: finished только по явному сигналу VK', () => {
  const matches = [
    { id: 1, status: 'live', starts_at: at(-1), stream_url: vkVideoUrl(G, 100) }, // эфир кончился
    { id: 2, status: 'live', starts_at: at(-1), stream_url: vkVideoUrl(G, 200) }, // еще идет
    { id: 3, status: 'live', starts_at: at(-1), stream_url: 'https://vk.com/video-999_1' }, // ручной LIVE с чужой ссылкой — не трогаем
    { id: 4, status: 'live', starts_at: at(-1), stream_url: vkVideoUrl(G, 300) }, // видео выпало из выборки — не трогаем
  ];
  const videos = [
    { id: 100, live: 1, live_status: 'finished' },
    { id: 200, live: 1, live_status: 'started' },
  ];
  const plan = planLiveTransitions(matches, videos, G, NOW);
  assert.deepEqual(plan.setFinished, [1]);
});

test('planLiveTransitions: анонс (waiting/upcoming) эфиром не считается', () => {
  const matches = [{ id: 7, status: 'scheduled', starts_at: at(0.1) }];
  const videos = [
    { id: 1, live: 1, live_status: 'waiting' },
    { id: 2, live: 1, live_status: 'upcoming' },
    { id: 3, live: 0 }, // обычная запись
  ];
  const plan = planLiveTransitions(matches, videos, G, NOW);
  assert.deepEqual(plan.setLive, []);
});
