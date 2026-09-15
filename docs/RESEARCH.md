# Research and design direction

The soundtrack was expanded on 16 September 2026 using interactive-composition
research. See [Soundtrack](SOUNDTRACK.md) for sources, original compositions and
the concrete arrangement and gameplay decisions.

Research date: 15 September 2026. The following are design references, not a
claim that one game is objectively the best in the genre.

## Circuit Superstars — readable, tactile racing

Original Fire describes an elevated racing view, stylized visuals and driving
that combines accessibility with tactile physical handling. Their discussion
of vehicle identity and motorsport inspired the coherent rally car and the
garage's paint selection.

**Applied:** a high, smooth follow camera; visible lateral momentum; readable
apexes; strong vehicle color separation; a small racing vocabulary that can be
learned in one lap. The game uses automatic acceleration by default on mobile,
so the player can focus on steering and braking.

Sources: [Developer Q&A, Square Enix](https://collective.square-enix-games.com/en_GB/news/circuit-superstars-questions),
[Original Fire interview](https://collective.square-enix-games.com/en_GB/news/interview-original-fire-games-circuit-superstars).

## Beach Buggy Racing — recognizable places and mobile readability

Vector Unit's own game catalog presents Beach Buggy Racing as an island racing
adventure with visually varied tracks. Its mobile focus makes it a useful
reference for bold silhouettes and environments that can be read at speed on a
small display.

**Applied:** country, beach and winter have different palettes, buildings,
vegetation, surface grip, circuit shape and vertical profile. Collectibles are
bright green, float above the asphalt and respond with particles, sound and an
explicit fuel message. Two-thumb controls remain outside the forward view.

Source: [Vector Unit game catalog](https://www.vectorunit.com/).

## Micro Machines — the pleasure of small-scale worlds

The official product description emphasizes miniature vehicles and playful
environmental context. The important reference here is the sense that a track
is a little world, with props and scenery that establish scale.

**Applied:** each circuit sits on a sculpted diorama; the garage emphasizes the
die-cast body; cars have exaggerated tires, mirrors, stripes and spoilers.
Barns, silos, beach parasols, palms, chalets, snowmen, crops and fences create a
world outside the road. The menu shows a live rendering of the actual scene.

Sources: [Official game site](https://www.micromachinesgame.com/),
[Official PlayStation product page](https://store.playstation.com/en-us/concept/227398).

## Death Rally — a compact, complete racing loop

Death Rally supplies the historical top-down racing reference from the brief.
ToyCars follows the short journey from choosing a race through a countdown and
wheel-to-wheel competition to a clear result screen. The requested survival
pressure is represented by fuel collection. Weapons and a damage economy are
outside this version's scope.

Source: [Remedy's games catalog](https://www.remedygames.com/games).

## Original direction for ToyCars

- **Tone:** welcoming, energetic, tactile. A little racing club, not a military HUD.
- **Palette:** warm paper, charcoal and tangerine in the interface; moss, sand,
  lagoon and icy blue in the world.
- **Typography:** Barlow for information, Barlow Condensed for titles and race numbers.
- **Session:** two laps, eight drivers, roughly one minute to ninety seconds
  depending on route, difficulty and player pace.
- **Challenge:** learn the line, carry speed over ramps, keep enough fuel to finish.
- **Ownership:** everything needed to race is on the device. Tracks unlock by
  playing Championship; no accounts, ads, purchases or network services.
- **Asset pipeline:** Blender is the authoring source for all modeled 3D content.

## Technical references

The platform implementation was checked against SDL's primary documentation:

- [Creating a GL context](https://wiki.libsdl.org/SDL3/SDL_GL_CreateContext)
- [SDL3 Android guide](https://wiki.libsdl.org/SDL3/README-android)
- [SDL3 iOS guide](https://wiki.libsdl.org/SDL3/README-ios)
- [Audio device streams](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream)

The renderer uses GL 3.3 on desktop and GLES 3.0 on Android/iOS. Apple's OpenGL
APIs are deprecated; a Metal backend through SDL_GPU is a future renderer
upgrade, not something this version claims to implement.

## Sega Rally and Daytona — progression and quick races

Research date: 17 September 2026. Sega Rally Championship links Desert, Forest
and Mountain stages, carrying position between stages. First place after the
third stage grants the bonus Lakeside stage. On Saturn, reaching Lakeside also
unlocks it for practice/time trials. Original Daytona USA instead offers three
courses (Beginner, Advanced, Expert), with checkpoints extending a race timer;
it is not a persistent track-unlock campaign.

Sources: [Sega Rally gameplay](https://en.wikipedia.org/wiki/Sega_Rally_Championship),
[Saturn unlock conditions](https://gamefaqs.gamespot.com/saturn/573998-sega-rally-championship/cheats),
[Daytona USA gameplay](https://en.wikipedia.org/wiki/Daytona_USA#Gameplay).

**Applied:** ToyCars combines a saved Championship route with an Arcade mode
for individual races on earned tracks. A podium on Harvest Hills unlocks
Sunshine Coast, a podium there unlocks Alpine Rush, and a podium in Alpine Rush
completes Championship. Any difficulty qualifies. This is an adaptation for
ToyCars, not an exact reproduction of either Sega game's rules: positions
reset between stages, progress survives quitting, and fuel remains the race's
survival constraint. No cumulative points or checkpoint countdown was added.

## Trackside television production

The original broadcast props use [GP1's motorsport production gallery](https://gp1.tv/production)
for trackside camera operators, paddock crews and outside-broadcast vehicles, and
[Zest4.tv's OB1](https://www.zest4.tv/ob1) for the compact production truck and
separate production, engineering and sound roles. These are references, not imported assets.
Each circuit has four tripod camera positions plus a small broadcast compound:
a fifth camera operator, reporter with microphone, sound engineer with boom and
mixer bag, vision technician with monitors, TV van, roof dish, radio mast,
flight cases and terrain-following cables. Crew wear media vests, passes and
headsets. Locations are checked against road clearance, water, slopes and existing
scenery; subsequent habitat generation also avoids the new props. The original
low-poly geometry is static, included in the existing scene/shadow batch and
editable in the three Blender track scenes.


### Rally safety netting

[FIA Rally Safety Guidelines 2024, spectator safety](https://www.fia.com/sites/default/files/rally_safety_guidelines_digital_2024_en_09092024as1.pdf)
describe tape and plastic netting as boundaries for spectators and prohibited
areas, with outside bends and jump landing areas treated as dangerous zones.
The game's short orange nets mark selected outer-bend exclusion boundaries,
not vehicle containment barriers. Placement is a gameplay adaptation: four
separate 16 m runs, approximately 3.2 m beyond the road edge, avoiding water
and jumps. The net uses constrained particles and hinged stakes with planted
feet. Damped rotation and limited fabric impulses let stakes fold locally under
a car without being launched across the scene.
