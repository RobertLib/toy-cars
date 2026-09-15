# Research and design direction

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
- **Ownership:** everything needed to race is on the device. Every track is
  available immediately; no accounts, ads, purchases or network services.
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
