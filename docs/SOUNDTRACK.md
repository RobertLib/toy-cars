# ToyCars soundtrack

The soundtrack is an original, offline C11 score, synthesized at 48 kHz. It uses
no recordings, samples, services or additional runtime dependencies. The musical
research informed the arrangement system; melodies are original to this project.

## Compositions

| Setting | Composition | Tempo | Character |
| --- | --- | --- | --- |
| Menu, garage, help, settings | Pocket Paddock | 96 BPM | Relaxed C-major electric keys, lightly swung pocket groove |
| Harvest Hills | Orchard Run | 116 BPM | D-major plucked melody, syncopated bass and warm chords |
| Harvest Hills | Golden Switchback | 122 BPM | A-minor keys, broken beat and a contrasting rising answer |
| Sunshine Coast | Saltwater Skylines | 112 BPM | C-major extended chords, offbeat keys and a laid-back swing |
| Sunshine Coast | Tidal Circuit | 124 BPM | F-Dorian plucks, straight dance pulse and spacious answers |
| Alpine Rush | Aurora Apex | 128 BPM | B-minor soft synth lead and a driving four-beat kick |
| Alpine Rush | Glacier Afterglow | 118 BPM | D-Dorian electric keys and a restrained broken beat |

Each score has two eight-bar melodic phrases and two harmonic progressions inside
a 64-bar form (about two to two-and-a-half minutes). Four-bar sections move through
an opening, theme, answer, lift, development, breakdown and return. Melodies rest
during the opening and breakdowns, leave gaps between calls and answers, and change
register or omit their last answer on alternate cycles. Seventh/ninth chord
voicings, bass approaches, offbeat comping and occasional end-of-phrase fills add
motion without continuously filling every subdivision.

The palette combines FM-style electric keys, harmonic plucks, softly detuned pads,
rounded bass, a soft lead, pitched kick/toms, filtered snare and short hats.
Attack/release envelopes avoid hard note edges. Stereo placement and a dark
dotted-eighth echo provide space; bass and kick stay centered. Oscillators use an
interpolated sine table and note frequencies are cached, keeping per-sample work
bounded. The engine preallocates 64 voices and a stereo delay; playback allocates
no memory and never uses the simulation's random-number generator.

## Following the race

- Entering a race selects a composition for that environment. Starting again
  from the menu or results alternates the two scores. A long race moves to the
  companion score after 64 bars; restarting from pause preserves the current score.
- Speed, a rival within nine progress units and the last lap increase a smoothed
  intensity value. This brings in extra percussion and, in the lift sections,
  answering plucks. Tempo and pitch stay stable while the car accelerates/brakes.
- Countdown reduces the lead; pause lowers the mix and percussion. Resume keeps
  the ongoing score. Results resolve into sparse tonic/dominant harmony without
  drums at the next bar. Menu navigation shares the paddock piece.
- Track changes fade through a bar boundary. Very late requests wait another bar
  so a live chord is not cut off. The incoming piece fades in from its opening.
- Sound events briefly lower music by 32%. The independent music switch fades
  both direct sound and echoes, preserving the musical clock while muted.
- Application backgrounding still suspends the audio stream and its musical clock.

`tests/music.c` renders complete forms without an audio device, checks finite
samples, headroom, adjacent-sample jumps, stereo output, playlist behavior,
late transitions, deterministic rendering, adaptive layers and mute. Its optional
`--render DIRECTORY` argument writes seven 45-second WAV previews. Automated
signal checks cannot judge musical taste; headphone, speaker and in-game listening
remain useful when tuning the mix, especially on physical mobile devices.

## Research applied (16 September 2026)

Winifred Phillips describes rearranging musical sections and adding/removing
simultaneous instrument layers, including the risk of cutting a theme midway
through a transition. Here that informs complete phrase development, a musically
useful backing when the lead rests, and changes aligned to bars. See her
[Game Composers and the Importance of Themes: Interactivity in Game Music](https://www.gamedeveloper.com/audio/game-composers-and-the-importance-of-themes-interactivity-in-game-music-pt-5-).

Audiokinetic's [Creating interactive music](https://www.audiokinetic.com/en/public-library/2024.1.8_8893/?id=creating_interactive_music&source=Help)
organizes interactive music around segments, tracks and playback behavior. This
project implements a small score sequencer with those broad principles in C;
it does not embed Wwise.
