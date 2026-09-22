#!/usr/bin/env python3
"""soundgen.py — эмбиент-петли регионов: assets/audio/ambient.toml → <каталог>/amb_<имя>.pcm.

Использование: python3 tools/soundgen.py <каталог_вывода> [--config FILE] [--wav] [--quiet]

Формат вывода: сырой PCM, моно, 16 бит со знаком, little-endian, 22 050 Гц, без заголовка
(число сэмплов = размер файла / 2). Микшер core/audio.h играет петлю на 44 100 Гц,
продвигая дробную позицию с шагом 0,5, поэтому файл именно 22 050 Гц.

Синтез. Сэмплированного материала нет: розовый и коричневый шум через фильтр,
узкополосные резонансы, тоны с гармониками, медленные огибающие и редкие события
(капли, шорохи). Все параметры — в TOML, ГПСЧ с явным зерном, вывод побайтово
воспроизводим.

Зацикливание без щелчка. Каждый слой укладывается в петлю целым числом периодов:
  * шум и резонансы строятся обратным БПФ длины N из спектра, у которого занулена
    постоянная составляющая. Такой сигнал периодичен с периодом ровно N сэмплов;
  * тоны, тремоло и огибающие порывов используют частоты, подогнанные под кратные
    1/длительность (snap_hz);
  * события вписываются по кругу (индексы по модулю N), их огибающие приходят в ноль.
Сумма и произведение периодических с периодом N сигналов тоже периодичны, поэтому
последний сэмпл переходит в первый как два соседних сэмпла внутри петли —
кроссфейд последних 0,3 с не нужен, стык и так гладкий. Это проверяется для каждой
петли: скачок |x[0] − x[N−1]| сравнивается с обычным шагом соседних сэмплов
(99,9-й процентиль). Превышение — ошибка с ненулевым кодом возврата.

Зависимости: numpy и stdlib (tomllib, wave).
"""
import argparse
import sys
import tomllib
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CONFIG = ROOT / "assets" / "audio" / "ambient.toml"

INT16_MAX = 32767
PEAK_LIMIT = 0.80        # пик петли не выше 0,8 от максимума (запас для микшера и нот)
LOOP_MIN_S = 4.0
LOOP_MAX_S = 8.0
SEAM_RATIO_MAX = 4.0     # скачок на стыке — не более 4 обычных шагов…
SEAM_ABS_MIN = 32        # …но мелкие скачки (< 32 LSB) считаем гладкими в любом случае


def plural(n, one, few, many):
    """Русское склонение числительного: 1 петля, 2 петли, 5 петель."""
    n10, n100 = n % 10, n % 100
    if n10 == 1 and n100 != 11:
        return one
    if 2 <= n10 <= 4 and not 12 <= n100 <= 14:
        return few
    return many


def die(msg):
    print(f"soundgen: ошибка: {msg}", file=sys.stderr)
    raise SystemExit(1)


# ── мелкие утилиты ───────────────────────────────────────────────────────────

def rng_for(seed, index):
    """ГПСЧ слоя: зерно региона плюс номер слоя. Явное зерно = воспроизводимый вывод."""
    return np.random.default_rng(np.random.SeedSequence([int(seed), int(index)]))


def norm_rms(x):
    """Приводит к единичному среднеквадратичному уровню: gain слоёв сравним между собой."""
    r = float(np.sqrt(np.mean(x * x)))
    return x / r if r > 1e-12 else x


def norm_peak(x):
    p = float(np.max(np.abs(x))) if x.size else 0.0
    return x / p if p > 1e-12 else x


def snap_hz(hz, seconds):
    """Ближайшая частота, укладывающаяся в петлю целым числом периодов (минимум один)."""
    return max(1, int(round(float(hz) * seconds))) / seconds


def _band_shape(f, low, high, order=2):
    """Амплитудная АЧХ полосы: плавные скаты Баттерворта, постоянная составляющая убрана."""
    fz = np.maximum(f, 1e-6)
    w = np.ones_like(fz)
    if low and low > 0.0:
        w = w / np.sqrt(1.0 + (low / fz) ** (2 * order))
    if high and high > 0.0:
        w = w / np.sqrt(1.0 + (fz / high) ** (2 * order))
    w[f <= 0.0] = 0.0
    return w


