"""Rebuild assets/sounds from their recorded sources.

    python3 tools/build_sounds.py [--cache DIR] [NAME ...]

Needs numpy, scipy, soundfile and oggenc (vorbis-tools); the game needs none
of them. Freesound sources are its public high-quality Ogg previews, Kenney
sources are the pack archives. Downloads are cached (default build/sound-sources).
Every edit is listed in SOUNDS below; the script prints each result's loudness
for the table in src/sfx.c. Credits are in assets/licenses/SOUNDS.txt.
"""
import argparse
import io
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile

import numpy as np
import soundfile as sf
from scipy import signal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RATE = 44100
UA = {"User-Agent": "ToyCars sound build"}
KENNEY = {
    "interface": "https://kenney.nl/media/pages/assets/interface-sounds/fa43c1dd4d-1677589452/kenney_interface-sounds.zip",
    "impact": "https://kenney.nl/media/pages/assets/impact-sounds/87b4ddecda-1677589768/kenney_impact-sounds.zip",
    "ui": "https://kenney.nl/media/pages/assets/ui-audio/490d233f68-1677590494/kenney_ui-audio.zip",
}


def fetch(url, cache):
    path = os.path.join(cache, re.sub(r"[^A-Za-z0-9._-]+", "_", url.split("//", 1)[1])[-120:])
    if not os.path.exists(path):
        # Freesound answers HTTP 429 to more than about one request in 2.5 s.
        time.sleep(max(0, fetch.last + 3 - time.time()))
        fetch.last = time.time()
        req = urllib.request.Request(url, headers=UA)
        data = urllib.request.urlopen(req, timeout=120).read()
        with open(path + ".part", "wb") as f:
            f.write(data)
        os.replace(path + ".part", path)
    return path


fetch.last = 0


def freesound(preview, cache, circular):
    """A Freesound HQ preview: preview is the path after /previews/."""
    return read(fetch("https://cdn.freesound.org/previews/" + preview, cache), circular)


def kenney(pack, name, cache):
    with zipfile.ZipFile(fetch(KENNEY[pack], cache)) as z:
        member = next(m for m in z.namelist() if m.endswith("/" + name))
        return read(io.BytesIO(z.read(member)), False)


