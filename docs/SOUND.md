# ToyCars sound effects

Every sound effect is a recording released under CC0, edited for the game and
shipped as Ogg Vorbis in `assets/sounds` (51 files, 11.6 MB). The same vendored
[stb_vorbis](../third_party/stb_vorbis/stb_vorbis.c) decoder as the soundtrack
reads them, so SDL3 stays the only runtime library. Credits for every source are
in [SOUNDS.txt](../assets/licenses/SOUNDS.txt); the music is described in
[Soundtrack](SOUNDTRACK.md).

## Sources

Most effects come from **Pól's Foundation Sound Effects**, a library recorded for
a rally game: the countdown signal, gear changes, exhaust backfires, a rally car
landing on dirt, its metal and plastic impacts, and rally crowds cheering with
air horns and drums. Sharing one source keeps the impacts, signals and crowds in
one character.

| Sound | Source | Heard |
| --- | --- | --- |
| Engine, four layers | GiocoSound, BMW 120d recorded outside at idle, low, medium and high engine speed | Every car, see below |
| Skid | audible-edge, tires squealing as a car circles the microphone | Sideways slides on the road |
| Gravel, sand, snow | mhad's tire rolling on gravel (sand: softened and slowed); Kenney's snow footsteps scattered into a continuous crunch | Off the road on each track |
| Wading | paulprit, fast wading through a river | Tires in a ford |
| Wind | kyles, smooth rushing air | Speed, and flight off jumps |
| Countdown | Pól, rally countdown signal: three beeps and the start tone | 3, 2, 1, GO |
| Gear change, backfire | Pól | Upshifts; lifting off a revving engine |
| Stall | Pól's backfire twice, then GiocoSound's engine stop | Running out of fuel |
| Landing, car hit, cone | Pól, landing on dirt, metal impact, plastic impact | Landings and tumbles; cars and rails; cones |
| Tire stack | Kenney's heavy thump with a slap from DDT197's rubber tire | Tire barriers |
| Net | Pól's plastic impact, lower and softer, with qubodup's bamboo swish | Safety nets |
| Take-off, recovery | florianreichelt's woosh, forward and reversed | Jumps; recovery |
| Boost | Pól's backfire kicking off the woosh | Boost pads |
| Fuel | IENBA, an item pickup made of chalk, keys and a synth | Fuel cans |
| Splash | qubodup, a big water splash | Driving into a ford |
| Crowd | Pól, cheering and clapping; roaring with air horns for a medal | Finish; championship medal |
| Bell | domrodrig, a hand bell | Final lap with music off |
| Helicopter | unikumpu, a rotor loop | The camera helicopter |
| Ambience | Kinoton's spring countryside, jackmichaelking's waves and gulls, xkeril's Swiss mountain morning, sengjinn's night crickets | Harvest Hills, Sunshine Coast, Alpine Rush; crickets for the daytime landscapes at night |
| Ambience, later tracks | fran_marenco's wind and birds in the Patagonian desert; cribbler's wind and crows; Felix Blume's night frogs and crickets in French Guiana; tomasloos's bird by a bamboo grove over a stream in Japan; Kinoton's harbour with lapping water and creaking boats; Kinoton's skylarks over windy grassland; Felix Blume's Mount Yasur volcano at night | Canyon Dash, Maple Woods, Jungle Ruins, Sakura Hills, Harbor Lights, Highland Loch, Volcano Peak |
| Weather | speakwithanimals' rain, Fission9's gusting polar wind | Rain; wind and snow |
| Menu | Kenney UI Audio, a click and a switch | Every menu change |
| Crossing bell | mpaol2023, an electronic level-crossing bell, twelve strikes 0.413 s apart | Level crossings while their lights flash |
| Steam train | relwin, riding behind steam locomotive 478 up a grade (chuffs and wheels, about four chuffs a second); Bidone's old steam whistle | Harvest Hills train |
| Mountain train | nikitralala, a fast train's bogies clacking over rail joints (one carriage every 1.09 s); euroblues, the typical Swiss locomotive whistle (SBB Re 420) | Alpine Rush train |
| Road train | kyles, a tractor's diesel idling close up; timgormly's toy train whistle, a short toot and a long one | Sunshine Coast road train |

### Selection notes (21 October 2026)

- **Engine:** an engine whose pitch follows its speed needs one engine recorded
  at several steady speeds. GiocoSound's outside set has clean, strongly periodic
  layers at 834, 1494, 2020 and 2550 rpm (firing frequency over two). The rally
  library's own idle, low and medium layers share one engine cycle and differ
  mainly in noise, so they cannot form a speed ladder; its gear, backfire and
  impacts are used instead. The engine character is the main taste judgment
  in this set.
- **Wind for weather:** a cold alpine night recording was rejected because wind
  chimes ring through most of it (measured narrow peaks near 3.5 and 6.1 kHz).
