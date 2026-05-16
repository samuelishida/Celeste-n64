# Audio

Milestone 0 does not need audio.

First pass audio targets:

- jump
- dash
- collect
- one music loop

## Downloaded Assets

### Sound Effects
- `sfx/jump_09.mp3` — 8-bit Jump Sound (CC0)
  - Source: https://opengameart.org/content/8-bit-jump-1

- `sfx/coin.mp3` — 8-bit Coin Sound (CC0)
  - Source: https://opengameart.org/content/10-8bit-coin-sounds

- `sfx/platformer_sfx.ogg` — 8-bit Platformer SFX Pack (CC0)
  - Source: https://opengameart.org/content/8-bit-platformer-sfx

### Music
- `music/grasslands.mp3` — Grasslands Theme (CC-BY 3.0)
  - Source: https://opengameart.org/content/platformer-game-music-pack
  - Loopable background music

## Conversion Pipeline

### MP3/OGG → WAV → N64 wav64

1. Convert to WAV (if not already):
   ```sh
   ffmpeg -i input.mp3 output.wav
   ```

2. Convert to N64 format:
   ```sh
   # For SFX (fast, low CPU, ~4:1 compression):
   audioconv64 --wav-compress 1 sound.wav
   
   # For music (high compression, ~15:1, higher CPU):
   audioconv64 --wav-compress 3 --wav-resample 22050 music.wav
   ```

3. Load at runtime:
   ```c
   wav64_load("rom://audio/jump.wav64");
   ```

## N64 Audio Considerations

- **SFX:** Use VADPCM (compression level 1) for instant playback
- **Music:** Use Opus (compression level 3) for long tracks
  - ~3-5 minutes of mono music per MB of ROM
  - Mono Opus: ~18-20% CPU, Stereo: ~35% CPU
- **Sample rate:** Lower = smaller file + lower CPU usage
  - 22050 Hz is a good balance for music
  - 44100 Hz for high-quality SFX
- **Channels:** Mono is strongly preferred on N64

