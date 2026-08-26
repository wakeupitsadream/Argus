import test from 'node:test';
import assert from 'node:assert/strict';
import { dateKey, formatTime, formatDayLabel, formatMatchDate, vkEmbedUrl } from '../assets/format.js';

test('dateKey: UTC → оренбургские сутки (+5)', () => {
  assert.equal(dateKey('2026-09-14T07:00:00Z'), '2026-09-14'); // 12:00 местного
  assert.equal(dateKey('2026-09-14T20:00:00Z'), '2026-09-15'); // 01:00 следующего дня
  assert.equal(dateKey('мусор'), null);
});

test('formatTime: рендер по Оренбургу', () => {
  assert.equal(formatTime('2026-09-14T07:00:00Z'), '12:00');
  assert.equal(formatTime('2026-09-14T12:00:00+05:00'), '12:00');
});

test('formatDayLabel: Сегодня/Завтра относительно now', () => {
  const now = '2026-09-14T08:00:00+05:00';
  assert.equal(formatDayLabel('2026-09-14T18:00:00+05:00', now), 'Сегодня');
  assert.equal(formatDayLabel('2026-09-15T10:00:00+05:00', now), 'Завтра');
  assert.match(formatDayLabel('2026-09-19T10:00:00+05:00', now), /^Суббота, 19 сентября$/);
});

test('formatMatchDate содержит день и время', () => {
  const s = formatMatchDate('2026-09-14T12:00:00+05:00');
  assert.match(s, /14 сент/);
  assert.match(s, /12:00/);
});

test('vkEmbedUrl: vk.com/video-oid_id', () => {
  assert.equal(
    vkEmbedUrl('https://vk.com/video-123456_654321'),
    'https://vk.com/video_ext.php?oid=-123456&id=654321&hd=2',
  );
});

test('vkEmbedUrl: vkvideo.ru и положительный oid', () => {
  assert.equal(
    vkEmbedUrl('https://vkvideo.ru/video-220754053_456239017'),
    'https://vk.com/video_ext.php?oid=-220754053&id=456239017&hd=2',
  );
  assert.equal(
    vkEmbedUrl('https://vk.com/video123_456'),
    'https://vk.com/video_ext.php?oid=123&id=456&hd=2',
  );
});

test('vkEmbedUrl: ссылка с параметром z=', () => {
  assert.equal(
    vkEmbedUrl('https://vk.com/feed?z=video-111_222'),
    'https://vk.com/video_ext.php?oid=-111&id=222&hd=2',
  );
});

test('vkEmbedUrl: ключ доступа list пробрасывается в плеер', () => {
  assert.equal(
    vkEmbedUrl('https://vkvideo.ru/video-188914503_456239752?list=2fbdf76b6a4901fbea'),
    'https://vk.com/video_ext.php?oid=-188914503&id=456239752&hd=2&list=2fbdf76b6a4901fbea',
  );
  // подозрительный list не пробрасываем
  assert.equal(
    vkEmbedUrl('https://vk.com/video-1_2?list=a"b'),
    'https://vk.com/video_ext.php?oid=-1&id=2&hd=2',
  );
});

test('vkEmbedUrl: готовый video_ext.php проходит как есть', () => {
  const src = 'https://vk.com/video_ext.php?oid=-1&id=2&hash=abc';
  assert.equal(vkEmbedUrl(src), src);
});

test('vkEmbedUrl: без протокола — достраиваем https', () => {
  assert.equal(
    vkEmbedUrl('vk.com/video-9_8'),
    'https://vk.com/video_ext.php?oid=-9&id=8&hd=2',
  );
});

test('vkEmbedUrl: мусор и чужой домен → null', () => {
  assert.equal(vkEmbedUrl('просто текст'), null);
  assert.equal(vkEmbedUrl('https://youtube.com/watch?v=video-1_2'), null);
  assert.equal(vkEmbedUrl(''), null);
  assert.equal(vkEmbedUrl(null), null);
  assert.equal(vkEmbedUrl('https://vk.com/durov'), null);
});
