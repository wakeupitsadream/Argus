import test from 'node:test';
import assert from 'node:assert/strict';
import { teamHash, teamInitials, teamBadge, teamBadgePair, clubKey, clubLogo } from '../assets/badges.js';

test('teamHash детерминирован и не зависит от регистра/ё', () => {
  assert.equal(teamHash('Сармат-2012'), teamHash('сармат-2012'));
  assert.equal(teamHash('Орлёнок'), teamHash('орленок'));
  assert.notEqual(teamHash('Сармат'), teamHash('Металлург'));
});

test('teamInitials: одно и два слова, цифры и кавычки отбрасываются', () => {
  assert.equal(teamInitials('Сармат-2012'), 'С');
  assert.equal(teamInitials('Белые Тигры'), 'БТ');
  assert.equal(teamInitials('«Сармат» 2013'), 'С');
  assert.equal(teamInitials('2012'), '?');
});

test('clubKey: нормализация и срез годового суффикса', () => {
  assert.equal(clubKey('Юниор-2012'), 'юниор');
  assert.equal(clubKey('«Юниор» 2015 г.р.'), 'юниор');
  assert.equal(clubKey('АкБарс-Динамо'), 'акбарс динамо');
  assert.equal(clubKey('ХК Медведь'), 'хк медведь');
});

test('clubLogo: реальные логотипы находятся, чужие клубы — нет', () => {
  assert.equal(clubLogo('Юниор-2012'), '/assets/img/clubs/junior.png');
  assert.equal(clubLogo('Нефтехимик'), '/assets/img/clubs/neftekhimik.png');
  assert.equal(clubLogo('АкБарс'), '/assets/img/clubs/akbars.png');
  assert.equal(clubLogo('Пестрецы'), '/assets/img/clubs/pestretsy.png');
  assert.equal(clubLogo('Медведь'), '/assets/img/clubs/medved.png');
  assert.equal(clubLogo('Сарматы'), '/assets/img/clubs/sarmaty.png');
  assert.equal(clubLogo('Рубин'), '/assets/img/clubs/rubin.png');
  assert.equal(clubLogo('Союз'), '/assets/img/clubs/soyuz.png');
  assert.equal(clubLogo('ЦСК ВВС'), '/assets/img/clubs/csk-vvs.png');
  assert.equal(clubLogo('СШОР №1-ЦСК ВВС'), '/assets/img/clubs/csk-vvs.png');
  // АкБарс-Динамо использует лого АкБарса (решение владельца)
  assert.equal(clubLogo('АкБарс-Динамо'), '/assets/img/clubs/akbars.png');
  // клуб без логотипа не должен получить чужой
  assert.equal(clubLogo('Металлург'), null);
});

test('teamBadge: клуб с логотипом → <img>, без — детерминированный SVG-щит', () => {
  assert.match(teamBadge('Юниор-2015'), /^<img class="club-badge club-badge--img" src="\/assets\/img\/clubs\/junior\.png"/);
  assert.match(teamBadge('АкБарс-Динамо'), /^<img class="club-badge club-badge--img" src="\/assets\/img\/clubs\/akbars\.png"/);
  const a = teamBadge('Белые Тигры');
  assert.equal(a, teamBadge('Белые Тигры'));
  assert.match(a, /^<svg class="club-badge"/);
  assert.match(a, />БТ<\/text>/);
});

test('teamBadgePair содержит две эмблемы (лого и щит смешиваются)', () => {
  const pair = teamBadgePair('Юниор', 'Металлург');
  assert.match(pair, /badge-pair/);
  assert.match(pair, /<img[^>]+junior\.png/);
  assert.match(pair, /<svg class="club-badge"/);
});