- **Driving on snow and sand** has no usable CC0 recording, so those two
  surfaces are made from footsteps and the gravel roll.
- **Railway (21 October 2026):** two otherwise convincing steam recordings were
  rejected because their descriptions say what they are: one is a textured
  shower floor being scrubbed, the other is AI generated. The steam loop comes
  from a real ride behind a working locomotive, whose tempo stays within about
  3% (0.257 s per chuff) for half a minute. Slower, more distinct chuffs from
  locomotives leaving a station speed up through every usable stretch, so a
  loop would stumble at its seam. The crossing bell was chosen for its steady
  electronic strike (period 0.4131 s, autocorrelation 0.98): the Slovak
  mechanical bell strikes unevenly, the Danish one carries passing traffic, and
  the Belgian one rings continuously at 5.6 strokes a second. No steady CC0
  recording of a passing train includes its motor; the clacking carriages of a
  passing train are the steadiest material.
- Freesound sources are its public high-quality previews (Ogg Vorbis around
  190 kb/s); its original files need an account.
- **Ambience for the later tracks (22 October 2026):** about 70 CC0 candidates
  were compared on spectrogram sheets, narrow-peak scans, loudness over time,
  stereo correlation and loop seams. Each bed is one steady minute of a real
  field recording; the garden mixes two recordings by the same recordist in
  Japan, a bird by a bamboo grove and a stream under a bridge at −18 dB.
  Rejected: recordings with traffic, aircraft, engines, music, voices, hum
  lines (most explicitly autumnal forests and a Yorkshire moor carry an 84–250
  Hz hum), chimes or clanking rigging, clipping, synthesized "desert wind" and
  edited clips with digital silence. Compromises: the desert was recorded in the
  Patagonian steppe and the moor on spring dunes (skylarks, no sheep or
  curlew); no CC0 Japanese bush warbler exists; the harbour may be a daytime
  recording; the volcano is the quietest stretch between eruptions of Mount
  Yasur and still swells by up to 13 dB in a slow rumble. The countryside,
  desert, forest, garden and moor beds give way to night crickets after dark;
  the jungle, harbour and volcano beds are night recordings.

## Engine

Each car runs the same model in `src/audio.c`:

- **Gearbox:** four gears reach 6800 rpm at 12.5, 20.5, 28.5 and 38 m/s. The
  engine shifts up at 6500 rpm and down below 3100 rpm under throttle (2300 rpm
  coasting). A shift closes the throttle for 0.13 s while the engine speed drops
  to the next gear; the player hears a gearbox clunk.
- **Engine speed** follows the tires in gear. A slipping clutch at launch,
  wheelspin in slides and on loose ground raise it; in the air, on the grid and
  while tumbling the engine revs freely with the throttle. The player can rev on
  the grid unless automatic acceleration is on; rivals blip their throttles.
- **Rivals** share the model; each rival's engine is detuned by up to ±3.6% so
  the field does not sound like one car.
- **Layers:** engine speed maps to playback pitch with the exponent 0.7, so idle
  to redline spans the four recordings without sounding sped up (the top of the
  high layer plays at 1.3 times its speed). The two layers around the current
  speed crossfade with equal power.
- **Load:** on the throttle the engine is louder and fully open; off it the
  sound turns darker through a low-pass filter. Lifting off above 4300 rpm pops
  the exhaust one to three times, and some upshifts pop once.
- **Fuel:** an empty tank plays the stall; the engine winds down and falls silent.

## Direction

`audio_direct` turns each frame's game state into sound:

- **Race only:** engines, tires, ambience, weather and the helicopter play in
  the countdown, the race and (at 70% level) the results. Pause, the menus and
  the sound switch fade them out over about 0.1 s; one-shots finish naturally.
- **Player's car** is heard centered: engine, skid, surface, wading and wind.
- **Rivals and impacts** are placed from the camera: full level within 6 m
  (impacts 6–12 m), falling with distance and silent beyond 75 m, panned by
  direction, darker behind the camera and far away, and pitched by the Doppler
  shift of their closing speed (camera cuts are not counted as motion). Rival
  loops are released when inaudible, so distant cars cost nothing.
- **Cues:** the simulation queues sound events (`game_sound`) with a car, a
  strength in m/s and a position: countdown, GO, fuel, final lap, take-off,
  landing (from 2.2 m/s) and tumbling hits, out of fuel, finish, medal, boost, recovery, and hits
  between cars, against rails, cones, tire stacks and nets. Impact level follows
  the impact speed. Each car may sound one hit per 0.12 s; each kind of prop
  one per 0.07 s. A net only rustles on a fresh push.
