// Валидация заявки — одна и та же на клиенте (подсветка полей)
// и на сервере (api/request.js). Чистые функции без DOM.

import { SERVICE_IDS } from './data.js';

// payload → {ok, errors: {поле: текст}, services, matchId, customMatch}
// services нормализованы (уникальные, только известные), matchId — число или null.
export function validateRequest(p) {
  const body = p && typeof p === 'object' ? p : {};
  const errors = {};

  const name = String(body.name || '').trim();
  if (name.length < 2) errors.name = 'Укажите имя';

  const digits = String(body.phone || '').replace(/\D/g, '');
  if (digits.length < 10) errors.phone = 'Укажите телефон полностью';

  if (body.consent !== true) errors.consent = 'Нужно согласие на обработку данных';

  const services = [...new Set(Array.isArray(body.services) ? body.services : [])]
    .filter((id) => SERVICE_IDS.includes(id));
  if (services.length === 0) errors.services = 'Выберите хотя бы одну услугу';

  const matchId = Number(body.match_id);
  const hasMatch = Number.isInteger(matchId) && matchId > 0;

  const custom = body.custom_match;
  const hasCustom = Boolean(
    custom && typeof custom === 'object' &&
    String(custom.teams || '').trim().length >= 3 &&
    String(custom.date_text || '').trim().length >= 3,
  );

  if (!hasMatch && !hasCustom) {
    errors.match = 'Выберите матч из каталога или опишите свой';
  }

  return {
    ok: Object.keys(errors).length === 0,
    errors,
    services,
    matchId: hasMatch ? matchId : null,
    // приоритет у матча из каталога: кастом сохраняем, только если каталожного нет
    customMatch: !hasMatch && hasCustom ? {
      sport: String(custom.sport || '').slice(0, 60),
      teams: String(custom.teams || '').trim().slice(0, 200),
      venue: String(custom.venue || '').trim().slice(0, 200),
      date_text: String(custom.date_text || '').trim().slice(0, 120),
    } : null,
    name,
    phone: String(body.phone || '').trim().slice(0, 30),
  };
}
