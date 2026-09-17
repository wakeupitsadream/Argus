// Подстраховка автозапуска без звука для iframe-плеера VK.
// mute=1 в URL плеером поддерживается, но не задокументирован, поэтому
// дублируем через официальный postMessage-API (src обязан содержать
// js_api=1 — см. withAutoplayMuted): рукопожатие {method:'init'}, в ответ
// плеер шлет событие inited — тогда командуем mute и play. Любой сбой
// безвреден: остается родная кнопка Play плеера VK.
// Протокол: dev.vk.com/ru/widgets/video (vk.com/js/api/videoplayer.js).

// Возвращает функцию отписки — вызвать, если iframe заменяется другим.
export function nudgeVkMutedPlay(iframe) {
  if (!iframe) return () => {};
  // origin берем из src: готовый embed-код может быть и с vkvideo.ru
  let origin;
  try { origin = new URL(iframe.src).origin; } catch { return () => {}; }
  const send = (msg) => {
    try { iframe.contentWindow && iframe.contentWindow.postMessage(msg, origin); } catch { /* плеер еще не готов */ }
  };
  const onMsg = (e) => {
    if (e.origin !== origin || !iframe.contentWindow || e.source !== iframe.contentWindow) return;
    if (e.data && e.data.event === 'inited') {
      send({ method: 'mute' });
      send({ method: 'play' });
    }
  };
  window.addEventListener('message', onMsg);
  iframe.addEventListener('load', () => send({ method: 'init' }));
  return () => window.removeEventListener('message', onMsg);
}