- **Railway:** the crossing bell rings at the crossing while its signals flash.
  The train is heard from its vehicle nearest the camera, with the Doppler shift
  of its speed, at a third of the level and darker than 900 Hz while that vehicle
  is still in a tunnel. The steam loop plays at 1.15 times its speed and the
  mountain train's clatter at 0.8; the road train's diesel idles at the stops
  and rises to 1.35 times its pitch when driving. A whistle sounds once per
  crossing, 5.5 s before the train arrives, and carries 220 m.
- **Menus** click for every change of screen, track, mode, car color, time of
  day, setting, difficulty, initials or records page. Watching the game state
  instead of the buttons covers mouse, touch, keyboard and gamepad alike.

## Mix

Every sound is normalized by its measured loudness (the table in
`src/sfx.c`); the director then sets design levels, offset together by
`MIX_TRIM` (−2 dB):

| Element | Design level |
| --- | --- |
| Player's engine | −30 LUFS base, up to about −26 LUFS measured at full throttle and high revs |
| Rival engine at 6 m | −35 LUFS base |
| Skid, surface, wading | −31 LUFS (rivals −36 to −38) |
| Wind at top speed | −38 LUFS |
| Ambience; rain; gusts | −40; −36; −37 LUFS |
| Helicopter at 25 m | −38 LUFS |
| Countdown, GO, fuel | −26, −25, −26 LUFS |
| Impacts | −40 to −28 LUFS by impact speed |
| Crowd | −33 LUFS at the finish, −29 for a medal |
| Crossing bell at 10 m | −36 LUFS |
| Train at 16 m | −30 LUFS steam, −32 mountain railway, −33 road train (about −35 idling) |
| Whistles at 22 m | −31 LUFS steam, −35 mountain railway, −33 road train |
| Menu clicks | −30 and −28 LUFS |

Measured from the sound test's race previews, a race plays near −21 LUFS and the
menu music alone at −26.4 LUFS. A peak limiter (instant attack, 150 ms
release, ceiling 0.89) replaces the former hard clip at ±0.8; in the test races
it never engages and peaks stay at 0.54–0.66.

## Playback

`src/sfx.c` is a small mixer, independent of the game like `src/music.c`:

- Short sounds and loops are decoded to 16-bit samples at start-up (123 s,
  10.8 MB). The ambience and weather beds (10.1 MB of files) stream from memory
  through their own decoders only while they play.
- 48 voices. A held sound (`sfx_hold`) follows new gain, pitch, pan and filter
  targets over each rendered block and frees its voice once faded out; a full
  voice budget replaces the quietest one-shot. Loops of the same sound start at
  different points so rivals never phase.
- Pitch uses four-point Hermite interpolation, panning is equal-power, and a
  one-pole low-pass per voice handles distance and engine load. A voice cut
  short hands its last sample to a decaying tail, so it cannot click.
- Everything runs at 44.1 kHz, the files' rate, in the existing 50 ms audio
  queue. A missing or invalid file logs `Sound unavailable` and stays silent.

## Rebuilding and replacing sounds

`tools/build_sounds.py` downloads the sources (cached in `build/sound-sources`),
applies the edits listed beside each sound, encodes with oggenc and writes each
result's measured loudness into the table in `src/sfx.c`:

```sh
python3 tools/build_sounds.py            # every sound
python3 tools/build_sounds.py skid snow  # selected sounds
```

It needs numpy, scipy, soundfile and oggenc (vorbis-tools); the game needs none
of them. Loops are folded over an equal-power crossfade; sources that are
already seamless (the engine layers, the helicopter) are resampled and filtered
as one period of a repeating signal so their seams survive. Rhythmic loops (the
crossing bell, the trains, the road train's engine) span a whole number of
strikes, chuffs or carriages, and their end moves to where the tail best
matches the head (in waveform, or for noisy rhythms in envelope), so the beat
runs on through the seam; loops cut from a passing or climbing train first
have their steady rise or fall in level removed. Downloads wait three seconds
apart, as Freesound refuses faster requests. To replace a sound,
change its entry in `SOUNDS` and update its credit in `SOUNDS.txt`.

## Tests and previews

`ToyCarsSoundTests` (part of `ctest`) checks:

- **Every file:** 44.1 kHz, stereo beds and mono or stereo effects, sensible
  length, headroom, loudness within 0.2 LU of the table, and loop seams no
  larger than the sound's own 99.9th-percentile sample step.
- **Mixer:** click-free start and release of held loops, voice release,
  crossfading a held handle to a new sound, the voice budget, and silent
  missing files.
- **Races:** a full race on each track, directed and mixed at 60 frames per
  second from a chase camera: the gearbox reaches top gear, engine speed stays
  in range, every cue is consumed, menus and pause are silent, resuming brings
  the race back, the mix is audible and the limiter is rarely needed.

`--render DIRECTORY` also writes a 47-second WAV per track (menu, countdown,
race, pause, resume) with music and effects. Automated checks cannot judge
taste: listen on headphones, on speakers and in game, especially on phones.
