"""Verify the optional half-speed chirp fixture after REAPER project reopening."""
import math
import sys
import wave

with wave.open(sys.argv[1], "rb") as wav:
    assert (wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getnframes()) == (48000, 2, 3, 192000)
    raw = wav.readframes(wav.getnframes())
left = [int.from_bytes(raw[i:i+3], "little", signed=True) / 8388608 for i in range(0, len(raw), 6)]
right = [int.from_bytes(raw[i+3:i+6], "little", signed=True) / 8388608 for i in range(0, len(raw), 6)]
assert left == right
assert .001 < max(map(abs, left)) < 1

def projection(start, end, onset, root, slope, pitch):
    begin, finish = round(start * 48000), round(end * 48000)
    real = imag = 0
    for i in range(begin, finish):
        t = i / 48000 - onset
        # Source speed .5 changes the frequency slope, independently of note pitch.
        phase = 2 * math.pi * pitch * (root * t + .25 * slope * t * t)
        real += left[i] * math.cos(phase)
        imag += left[i] * math.sin(phase)
    return 2 * math.hypot(real, imag) / (finish - begin)

for start, end, onset, pitch in [(.5, 1, .1, 1), (2.5, 3, 2, 2)]:
    for root, slope in [(613.7, 200), (947.3, 100)]:
        value = projection(start, end, onset, root, slope, pitch)
        print(f"{start}-{end}s: {root} Hz chirp, pitch ratio {pitch}, amplitude {value:.9f}")
        assert value > .009, "Missing evolving partial, wrong speed, or incorrect MIDI pitch"
assert projection(.5, 1, .1, 613.7, 200, .5) < .001
print("PASS: reopened REAPER trajectory state, independent half speed, A4/A5 transposition, stereo")