def read(path, circular):
    """Reads at 44.1 kHz. A circular source is already a seamless loop: it is
    resampled as one period of a repeating signal so its seam survives."""
    x, rate = sf.read(path, always_2d=True, dtype="float64")
    if rate == RATE:
        return x
    g = np.gcd(rate, RATE)
    if not circular:
        return signal.resample_poly(x, RATE // g, rate // g, axis=0)
    y = signal.resample_poly(np.concatenate([x, x, x]), RATE // g, rate // g, axis=0)
    n = int(round(len(x) * RATE / rate))
    return y[n:2 * n]


# Edits; each takes and returns float64 frames x channels at 44.1 kHz.
def mono(x):
    return x.mean(axis=1, keepdims=True)


def cut(x, start, end=None):
    return x[int(start * RATE):None if end is None else int(end * RATE)]


def highpass(x, hz, order=2):
    return signal.sosfiltfilt(signal.butter(order, hz, "highpass", fs=RATE, output="sos"), x, axis=0)


def lowpass(x, hz, order=2):
    return signal.sosfiltfilt(signal.butter(order, hz, "lowpass", fs=RATE, output="sos"), x, axis=0)


def pitch(x, ratio):
    """Plays the sound ratio times faster (higher and shorter)."""
    n = int(round(len(x) / ratio))
    return signal.resample(x, n, axis=0) if n > 0 else x[:0]


def fade(x, fade_in=.003, fade_out=.02):
    x = x.copy()
    a, b = max(1, int(fade_in * RATE)), max(1, int(fade_out * RATE))
    x[:a] *= np.sin(np.linspace(0, np.pi / 2, a))[:, None] ** 2
    x[-b:] *= np.cos(np.linspace(0, np.pi / 2, b))[:, None] ** 2
    return x


def loop(x, crossfade=.25):
    """Folds the tail over the head with an equal-power crossfade, so the
    result repeats seamlessly; the length shrinks by the crossfade."""
    n = int(crossfade * RATE)
    body, tail = x[:-n].copy(), x[-n:]
    t = np.linspace(0, np.pi / 2, n)[:, None]
    body[:n] = body[:n] * np.sin(t) + tail * np.cos(t)
    return body


def seamless(x, length, crossfade=.25, search=.05, envelope=False):
    """Loops about length seconds from the start of x. The end moves by up to
    search seconds to where the tail best matches the head, so the crossfade
    joins like material in phase (or, with envelope, in rhythm) instead of
    smearing two strikes or chuffs together."""
    n, target, reach = int(crossfade * RATE), int(length * RATE), int(search * RATE)
    m = x.mean(axis=1)
    if envelope:
        m = lowpass(np.abs(highpass(m, 300)), 40)
    head = m[:n] - m[:n].mean()
    region = m[target - reach:target + reach + n]
    score = signal.correlate(region, head, mode="valid")
    energy = np.sqrt(np.convolve(region ** 2, np.ones(n), mode="valid") -
                     np.convolve(region, np.ones(n), mode="valid") ** 2 / n)
    end = target - reach + int(np.argmax(score / np.maximum(energy, 1e-12)))
    return loop(x[:end + n], crossfade)


def flatten(x, window=.5):
    """Removes a steady rise or fall in level, so a loop cut from a passing or
    climbing source does not swell at its seam."""
    n = int(window * RATE)
    m = x.mean(axis=1)
    blocks = len(m) // n
    level = 10 * np.log10(np.mean(m[:blocks * n].reshape(blocks, n) ** 2, axis=1) + 1e-20)
    slope, _ = np.polyfit((np.arange(blocks) + .5) * n, level, 1)
    return x * 10 ** (-slope * (np.arange(len(m)) - len(m) / 2) / 20)[:, None]


def gate_tail(x, db=-60):
    """Drops trailing near-silence from a one-shot."""
    level = np.abs(x).max(axis=1)
    keep = np.where(level > 10 ** (db / 20) * level.max())[0]
    return x[:keep[-1] + int(.01 * RATE)] if len(keep) else x


def reverse(x):
    return x[::-1].copy()


def scatter(grains, seconds, rate, seed):
    """A continuous texture of randomly timed, leveled and pitched grains,
    wrapped around so the result loops seamlessly."""
    rng = np.random.default_rng(seed)
    n = int(seconds * RATE)
    out = np.zeros((n, 1))
    for _ in range(int(seconds * rate)):
        g = pitch(grains[rng.integers(len(grains))], rng.uniform(.85, 1.15)) * rng.uniform(.35, 1)
        idx = (rng.integers(n) + np.arange(len(g))) % n
        np.add.at(out, idx, g)
    return out


def normalize(x, peak=-1.0):
    return x * (10 ** (peak / 20) / max(np.abs(x).max(), 1e-9))


def mix(*parts):
    n = max(len(p) for p, _ in parts)
    out = np.zeros((n, max(p.shape[1] for p, _ in parts)))
    for p, offset in parts:
        start = int(offset * RATE)
        end = min(n, start + len(p))
        out[start:end] += p[:end - start] if p.shape[1] == out.shape[1] else p[:end - start].mean(1, keepdims=True)
    return out


# Loudness, ITU-R BS.1770-4 with 400 ms blocks (one block for shorter sounds).
def kweight(x):
    f0, g, q = 1681.974450955533, 3.999843853973347, .7071752369554196
    k = np.tan(np.pi * f0 / RATE)
    vh = 10 ** (g / 20)
    vb = vh ** .4996667741545416
    a0 = 1 + k / q + k * k
    b = [(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0]
    a = [1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
    y = signal.lfilter(b, a, x, axis=0)
    f0, q = 38.13547087602444, .5003270373238773
    k = np.tan(np.pi * f0 / RATE)
    a0 = 1 + k / q + k * k
    return signal.lfilter([1, -2, 1], [1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0], y, axis=0)


def loudness(x):
    y = kweight(x)
    block, hop = int(RATE * .4), int(RATE * .1)
    if len(y) < block:
        return -.691 + 10 * np.log10(np.mean(y ** 2, axis=0).sum() + 1e-20)
    e = np.array([np.mean(y[i:i + block] ** 2, axis=0).sum() for i in range(0, len(y) - block + 1, hop)])
    e = e[-.691 + 10 * np.log10(e + 1e-20) > -70]
    if not len(e):
        return -np.inf
    e = e[-.691 + 10 * np.log10(e) > -.691 + 10 * np.log10(e.mean()) - 10]
    return -.691 + 10 * np.log10(e.mean())


def write(name, x, quality, cache):
    path = os.path.join(ROOT, "assets", "sounds", name + ".ogg")
    with tempfile.TemporaryDirectory(dir=cache) as tmp:
        wav = os.path.join(tmp, name + ".wav")
        sf.write(wav, np.clip(x, -1, 1), RATE, subtype="PCM_24")
        subprocess.run(["oggenc", "-Q", "-q", str(quality), "-o", path, wav], check=True)
    decoded, _ = sf.read(path, always_2d=True)
    return decoded


def table(name, measured):
    """Writes the measured loudness into the sound's entry in src/sfx.c."""
    path = os.path.join(ROOT, "src", "sfx.c")
    with open(path) as f:
        text = f.read()
    entry = re.compile(r'("sounds/%s\.ogg", )-?[0-9.]+f' % re.escape(name))
    if not entry.search(text):
        print(f"  {name} is not in src/sfx.c")
        return
    with open(path, "w") as f:
        f.write(entry.sub(lambda m: f"{m.group(1)}{measured:.2f}f", text))


def S(preview, circular=False):
    return lambda cache: freesound(preview, cache, circular)


def K(pack, name):
    return lambda cache: kenney(pack, name, cache)


def chain(*edits):
    def run(x):
        for edit in edits:
            x = edit(x)
        return x
    return run


def at(edit, *args, **kwargs):
    return lambda x: edit(x, *args, **kwargs)


def around(edit):
    """Applies an edit to a seamless loop as one period of a repeating signal."""
    def run(x):
        y = edit(np.concatenate([x, x, x]))
        return y[len(x):2 * len(x)]
    return run


# Freesound previews (path after /previews/) by author.
GIOCO = "401/{}_5820033-hq.ogg"   # GiocoSound: BMW 120d, Earthworks QTC30 + Rode NT4
POL = "385/{}_7097737-hq.ogg"     # Pól: Foundation Sound Effects, a rally car library
ONE_SHOT = (fade, normalize)


def engine(sound_id):
    return (S(GIOCO.format(sound_id), circular=True), chain(mono, around(at(highpass, 28)), normalize), 6)


# name: (source, edit, Vorbis quality). Effects are mono; ambience beds stereo.
SOUNDS = {
    # Engine layers recorded at 834, 1494, 2020 and 2550 rpm (firing frequency / 2).
    "engine-idle": engine(401552),
    "engine-low": engine(401556),
    "engine-mid": engine(401555),
    "engine-high": engine(401551),
    # audible-edge: tires squealing round the microphone; the steady middle.
    "skid": (S("71/71739_995351-hq.ogg"), chain(at(cut, 5.5, 10.6), at(highpass, 160), loop, normalize), 5),
    # mhad: a tire rolling on gravel (Foundation Sound Effects).
    "dirt": (S("390/390798_7336549-hq.ogg"), chain(mono, at(cut, .3, 5.0), at(highpass, 50), at(loop, .3), normalize), 5),
    # paulprit: fast wading through a river.
    "wade": (S("563/563555_8682843-hq.ogg"), chain(mono, at(cut, 25, 37), at(highpass, 90), at(loop, 1), normalize), 4),
    # unikumpu: helicopter rotor loop.
    "helicopter": (S("609/609460_11932590-hq.ogg", circular=True), chain(mono, around(at(highpass, 40)), normalize), 5),
    # Kinoton: rural spring afternoon, ORTF stereo; birds and insects.
    "ambience-country": (S("514/514550_2247456-hq.ogg"), chain(at(cut, 20, 82), at(highpass, 60), at(loop, 3), normalize), 3),
    # jackmichaelking: calm waves on a beach, a little birdsong and distant gulls.
    "ambience-beach": (S("518/518637_6089496-hq.ogg"), chain(at(cut, 10, 72), at(highpass, 40), at(loop, 3), normalize), 3),
    # xkeril: early morning in the Swiss mountains, wind and a distant river.
    "ambience-winter": (S("757/757868_13504080-hq.ogg"), chain(at(cut, 40, 102), at(highpass, 40), at(loop, 3), normalize), 3),
    # fran_marenco: soft wind in the Patagonian desert, a few birds and insects (Canyon).
    "ambience-desert": (S("796/796193_3557494-hq.ogg"), chain(at(cut, 300, 362), at(highpass, 60), at(loop, 3), normalize), 3),
    # cribbler: wind in the trees on a small wooded mountain, crows calling now and then (Autumn).
    "ambience-forest": (S("734/734711_3728489-hq.ogg"), chain(at(cut, 1074, 1136), at(highpass, 50), at(loop, 3), normalize), 3),
    # felix.blume: night in French Guiana's rainforest, frogs, toads and crickets (Jungle).
    "ambience-jungle": (S("868/868734_1661766-hq.ogg"), chain(at(cut, 218, 280), at(highpass, 60), at(loop, 3), normalize), 3),
    # tomasloos, Japan: a bird calling by a bamboo grove over a small stream (Sakura); the
    # grove's own trickle is faint, so his steady stream under a bridge runs beneath it.
    "ambience-garden": (lambda cache: (freesound("843/843520_1680683-hq.ogg", cache, False), freesound("843/843511_1680683-hq.ogg", cache, False)),
                        lambda parts: normalize(loop(mix((highpass(cut(parts[0], 0, 62), 120), 0),
                                                         (highpass(cut(parts[1], 71, 133), 60) * .12, 0)), 3)), 3),
    # Kinoton: a harbour, water splashing at the pier, boats creaking, a distant horn (Harbor);
    # the steep high-pass removes a faint 108 Hz hum.
    "ambience-harbor": (S("561/561815_2247456-hq.ogg"), chain(at(cut, 90, 152), at(highpass, 150, 4), at(loop, 3), normalize), 3),
    # Kinoton: skylarks singing over windy grassland in spring (Highland).
    "ambience-moor": (S("635/635912_2247456-hq.ogg"), chain(at(cut, 108, 170), at(highpass, 50), at(loop, 3), normalize), 3),
    # felix.blume: Mount Yasur at night from the crater rim, rumbling, puffing and spattering
    # between eruptions (Volcano).
    "ambience-volcano": (S("661/661147_1661766-hq.ogg"), chain(at(cut, 1067, 1127), at(highpass, 35), at(loop, 3), normalize), 3),
    # Pól: the rally countdown, three beeps and the start tone.
    "beep": (S(POL.format(385928)), chain(mono, at(cut, 0, .62), *ONE_SHOT), 5),
    "go": (S(POL.format(385928)), chain(mono, at(cut, 2.99, 4.0), at(fade, .002, .3), normalize), 5),
    # IENBA: an item pickup made of chalk, keys and a synth (first take).
    "fuel": (S("698/698768_9616576-hq.ogg"), chain(mono, at(cut, 0, 2.2), gate_tail, *ONE_SHOT), 5),
    "landing": (S(POL.format(385933)), chain(at(cut, .06, .75), *ONE_SHOT), 5),
    "car-hit": (S(POL.format(385937)), chain(at(cut, .12, .9), at(fade, .002, .25), normalize), 5),
    "cone": (S(POL.format(385940)), chain(at(cut, .1, .62), at(fade, .002, .12), normalize), 5),
    "gear": (S(POL.format(385936)), chain(at(cut, .14, .5), *ONE_SHOT), 5),
    "backfire": (S(POL.format(385935)), chain(at(cut, .18, .5), *ONE_SHOT), 5),
    # The rally backfire coughing twice before the BMW engine shuts off.
    "sputter": (lambda cache: (freesound(POL.format(385935), cache, False), freesound(GIOCO.format(401557), cache, False)),
                lambda parts: normalize(fade(mix((cut(parts[0], .18, .5), 0), (cut(parts[0], .18, .5) * .7, .32),
                                                 (mono(cut(parts[1], .55, 2.0)), .5)))), 5),
    # qubodup: a big water splash.
    "splash": (S("442/442773_71257-hq.ogg"), chain(mono, at(fade, .002, .3), normalize), 5),
    # domrodrig: a hand bell rung once.
    "bell": (S("116/116779_1550701-hq.ogg"), chain(mono, at(cut, .28, 2.87), at(fade, .002, .5), normalize), 5),
    # Pól: rally crowds, cheering and clapping, and roaring with air horns.
    "cheer": (S(POL.format(385919)), chain(at(cut, 1.0, 8.0), at(fade, .25, 2.5), normalize), 4),
    "roar": (S(POL.format(385920)), chain(at(cut, 1.0, 10.0), at(fade, .25, 3), normalize), 4),
    # sengjinn: crickets chirping around midnight.
    "ambience-night": (S("175/175020_2979997-hq.ogg"), chain(at(cut, 0, 60), at(highpass, 80), at(loop, 3), normalize), 3),
    # Sand: the gravel roll, softened and slowed.
    "sand": (S("390/390798_7336549-hq.ogg"), chain(mono, at(cut, .3, 5.0), at(lowpass, 1700), at(pitch, .82), at(highpass, 40), at(loop, .3), normalize), 5),
    # Snow: Kenney's snow footsteps scattered into a continuous crunch.
    "snow": (lambda cache: [mono(kenney("impact", f"footstep_snow_00{i}.ogg", cache)) for i in range(5)],
             lambda grains: normalize(around(at(highpass, 70))(scatter(grains, 4, 18, 7))), 5),
    # florianreichelt: a broad woosh, forward for take-offs and reversed into a recovery.
    "takeoff": (S("683/683096_6253486-hq.ogg"), chain(mono, at(fade, .01, .3), normalize), 5),
    "recover": (S("683/683096_6253486-hq.ogg"), chain(mono, at(cut, .25, 1.45), reverse, at(fade, .2, .04), normalize), 5),
    # The rally backfire kicking off a woosh.
    "boost": (lambda cache: (freesound(POL.format(385935), cache, False), freesound("683/683096_6253486-hq.ogg", cache, False)),
              lambda parts: normalize(fade(mix((cut(parts[0], .18, .5), 0), (mono(cut(parts[1], .3, 1.7)) * .8, .02)), .002, .3)), 5),
    # Kenney's heavy punch thump with one slap from DDT197's rubber tire.
    "tire": (lambda cache: (kenney("impact", "impactPunch_heavy_000.ogg", cache), freesound("445/445781_8292592-hq.ogg", cache, False)),
             lambda parts: normalize(fade(mix((mono(parts[0]), 0), (mono(cut(parts[1], 1.55, 1.85)) * .45, .004)), .002, .1)), 5),
    # The rally plastic impact, lower and softer, with qubodup's bamboo swish for the net.
    "fence": (lambda cache: (freesound(POL.format(385940), cache, False), freesound("60/60013_71257-hq.ogg", cache, False)),
              lambda parts: normalize(fade(mix((lowpass(pitch(cut(parts[0], .1, .62), .8), 3000), 0), (mono(parts[1]) * .6, .01)), .002, .12)), 5),
    # kyles: smooth rushing air, for the wind at speed.
    "wind": (S("454/454072_612689-hq.ogg"), chain(mono, at(cut, 60, 73), at(highpass, 60), at(loop, 1.5), normalize), 4),
    # speakwithanimals: steady rain; Fission9: gusting polar wind.
    "rain": (S("525/525046_10637780-hq.ogg"), chain(at(cut, 100, 162), at(highpass, 50), at(loop, 3), normalize), 3),
    "gusts": (S("521/521820_9395330-hq.ogg"), chain(at(cut, 1, 61), at(highpass, 40), at(loop, 3), normalize), 3),
    # Railway. mpaol2023: an electronic level-crossing bell striking every 0.413 s; twelve strikes.
    "crossing-bell": (S("370/370824_6430986-hq.ogg"), chain(mono, at(highpass, 100), at(cut, 9.07, 15.0), at(seamless, 12 * .4131, .06, .03), normalize), 5),
    # Bidone: an old steam locomotive's whistle.
    "steam-whistle": (S("71/71778_706955-hq.ogg"), chain(mono, at(highpass, 120), at(cut, .1, 3.17), at(fade, .1, .3), normalize), 5),
    # euroblues: the typical Swiss locomotive whistle (SBB Re 420), stepping up a little.
    "train-horn": (S("20/20065_97596-hq.ogg"), chain(mono, at(highpass, 150), at(cut, 0, 2.4), at(fade, .004, .5), normalize), 5),
    # relwin: riding behind steam locomotive 478 up a grade; 31 chuffs, about four a second.
    "steam-train": (S("686/686059_1024769-hq.ogg"), chain(mono, at(highpass, 40), at(cut, 2.0, 11.0), flatten, at(seamless, 31 * .2568, .3, .13, True), normalize), 5),
    # nikitralala: a fast train passing; eight carriages of bogies clacking over the joints.
    "train": (S("239/239933_2352202-hq.ogg"), chain(mono, at(highpass, 40), at(cut, 34.0, 44.0), flatten, at(seamless, 8 * 1.091, .3, .1, True), normalize), 5),
    # kyles: a tractor's diesel idling close up, for the seaside road train.
    "land-train": (S("637/637884_612689-hq.ogg"), chain(mono, at(highpass, 40), at(cut, 16.0, 23.0), at(seamless, 21 * .2887, .2, .05), normalize), 5),
    # timgormly: a toy train whistle, a short toot and a long one.
    "land-train-horn": (S("162/162803_2578041-hq.ogg"), chain(mono, at(highpass, 150), at(fade, .003, .15), gate_tail, normalize), 5),
    # Kenney UI Audio: a short click and a switch.
    "ui-move": (K("ui", "click1.ogg"), chain(mono, *ONE_SHOT), 5),
    "ui-select": (K("ui", "switch7.ogg"), chain(mono, *ONE_SHOT), 5),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", default=os.path.join(ROOT, "build", "sound-sources"))
    parser.add_argument("names", nargs="*")
    args = parser.parse_args()
    os.makedirs(args.cache, exist_ok=True)
    os.makedirs(os.path.join(ROOT, "assets", "sounds"), exist_ok=True)
    for name in args.names or SOUNDS:
        source, edit, quality = SOUNDS[name]
        x = edit(source(args.cache))
        if name.startswith("ambience") or name in ("rain", "gusts"):
            assert x.shape[1] == 2, name
        else:
            x = mono(x)
        decoded = write(name, x, quality, args.cache)
        measured = loudness(decoded)
        print(f'{name:18} {len(decoded) / RATE:6.2f} s {decoded.shape[1]} ch '
              f'peak {20 * np.log10(np.abs(decoded).max()):6.1f} dBFS  {measured:6.2f} LUFS')
        table(name, measured)


if __name__ == "__main__":
    sys.exit(main())
