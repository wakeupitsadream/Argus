import test from 'node:test';
import assert from 'node:assert/strict';
import { ambientSrc } from '../assets/ambient.js';
import { withAutoplayMuted, cacheBucket } from '../assets/format.js';

const VK = (n) => `https://vk.com/video-241086908_${n}`;

test('withAutoplayMuted: автозапуск без звука + js_api; мусор → null', () => {
  assert.equal(
    withAutoplayMuted('https://vk.com/video_ext.php?oid=-1&id=2&hd=2'),
    'https://vk.com/video_ext.php?oid=-1&id=2&hd=2&autoplay=1&mute=1&js_api=1',
  );
  assert.equal(withAutoplayMuted(null), null);
  assert.equal(withAutoplayMuted(''), null);
});

test('cacheBucket: меняется раз в окно, внутри окна стабилен', () => {
  const t = 59_620_588 * 30_000; // начало 30-секундного окна
  assert.equal(cacheBucket(30, t), 59_620_588);
  assert.equal(cacheBucket(30, t + 29_999), 59_620_588);
  assert.equal(cacheBucket(30, t + 30_000), 59_620_589);
  assert.equal(typeof cacheBucket(), 'number');
});

test('ambientSrc: muted-автозапуск + loop + низкое качество; не-VK → null', () => {
  const src = ambientSrc(VK(100));
  assert.ok(src.startsWith('https://vk.com/video_ext.php?oid=-241086908&id=100'));
  for (const p of ['autoplay=1', 'mute=1', 'js_api=1', 'loop=1', 'hd=1']) {
    assert.ok(src.includes(p), `нет параметра ${p}`);
  }
  assert.ok(!src.includes('hd=2'), 'фону хватает hd=1');
  assert.equal(ambientSrc('https://youtube.com/watch?v=x'), null);
  assert.equal(ambientSrc(null), null);
});
