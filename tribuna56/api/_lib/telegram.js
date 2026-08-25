// Отправка сообщения владельцу в Telegram. Молчаливая деградация:
// без env или при ошибке сети — console.warn, сайт не ломается.
// env: TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID.

export async function sendTelegram(text) {
  const token = process.env.TELEGRAM_BOT_TOKEN;
  const chatId = process.env.TELEGRAM_CHAT_ID;
  if (!token || !chatId) {
    console.warn('[telegram] TELEGRAM_BOT_TOKEN/TELEGRAM_CHAT_ID не заданы — сообщение не отправлено:', String(text).slice(0, 300));
    return false;
  }
  try {
    const r = await fetch(`https://api.telegram.org/bot${token}/sendMessage`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ chat_id: chatId, text: String(text) }),
    });
    if (!r.ok) console.warn('[telegram] ответил', r.status);
    return r.ok;
  } catch (e) {
    console.warn('[telegram] отправка упала:', e && e.message);
    return false;
  }
}
