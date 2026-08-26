import test from 'node:test';
import assert from 'node:assert/strict';
import { teamHash, teamInitials, teamBadge, teamBadgePair } from '../assets/badges.js';

test('teamHash детерминирован и не зависит от регистра/ё', () => {
  assert.equal(teamHash('Юниор-2012'), teamHash('юниор-2012'));
  assert.equal(teamHash('Орлёнок'), teamHash('орленок'));
  assert.notEqual(teamHash('Юниор'), teamHash('Сармат'));
});

test('teamInitials: одно и два слова, цифры и кавычки отбрасываются', () => {
  assert.equal(teamInitials('Юниор-2012'), 'Ю');
  assert.equal(teamInitials('Белые Тигры'), 'БТ');
  assert.equal(teamInitials('«Сармат» 2013'), 'С');
  assert.equal(teamInitials('2012'), '?');
});

test('teamBadge: SVG стабилен для одного клуба', () => {
  const a = teamBadge('Юниор-2012');
  assert.equal(a, teamBadge('Юниор-2012'));
  assert.match(a, /^<svg class="club-badge"/);
  assert.match(a, />Ю<\/text>/);
});

test('teamBadgePair содержит две эмблемы', () => {
  const pair = teamBadgePair('Юниор', 'Сармат');
  assert.equal((pair.match(/<svg/g) || []).length, 2);
  assert.match(pair, /badge-pair/);
});