def _tail_taper(m, frac=0.15):
    """Спад хвоста события в точный ноль: событие не обрывается на полуслове."""
    k = max(1, int(m * frac))
    w = np.ones(m)
    w[m - k:] = 0.5 * (1.0 + np.cos(np.linspace(0.0, np.pi, k)))
    return w


def _place(out, w, pos):
    """Вписывает событие в петлю по кругу: индексы берутся по модулю длины."""
    idx = (pos + np.arange(w.size)) % out.size
    np.add.at(out, idx, w)


# ── циклические примитивы (периодичны с периодом n по построению) ────────────

def circ_noise(n, rate, rng, tilt=1.0, low=0.0, high=0.0):
    """Окрашенный шум: белый шум → БПФ → наклон 1/f^tilt и полоса → обратный БПФ.
    tilt = 1 даёт розовый шум, tilt = 2 — коричневый (наклон задан по мощности)."""
    f = np.fft.rfftfreq(n, 1.0 / rate)
    shape = _band_shape(f, low, high) * np.maximum(f, 1e-6) ** (-0.5 * float(tilt))
    shape[f <= 0.0] = 0.0
    return norm_rms(np.fft.irfft(np.fft.rfft(rng.standard_normal(n)) * shape, n))


def circ_resonance(n, rate, rng, freq, q):
    """Узкополосный резонанс: шум через АЧХ резонатора второго порядка.
    Чем выше добротность q, тем ближе результат к «звону» на частоте freq."""
    f = np.fft.rfftfreq(n, 1.0 / rate)
    bw = max(freq / max(float(q), 0.5), rate / n)   # полоса не уже одного бина спектра
    d = (freq * freq - f * f) / (freq * bw)
    shape = 1.0 / np.sqrt(1.0 + d * d)
    shape[f <= 0.0] = 0.0
    return norm_rms(np.fft.irfft(np.fft.rfft(rng.standard_normal(n)) * shape, n))


def circ_lfo(n, rate, seconds, rng, hz):
    """Медленная огибающая: сумма трёх синусов около hz, каждый — целое число
    периодов в петле. Пик равен 1."""
    t = np.arange(n) / rate
    out = np.zeros(n)
    for ratio, weight in ((0.63, 0.7), (1.0, 1.0), (1.71, 0.5)):
        f = snap_hz(hz * ratio, seconds)
        out += weight * np.sin(2.0 * np.pi * f * t + rng.uniform(0.0, 2.0 * np.pi))
    return norm_peak(out)


def slow_env(n, rate, seconds, rng, hz, depth):
    """Огибающая 1 ± depth: порывы ветра, уход резонанса, тремоло. Всегда положительна."""
    hz, depth = float(hz or 0.0), float(depth or 0.0)
    if hz <= 0.0 or depth <= 0.0:
        return np.ones(n)
    return 1.0 + min(depth, 0.95) * circ_lfo(n, rate, seconds, rng, hz)


# ── слои ─────────────────────────────────────────────────────────────────────

def layer_noise(n, rate, seconds, rng, p):
    x = circ_noise(n, rate, rng, p.get("tilt", 1.0), p.get("low", 0.0), p.get("high", 0.0))
    x = x * slow_env(n, rate, seconds, rng, p.get("gust_hz"), p.get("gust_depth"))
    return norm_rms(x)


def layer_band(n, rate, seconds, rng, p):
    if "freq" not in p:
        die("слой band без freq")
    x = circ_resonance(n, rate, rng, float(p["freq"]), p.get("q", 20.0))
    x = x * slow_env(n, rate, seconds, rng, p.get("drift_hz"), p.get("drift_depth"))
    return norm_rms(x)


