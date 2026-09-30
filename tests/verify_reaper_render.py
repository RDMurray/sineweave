"""Verify the optional REAPER fixture render using only Python's standard library."""
import math
import pathlib
import sys
import wave

path = pathlib.Path(sys.argv[1])
with wave.open(str(path), "rb") as wav:
    assert wav.getframerate() == 48000 and wav.getnchannels() == 2
    assert wav.getsampwidth() == 3 and wav.getnframes() == 192000
    raw = wav.readframes(wav.getnframes())
left, right = [], []
for offset in range(0, len(raw), 6):
    left.append(int.from_bytes(raw[offset:offset + 3], "little", signed=True) / 8388608)
    right.append(int.from_bytes(raw[offset + 3:offset + 6], "little", signed=True) / 8388608)
assert left == right, "Mono synthesis must duplicate to stereo"
peak = max(abs(x) for x in left)
assert 0.001 < peak < 1, "Expected audible, unclipped output"

def amplitude(start, end, frequency):
    start, end = round(start * 48000), round(end * 48000)
    real = sum(left[i] * math.cos(2 * math.pi * frequency * i / 48000) for i in range(start, end))
    imag = sum(left[i] * math.sin(2 * math.pi * frequency * i / 48000) for i in range(start, end))
    return 2 * math.hypot(real, imag) / (end - start)

print(f"48 kHz, stereo, 24-bit, 4 seconds; peak {peak:.9f}")
for start, end, frequencies in [(.5, 1, (613.7, 947.3)), (2.5, 3, (1227.4, 1894.6))]:
    for frequency in frequencies:
        value = amplitude(start, end, frequency)
        assert value > .003, f"Missing expected partial at {frequency} Hz"
        print(f"{start}-{end} s: {frequency} Hz amplitude {value:.9f}")
assert amplitude(.5, 1, 1227.4) < .0001
assert amplitude(2.5, 3, 613.7) < .0001
print("PASS: REAPER MIDI transposition and saved-state playback")
