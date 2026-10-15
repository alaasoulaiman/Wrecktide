"""Generate the original pistol cues as 44.1 kHz mono PCM WAVs, using only Python's stdlib.

Run with an optional output directory. Import just sfx_pistol_shot and sfx_pistol_reload
with import_sfx.py and WRECKTIDE_SFX_SOURCE pointing to that directory.
"""
import math
from pathlib import Path
import random
import struct
import sys
import wave

PISTOL_RATE = 44100


def pistol_burst(seconds, decay, seed, lowpass, frequency):
    rng = random.Random(seed)
    result = []
    filtered = 0.0
    phase = 0.0
    for index in range(round(seconds * PISTOL_RATE)):
        t = index / PISTOL_RATE
        filtered += lowpass * (rng.uniform(-1.0, 1.0) - filtered)
        phase += math.tau * frequency * math.exp(-t * 5.0) / PISTOL_RATE
        envelope = min(1.0, t / 0.001) * math.exp(-t / decay)
        result.append((filtered + 0.35 * math.sin(phase)) * envelope)
    return result


def write_pistol_wave(path, samples):
    peak = max(max(abs(value) for value in samples), 0.0001)
    frames = b"".join(struct.pack("<h", round(value / peak * 0.8 * 32767)) for value in samples)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(PISTOL_RATE)
        output.writeframes(frames)
    print(f"{path} samples={len(samples)} seconds={len(samples) / PISTOL_RATE:.3f}")


def main(directory):
    directory.mkdir(parents=True, exist_ok=True)
    shot = pistol_burst(0.32, 0.055, 35, 0.82, 125)
    click = pistol_burst(0.06, 0.009, 36, 0.9, 1500)
    latch = pistol_burst(0.07, 0.014, 37, 0.65, 850)
    offset = round(0.065 * PISTOL_RATE)
    reload_cue = click + [0.0] * (offset + len(latch) - len(click))
    for index, value in enumerate(latch):
        reload_cue[offset + index] += value * 0.7
    write_pistol_wave(directory / "sfx_pistol_shot.wav", shot)
    write_pistol_wave(directory / "sfx_pistol_reload.wav", reload_cue)


if __name__ == "__main__":
    main(Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "Content/Audio/SFX")
