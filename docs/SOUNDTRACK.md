# ToyCars soundtrack

The soundtrack is a recorded racing-game score by MintoDog, released under CC0 on
OpenGameArt (see [the credits](../assets/licenses/MUSIC.txt)). The 20 published
Ogg Vorbis files (225–305 kb/s, 44.1 kHz stereo, 42 MB) ship unmodified in
`assets/music`. The vendored public-domain decoder
[stb_vorbis](../third_party/stb_vorbis/stb_vorbis.c) v1.22 decodes them in
process, so SDL3 stays the only runtime library. The game does not stream or
download music.

## Music

| Song | Final-lap version | Character | Tracks |
| --- | --- | --- | --- |
| Racing Game Menu (165 BPM) | — | Bright synth menu loop | Menu, garage, help, settings, records |
| Blossom Mountain [Remake] (140 BPM) | Climax, 155 BPM | Bright, harp and tambourine | Harvest Hills, Maple Woods, Sakura Hills |
| Joyful Jungle (140 BPM) | Climax, 155 BPM | Joyful, outdoor | Harvest Hills, Jungle Ruins, Highland Loch |
| Sky Blue Street (165 BPM) | Climax, 180 BPM | Happy, open sky | Sunshine Coast, Sakura Hills, Highland Loch |
| Sunny City (120 BPM) | Climax, 135 BPM | Saxophone and piano, summery | Sunshine Coast, Canyon Dash, Harbor Lights |
| Fever Stadium (165 BPM) | Climax, 180 BPM | Eurobeat | Alpine Rush, Canyon Dash, Jungle Ruins, Volcano Peak |
| Cool Highway [Remake] (140 BPM) | Climax, 155 BPM | Techno | Alpine Rush, Maple Woods, Harbor Lights, Volcano Peak |

The collection has six course songs for ten tracks, so each track plays its own
pair of them (`TRACK_SONGS` in `src/music.c`). Neighbouring tracks never share a
pair; the two fastest songs belong to the night finals of the Rookie and Master
cups, Alpine Rush and Volcano Peak.

Each climax version is the same song at a higher tempo, with added handclaps.
There are four jingles: Final Lap, and Race Finish Best, Good and Bad. Each Race
Finish jingle leads into its own loop.

## Following the race

- **Menu screens** play the menu loop.
- **Countdown:** the music fades out over 0.6 s and the start beeps play alone.
- **Race start:** the song starts at GO, from its first downbeat, with a 5 ms
  attack. Each new race in an environment alternates between its two songs.
  Restarting from pause keeps the current song.
- **Final lap:** the song fades over 0.25 s and the Final Lap jingle plays. The
  climax version then starts from its first sample as the jingle ends.
- **Pause:** the music keeps playing at 45% volume, through two one-pole low-pass
  stages near 700 Hz. Resuming continues the song where it was.
- **Finish:** a finish jingle and its loop play, chosen by the result:
  - Best: first place, or a championship medal.
  - Good: second to fourth place.
  - Bad: fifth or lower, or a DNF.

  The result is fixed while rivals finish. It only upgrades to Best when a
  championship medal is settled. The recorded crowd cheer (or, for a medal, the
  roaring crowd with air horns) plays with the jingle, and on its own with music
  off.
- **Back to the menu:** the current music crossfades into the menu loop (1.2 s
  out, 0.8 s in).
- **Music switch:** turning music off fades it to silence. Turning it back on
  resumes the same song without reloading it. With music off, a hand bell
  announces the final lap instead of the jingle.
- **Backgrounding the app** suspends the audio stream and playback.

## Playback

- **Loudness:** `src/music.c` lists each file's integrated loudness (ITU-R
  BS.1770-4). Playback gain brings every cue to `MUSIC_LOUDNESS` (−26 LUFS). The
  masters are between −10.5 and −13 LUFS. The player's engine plays near
  −26 LUFS at full throttle and a whole race near −21 LUFS, with the effects
  about 4 dB above the music. Change `MUSIC_LOUDNESS`, or `MIX_TRIM` in
  `src/audio.c` for all effects, to rebalance the mix (see
  [Sound effects](SOUND.md)).
- **Decks:** up to three decks cover a crossfade plus a jingle. When a cue starts,
  its whole file is read into memory and decoded progressively. A deck releases
  its file and decoder once it has faded out. At most about 11 MB is held, during
  a crossfade.
- **Loops:** loops wrap sample-accurately. At each loop seam or jingle-to-loop
  handover, playback continues from the last emitted sample and settles onto the
  new material with a 2 ms decay. Blossom Mountain's recording ends 0.22 above its
  first sample; the played step at its seam is 0.
- **Sample rate:** the output stream runs at 44.1 kHz, the files' native rate.
  SDL performs the only conversion, to the device rate. The sound effects use the
  same rate.
- **Missing files:** a missing or invalid file logs `Music unavailable` and that
  cue stays silent.

### Replacing a track

1. Add a 44.1 kHz stereo Ogg Vorbis file under `assets/music`.
2. Change its entry in the cue table in `src/music.c`: file, title and loudness.
3. Run `ToyCarsMusicTests`. It measures every file and fails if the table differs
   from the measurement by more than 0.2 LU. The printed value is the one to use.

## Tests and previews

`tests/music.c` covers:

- **Every file:** each one decodes to its declared length as finite, unclipped
  44.1 kHz stereo. Songs are full loops and jingles are short. Measured loudness
  matches the table.
- **Race flow:** it plays through menu, countdown, GO, pause and resume, final
  lap, the jingle-to-climax handover, finish results and upgrades, alternating
  songs, environment changes, the crossfade to the menu, and mute.
- **Clicks:** it checks the GO attack and Blossom Mountain's loop seam. It also
  checks that output does not depend on the audio block size, that the mix leaves
  headroom, and that missing files are silent.

`./build/ToyCarsMusicTests --render DIRECTORY` additionally writes a 64-second
WAV walkthrough for each environment: menu, countdown, race, pause, final lap,
results and menu. It needs no game window or audio device. Automated checks
cannot judge musical taste; listen on headphones, on speakers and in game,
especially on phones.

## Selection (21 October 2026)

MintoDog's "For Racing Game" collection was chosen for these reasons:

- It is one coherent racing soundtrack, with menu music, course songs with
  matching final-lap versions, and finish jingles with result loops.
- It is CC0.
- It is published as high-bitrate Ogg Vorbis.
- The measured loops are seamless apart from the Blossom Mountain step handled
  above.

Alternatives and exclusions:

- **Neon Sign Circuit** from the same collection is excluded because it contains
  impacts from a Ghosthack sample kit, which has its own terms.
- **elevchyt's PS1-era Jungle/Drum & Bass pack** (CC BY 4.0) was considered as
  a darker alternative. Its download goes through a pay-what-you-want flow.