def layer_tone(n, rate, seconds, rng, p):
    if "freq" not in p:
        die("слой tone без freq")
    base = float(p["freq"])
    detune = float(p.get("detune", 0.0))
    t = np.arange(n) / rate
    x = np.zeros(n)
    for k, amp in enumerate(p.get("harmonics", [1.0]), start=1):
        amp = float(amp)
        if amp == 0.0:
            continue
        # Второй расстроенный голос даёт медленные биения; обе частоты подогнаны
        # под целое число периодов, поэтому биение тоже укладывается в петлю.
        for shift in (0.0,) if detune <= 0.0 else (0.0, detune * k):
            f = snap_hz(base * k + shift, seconds)
            if f * 2.0 >= rate:      # выше Найквиста не синтезируем
                continue
            x += amp * np.sin(2.0 * np.pi * f * t + rng.uniform(0.0, 2.0 * np.pi))
    x = x * slow_env(n, rate, seconds, rng, p.get("trem_hz"), p.get("trem_depth"))
    return norm_rms(x)


def layer_drops(n, rate, seconds, rng, p):
    """Капли и щелчки: затухающие синусоиды с лёгким уходом частоты."""
    count = int(p.get("count", 4))
    out = np.zeros(n)
    if count <= 0:
        return out
    decay = max(float(p.get("decay", 0.2)), 1e-3)
    attack = max(float(p.get("attack", 0.002)), 1e-4)
    glide = float(p.get("glide", 0.0))
    f_lo, f_hi = float(p.get("freq_lo", 800.0)), float(p.get("freq_hi", 1600.0))
    m = max(int(min(decay * 7.0 + attack, seconds * 0.9) * rate), 8)
    tt = np.arange(m) / rate
    dur = m / rate
    taper = _tail_taper(m)
    for i in range(count):
        f0 = rng.uniform(f_lo, f_hi)
        phase = 2.0 * np.pi * (f0 * tt + 0.5 * glide * f0 * tt * tt / dur)
        env = (1.0 - np.exp(-tt / attack)) * np.exp(-tt / decay) * taper
        w = np.sin(phase) * env * rng.uniform(0.6, 1.0)
        _place(out, w, int(round((i + rng.uniform(-0.35, 0.35)) * n / count)) % n)
    return norm_peak(out)


def layer_rustle(n, rate, seconds, rng, p):
    """Шорохи: короткие полосовые всплески шума под окном Ханна (ноль на краях)."""
    count = int(p.get("count", 3))
    out = np.zeros(n)
    if count <= 0:
        return out
    dur = float(p.get("dur", 0.3))
    low, high = float(p.get("low", 800.0)), float(p.get("high", 5000.0))
    for i in range(count):
        m = max(int(min(max(dur * rng.uniform(0.7, 1.3), 0.02), seconds * 0.5) * rate), 16)
        f = np.fft.rfftfreq(m, 1.0 / rate)
        spec = np.fft.rfft(rng.standard_normal(m)) * _band_shape(f, low, high)
        w = norm_peak(np.fft.irfft(spec, m) * np.hanning(m)) * rng.uniform(0.5, 1.0)
        _place(out, w, int(round((i + rng.uniform(-0.35, 0.35)) * n / count)) % n)
    return norm_peak(out)


LAYERS = {
    "noise": layer_noise,
    "band": layer_band,
    "tone": layer_tone,
    "drops": layer_drops,
    "rustle": layer_rustle,
}


# ── сборка петли ─────────────────────────────────────────────────────────────

def render(name, cfg, defaults):
    rate = int(cfg.get("rate", defaults.get("rate", 22050)))
    seconds = float(cfg.get("seconds", defaults.get("seconds", 6.0)))
    peak = float(cfg.get("peak", defaults.get("peak", 0.6)))
    if "seed" not in cfg:
        die(f"{name}: нет зерна ГПСЧ (seed) — вывод должен быть воспроизводим")
    if not LOOP_MIN_S - 1e-9 <= seconds <= LOOP_MAX_S + 1e-9:
        die(f"{name}: длительность {seconds} с вне диапазона {LOOP_MIN_S}–{LOOP_MAX_S} с")
    if not 0.0 < peak <= PEAK_LIMIT:
        die(f"{name}: peak {peak} вне диапазона (0, {PEAK_LIMIT}]")
    layers = cfg.get("layer", [])
    if not layers:
        die(f"{name}: нет ни одного слоя ([[{name}.layer]])")

    n = int(round(seconds * rate))
    seconds = n / rate          # точная длительность: от неё считается «целое число периодов»
    mix = np.zeros(n)
    for i, p in enumerate(layers):
        fn = LAYERS.get(p.get("kind"))
        if fn is None:
            die(f"{name}, слой {i}: неизвестный вид '{p.get('kind')}' "
                f"(есть: {', '.join(sorted(LAYERS))})")
        mix += float(p.get("gain", 1.0)) * fn(n, rate, seconds, rng_for(cfg["seed"], i + 1), p)
    mix -= float(np.mean(mix))          # постоянная составляющая только съедает запас
    mix = norm_peak(mix) * peak
    return mix, rate, seconds, n


