from pathlib import Path
import math
import wave
import struct

SAMPLE_RATE = 48000
FREQ = 1000
SECONDS = 2
AMP = 12000

out = Path(__file__).resolve().parent / "audio_test_tone_1k.wav"
frames = []
for n in range(SAMPLE_RATE * SECONDS):
    sample = int(AMP * math.sin(2.0 * math.pi * FREQ * n / SAMPLE_RATE))
    frames.append(struct.pack('<hh', sample, sample))
with wave.open(str(out), 'wb') as wf:
    wf.setnchannels(2)
    wf.setsampwidth(2)
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(b''.join(frames))
print(out)
