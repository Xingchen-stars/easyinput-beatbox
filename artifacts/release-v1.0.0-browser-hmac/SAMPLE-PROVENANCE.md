# TR-707 drum samples

Source: [`fluid-music/open-drums`](https://github.com/fluid-music/open-drums/tree/main/tr-707/TR707WAV)

The source pack's `README.txt` states:

> All samples are public domain, so use em however ya want.

Files used:

- `BassDrum1.wav` → `kick.wav`
- `Snare1.wav` → `snare.wav`
- `HhC.wav` → `hihat_closed.wav`
- `HhO.wav` → `hihat_open.wav`
- `HandClap.wav` → `clap.wav`
- `RimShot.wav` → `rim.wav`

Firmware copies in `firmware/main/audio/samples/` are converted to:

- 32 kHz
- signed 16-bit little-endian PCM
- mono
- raw headerless data
- speaker-oriented EQ (hats attenuated in the mixer; kick boosted around 80–160 Hz)
- kick additionally layered with a short ~98 Hz decaying body for small-speaker thump
- conservative peak limiting

The original WAV files are retained here for provenance and future A/B tuning.