def to_int16(x):
    y = np.round(x * INT16_MAX)
    np.clip(y, -INT16_MAX, INT16_MAX, out=y)
    return y.astype("<i2")


def seam_stats(pcm):
    """Стык петли: скачок между последним и первым сэмплом против обычного шага."""
    a = pcm.astype(np.int64)
    join = int(abs(a[0] - a[-1]))
    steps = np.abs(np.diff(a))
    typical = float(np.percentile(steps, 99.9)) if steps.size else 0.0
    return join, typical


def write_wav(path, pcm, rate):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def main():
    ap = argparse.ArgumentParser(description="генератор эмбиент-петель ARGUS")
    ap.add_argument("out_dir", type=Path, help="каталог вывода (обычно build/assets)")
    ap.add_argument("--config", type=Path, default=DEFAULT_CONFIG, help="описание петель (TOML)")
    ap.add_argument("--wav", action="store_true", help="дополнительно писать .wav для прослушивания")
    ap.add_argument("--quiet", action="store_true", help="только итоговая строка")
    args = ap.parse_args()

    if not args.config.exists():
        die(f"нет файла описания {args.config}")
    data = tomllib.loads(args.config.read_text(encoding="utf-8"))
    defaults = data.pop("defaults", {})
    if not data:
        die(f"{args.config}: не описано ни одного региона")
    args.out_dir.mkdir(parents=True, exist_ok=True)

    if not args.quiet:
        print(f"{'файл':<17}{'длит.':>8}{'сэмплов':>9}{'байт':>9}{'пик':>17}{'RMS':>7}"
              f"   стык: скачок/шаг         слои")
    total = 0
    bad = 0
    for name, cfg in data.items():
        x, rate, seconds, n = render(name, cfg, defaults)
        pcm = to_int16(x)
        path = args.out_dir / f"amb_{name}.pcm"
        path.write_bytes(pcm.tobytes())
        if args.wav:
            write_wav(path.with_suffix(".wav"), pcm, rate)
        total += pcm.nbytes

        peak_i = int(np.max(np.abs(pcm)))
        peak_f = peak_i / INT16_MAX
        rms_f = float(np.sqrt(np.mean((pcm.astype(np.float64) / INT16_MAX) ** 2)))
        join, typical = seam_stats(pcm)
        ratio = join / typical if typical > 0.0 else 0.0
        smooth = join <= SEAM_ABS_MIN or ratio <= SEAM_RATIO_MAX
        if peak_i > int(PEAK_LIMIT * INT16_MAX):
            print(f"soundgen: {name}: пик {peak_f:.3f} выше предела {PEAK_LIMIT}", file=sys.stderr)
            bad += 1
        if not smooth:
            print(f"soundgen: {name}: щелчок на стыке — скачок {join} LSB "
                  f"при обычном шаге {typical:.0f}", file=sys.stderr)
            bad += 1
        if not args.quiet:
            db = 20.0 * np.log10(peak_f) if peak_f > 0 else -99.0
            print(f"{path.name:<17}{seconds:>6.2f} с{n:>9}{pcm.nbytes:>9}"
                  f"{peak_f:>10.3f} ({db:+.1f} дБ){rms_f:>7.3f}"
                  f"{join:>9}/{typical:<6.0f} = {ratio:4.2f}× {'гладко' if smooth else 'ЩЕЛЧОК'}"
                  f"{len(cfg.get('layer', [])):>4}")
    print(f"эмбиент: {len(data)} {plural(len(data), 'петля', 'петли', 'петель')}, "
          f"{total} байт ({total / 1024.0:.0f} КиБ) в {args.out_dir}"
          + ("" if not bad else f"; ПРОВЕРКИ ПРОВАЛЕНЫ: {bad}"))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
