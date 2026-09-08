# ToyCars

An arcade 3D toy car racer for iOS, inspired by Micro Machines and Death
Rally. Built on RealityKit, fully offline, camera above and behind, the
throttle drives itself and the player handles corners, drifts, fuel and jumps.

```
ToyCars/
  ToyCars/                 app sources (Xcode target)
    Core/                  track data, car catalog, progress saving
    Race/                  simulation, AI, camera, scene, effects, audio
    UI/                    SwiftUI screens and HUD
                           (Theme, Materials, Buttons, Badges, Gauges,
                            Backdrop and Logo are the design system;
                            the rest are the screens)
    Assets3D/              generated models and game data (usdz + json)
    Localizable.xcstrings  string catalog (English source, Czech translation)
  ToyCarsTests/            unit tests (Swift Testing)
  tools/blender/           model and track generators (Python for Blender)
    lib/                   shared libraries (mesh, spline, land, track, props)
  tools/preview/           preview renders (not part of the build)
```

## Language

The app is authored in English. Czech lives only in
`ToyCars/Localizable.xcstrings` and is selected automatically when the
device language is Czech; every other language falls back to English.
Nothing in the code, comments or data files should be in Czech.

Car taglines and track subtitles are stored in English in the generated
manifests and looked up in the string catalog at runtime
(`CarDef.localizedTagline`, `TrackDef.localizedSubtitle`), so a new car or
track only needs its English text plus a catalog entry.

## How the assets are made

Every model and track is **procedurally generated in Blender**. The
repository contains no hand-modelled files - the script is the single source
of truth and the output can be regenerated at any time.

```bash
BL=/Applications/Blender.app/Contents/MacOS/Blender

# 8 cars -> ToyCars/Assets3D/Cars/*.usdz + cars_manifest.json
$BL -b --factory-startup --python tools/blender/gen_cars.py -- [--preview] [--views] [--only ID]

# 3 tracks -> ToyCars/Assets3D/Tracks/*.usdz + *.json + tracks_manifest.json
$BL -b --factory-startup --python tools/blender/gen_tracks.py -- \
    [--top] [--preview] [--parts] [--only ID]

# the fuel canister and other collectibles
$BL -b --factory-startup --python tools/blender/gen_props.py --

# the app icon
$BL -b --factory-startup --python tools/blender/gen_icon.py
```

`tcrender.render` writes PNG in **RGB**, not Blender's RGBA default. The film
is opaque either way, so the alpha channel carried nothing - but App Store
Connect rejects an app icon that *has* one at all, transparent or not, and
this is the function that renders the icon.

The scripts are reproducible; Blender's USD exporter is not. Two runs of the
same unchanged script write byte-identical `.json` and a `.usdz` that differs
throughout - the geometry is the same, the file is not. So a change that only
touches the game data (pickup positions, say) is worth committing as the JSON
alone: a regenerated `.usdz` is ten megabytes of diff saying nothing.

### Why it is done this way

* **One centreline for graphics and physics alike.** `gen_tracks.py` fits a
  closed centripetal Catmull-Rom spline through the control points and
  resamples it into a dense polyline with a constant step (~0.7 m). The road,
  kerbs, terrain and prop placement are all built from it - and **that same**
  polyline is written into the JSON for the game. Physics and mesh can never
  drift apart.
* **Three circuits, three characters.** The one thing a set of tracks must
  not be is the same track three times, so each is built around a different
  idea and the numbers are what carry it:

  | | Sunset | Cove | Frostpeak |
  |---|---|---|---|
  | idea | a classic circuit over rolling hills | a shoreline blast | a mountain pass |
  | lap | 1640 m, 2 laps | 1338 m, 3 laps | 1894 m, 2 laps |
  | footprint | 370 x 425 m, compact | 545 x 225 m, a strip | 375 x 500 m, a valley |
  | corners | a dozen medium ones, snaking | 2 sweepers, 1 hairpin, a 300 m straight | 4 stacked switchbacks at r20 |
  | climb | 68 m a lap, 15 % | 22 m, 7.5 % - flat, it is a beach | 87 m, 19 % |
  | surface | asphalt, a gravel loop | sand, 370 m of boardwalk | snow, ice in the shade |

  Sunset is written as `control` points; the other two as a `route` of
  straights and arcs (`tcspline.route_control`), which says what a circuit
  *is* - "a 220 m sweeper at 70 m radius, then a 160 degree hairpin at 20" -
  instead of coordinates that only a plot can be read back out of. The turns
  have to add up to a full circle and two of the straights are given as
  `None`; those two lengths are solved so the lap closes.
* **The landscape comes first, the road follows it.** Each track spec carries
  a `land` block - a handful of hills, ridges and peaks plus fractal detail
  (`lib/tcland.py`) - and the plan view gives only `x, y, half width,
  surface`. The elevation of every control point is
  *sampled from that landscape*, so a lap is a climb out of a valley and a
  descent back into it rather than a flat ribbon. Two consequences fall out
  for free:

  1. `Centerline.limit_grade` caps the gradient at the spec's `max_grade`
     (7.5 % on the beach, 19 % over the pass) by cutting summits and filling
     dips by equal amounts - the same trade a road builder makes. What it
     takes off is exactly the cutting or embankment the terrain then shows
     beside the road.
  2. Because the terrain blends from road level out to *the same* field, the
     ground rises into a cut on the uphill side and falls away on the
     downhill one by itself. Nothing has to be modelled twice.

  Rules of thumb when authoring a `land` block: a hill the road crosses
  wants `height/radius` under ~0.13 (its steepest slope is `1.54 h/r`), and
  a `peak` should stay clear of the road entirely - mountains are scenery,
  not something to drive over, and the outer skirt of one 270 m away is
  still enough to tilt a starting grid by 12 %. `--preview` renders three views per track,
  including the whole landscape from outside, which is the quickest way to
  see whether it worked.
* **No textures.** Colour comes from assigning materials to individual faces
  by distance from the track, height and noise. The bundle stays small and the
  style consistent.
* **Few objects.** Props are joined into a handful of meshes before export so
  the draw call count stays low on mobile. A track is around 200 thousand
  triangles; `--parts` prints the breakdown per object when that budget needs
  watching.
* **Terrain that reaches the horizon without paying for the middle.**
  `graded_axis` samples the ground uniformly (3.4 m) over the track and then
  in geometrically growing steps outwards, far enough that no mountain is
  sliced off at the edge. A tensor product of two such axes is still a
  conforming quad grid, so there are no cracks and no T-junctions.
* **Mind the orientation on export.** Blender does write `upAxis = "Y"`, but
  it leaves the data Z-up and performs the conversion by **rotating the root
  prim by −90° around X**:

  ```
  def Xform "Root"    float3 xformOp:rotateXYZ = (-90, -0, 0)
      WheelFL  translate = (-0.657, -1.030, 0.400)   ← front wheel, Blender −Y
  ```

  Two things follow from this:
  1. The model faces **+Z** in RealityKit (not −Z as hand-made USDZ files do),
     so the car's rotation is `simd_quatf(angle: heading, axis: [0,1,0])`.
  2. Nested objects (the wheels) keep their local axes **in Blender's frame** -
     the axle is X, the steering axis is Z. The conversion into RealityKit is
     `(x, z, −y)`, which is what the game JSON export uses too.
* **Normals must point outwards - and Blender will not show you.** The Blender
  preview draws a face from both sides, but RealityKit culls back faces. An
  inside-out model therefore looks correct in a render and is see-through in
  the game. Watching the winding by hand did not work out, so the orientation
  is derived from the geometry:

  | where | how |
  |---|---|
  | `tcmesh.loft()` | the shell is temporarily capped, the signed volume is computed and the whole loft is flipped if it comes out negative |
  | `tcmesh.face_up()` | on horizontal strips (road, kerbs, trims) the faces pointing downwards are flipped |
  | `tcmesh.check_normals()` | after assembly it walks the closed parts of the model and reports the inverted ones |

  `check_normals` runs on every generation and prints `normals=OK`.
  If `normals=N BAD` appears, the model **must not** be committed.
* **Nothing may interpenetrate.** Coplanar faces are redrawn alternately by the
  engine and flicker. Accessories are therefore seated against the real
  surface rather than an estimate: the wheels must stand proud of the body by
  at least `WHEEL_GAP` (`auto_track`), the wing stays reach the panel
  (`body_surface_z`), windows and stripes are offset along the normal so the
  whole surface - not just the vertices - lies above the body
  (`press_out_of_body`), and the track narrows in hairpins to stay under the
  corner radius (`clamp_width_to_curvature`) - otherwise the inner road edge
  folds over itself. Beside the track two more distances matter: the flat
  verge has to reach past `CarSim.wallMargin` (the invisible wall 5 m off the
  road edge), or a car pushed off the road hovers over the hillside; and
  anything solid enough to notice - a boulder, a rock face - is placed with
  its whole radius outside that same wall (`WALL_CLEAR`), so the car never
  drives through rock.

## Game data (`track_<id>.json`)

The coordinates are already in the RealityKit convention (`x`, `y` = up, `z`).

| key | meaning |
|---|---|
| `px/py/pz` | centreline points (`py` is the height, so the profile of the lap) |
| `tx/tz` | tangent in the ground plane |
| `hw` | half width of the road |
| `surf` | surface (0 asphalt, 1 dirt, 2 sand, 3 snow, 4 ice, 5 wood) |
| `curv` | curvature - the AI derives cornering speed from it |
| `line` | the racing line as a lateral offset from the centre |
| `grid` | the starting grid |
| `cans` / `boosts` / `ramps` | fuel canisters, boost pads, jump ramps |
| `sky`, `sun`, `fog`… | theme colours for the sky and the lighting |

A boost pad fires **once per visit**, which means remembering the pad rather
than timing the turbo. Guarding on "boost is nearly spent" instead is a
property of the clock: since the pad sets 1.7 s of turbo and it burns down at
one a second, a car that came to rest on a pad - spun out, or nudged off the
line - collected a fresh one every 0.6 s for as long as it sat there, seven of
them in four seconds. `CarSim.lastBoostPad` holds the pad the car is standing
on and re-arms when it drives clear; no two pads on any circuit are closer
together than 100 m, so back-to-back pads still both count.

**A canister sits on whatever the car drives on.** The pickups are placed
from the centreline plus a lift, and a ramp is not in the centreline - it is
2 m of solid body laid on top of it, across 98 % of the road. Palm Cove has a
canister at s=262, which is on the lip of one, so the crate was struck a
metre *inside* the ramp and simply could not be seen. It still collected,
because the test is a distance in the ground plane rather than in space:
forty litres out of thin air, with the AI crossing the track to fetch
something that was not there. The lift now counts `rampHeight` in, which puts
the crate on the take-off lip where a jump can be taken through it - and
keeps every future track right without the generator having to know.

Skid marks are laid **by distance, not by frame**, and that is what makes the
three numbers behind them agree. Two marks per frame is 120 a second, so the
ring of them wrapped in 1.2 s against a life of 4.5 - every mark was recycled
while still fully opaque, so a trail popped out of existence rather than
fading, and it got *shorter* the faster you drove. Two marks every 1.6 m is 31
a second at racing speed: one car fills the ring in 6.4 s and two in 3.2,
both clear of the 2.6 s life, which is what lets a mark reach its fade.

## Driving on hills

The tracks are not flat, so the simulation reads the elevation profile back
out of `py`: `TrackData` differentiates it once into `slopes` (the gradient)
and again into `slopeRates`. `CarSim` uses both.

| what | how |
|---|---|
| climbing costs speed, dropping gains it | the component of gravity along the road is added to the longitudinal force, with the same toy-scale `CarSim.gravity` used for jumps. At 15 % a car tops out around a quarter slower uphill than down |
| a stationary car holds its place | with no throttle and no speed that force is skipped - otherwise the grid would roll away during the countdown and a car out of fuel would coast downhill for ever |
| the body follows the road | `groundPitch` leans the model by the gradient. Without it a car on a climb reads as flying up the hill with its wheels off the ground |
| the car sticks to the road | on the ground its height is set to the road, not eased towards it. Easing left the car sunk into every climb - and made it appear to take off on every descent |
| jumps come from ramps and crests only | a ramp ends at its steepest, so the car carries on along the lip. A crest only lets go when `v² ·` (profile curvature) beats gravity, which no current track's profile reaches - the roads are smooth enough that only the ramps launch |
| the camera clears the hill behind | on a descent the road the car came down is higher than the car, so the chase camera is lifted above *that*, not only above the car |

## The interface

The art direction is **die-cast toybox**. The game is about little toy cars, so
the interface is made of the materials a toy is made of - glossy
injection-moulded plastic, chrome trim, enamel paint, printed cardboard and
metal medals - rather than of flat rectangles. It is measured against the arcade
racers it competes with (Beach Buggy Racing, Mario Kart Tour, Asphalt), which
all get their tactile look the same way: every element is a *surface* with a
light side and a shadow side.

There are no image assets. Every panel, badge, dial, hill and cloud is drawn
from shapes and gradients, so the whole interface is resolution independent and
the bundle stays as small as the models make it.

### Three rules

1. **One light direction - from above.** Highlights sit on top edges, contact
   shadows underneath. `Bevel` and `Gloss` (Materials.swift) do this; nothing
   should hand-roll a rim.
2. **Display type is always outlined.** Saturated colours on saturated
   backdrops need a dark keyline to stay readable, and the genre expects it.
   `StrokeText` draws the glyphs sixteen times around a circle, because SwiftUI
   cannot stroke text.
3. **Nothing is pure grey.** Shadows are tinted with `TC.ink` (a navy), lights
   with `TC.cream` (a warm white), so a screen reads as one lit scene rather
   than as a stack of widgets.
4. **Every gap comes from the scale.** No screen picks its own numbers. A gap
   that is not on the scale is a gap that reads as a mistake next to the ones
   that are - see below.

### Space is a scale, not a guess

`Metrics` (Theme.swift) is the spacing, sizing and type scale a screen lays
itself out on. Each screen builds exactly one, from its own `GeometryReader`,
and takes every gap, margin, control height and point size from it:

```swift
GeometryReader { geo in
    let m = Metrics(geo.size)
    VStack(spacing: 0) {
        header(m).frame(height: m.pt(42))
        ...
    }
    .padding(.horizontal, m.edge)
    .padding(.vertical, m.edgeV)
}
```

The game is landscape-only, but that still spans a 375-point-tall phone to a
1024-point-tall tablet: the short edge, which is what a landscape layout has to
fit into, nearly triples. `Metrics` interpolates along `t`, 0 on the shortest
phone and 1 on a tablet. Three rules keep the result from looking like a zoomed
phone:

1. **Space grows faster than type.** Phone to tablet roughly doubles the gaps
   but moves a title by only a third. A tablet is held a little further away,
   not twice as far, so what a bigger screen buys is room *around* things.
2. **The steps are ratios, not increments.** `hair` → `tight` → `snug` →
   `item` → `group` → `section`, each about 1.5x the last. Grouping only works
   if `group` is unmistakably wider than `item`; if the two are within a couple
   of points of each other the eye cannot tell where a group ends, and a panel
   full of cream tiles reads as one undifferentiated mass. **This is the single
   thing most worth getting right on these screens.**
3. **Nothing spans the full width just because it can.** Rows of controls are
   capped at `m.column`; a button stretched across a tablet reads as a banner
   rather than as something to press. Panels are capped in height too, or a
   tablet hands a card seven hundred points to put four lines of text in.

Shared components that carry text take a `scale:` parameter (`StatRow`,
`StatTile`, `SegmentMeter`, `ChipButton`) which the caller feeds `m.type`.
Components that were already sized from one number - `MedalBadge`,
`CoinPill`, `RibbonBanner`, `TrophyRow` - just get `m.pt(…)`.

The overlays are screens too, and they were the ones quietly breaking this:
settings, pause, the countdown and the loading card all carried hard-coded
point sizes, so they stayed at phone dimensions on a tablet while everything
underneath them grew. Which of the two scales an overlay takes follows from
what is behind it:

* **Anything drawn over the live race** - the HUD, the controls, the starting
  lights, the pause button in the corner - is sized from
  `HUDView.scale(for:)`, so the instruments keep their proportion to the road
  rather than to the screen, and so the lights are not a different size from
  the gauges they are counting down to.
* **Anything that replaces the race or a menu** - the loading card, the
  pause panel, settings, the "track unavailable" card - builds its own
  `Metrics`, like every other screen.

### Landscape has no height to spare

A phone in landscape gives a screen about 380 points once the home indicator is
out, and the two-column screens spend that on a header, a content band and a
strip. The band ends up with roughly 240 - which is the budget that decides
what a side panel is allowed to contain. When a panel does not fit, the fix is
to **take something out, not to close the gaps up**, because tightening the
rhythm is what made these screens feel packed in the first place. Track
selection lost the circuit length (the poster's stamp already gives it), the
best position (the medal beside the name *is* it) and the pagination dots (the
strip below says the same thing, with names and lock states attached).

**The floor is 360, not 380.** `Metrics.t` used to bottom out at a 380-point
short edge, which makes it easy to read 380 as the smallest screen there is.
It is not: a 12/13 mini is **360** points across in landscape, and once the
home indicator is out that leaves about 310 for a full-screen panel. Between
360 and 380 nothing in the scale changed, so the two smallest phones shared a
layout tuned for the larger of them and one that only just fitted the phone
you happen to own ran off the bottom of the other. Settings did: the panel
overflowed the screen and took the lower half of its DONE button with it.

Two things came out of that, and both were needed. The scale now bottoms out
at 360, so nothing between the two sizes is flat any more - and what a
too-tall panel calls for is still to **take something out**, so Settings lost
the line saying the game is offline and saves to the device: the title screen
behind the panel prints "plays offline" in its own footprint, so it was the
one row telling the player something already on screen.

The default simulator set has nothing that small, so panels want checking on
the two that are - and it is worth having both, because they fail
differently: the mini is the shortest screen there is and the SE is the one
with no home indicator, so it keeps every point its screen has.

```bash
RT="$(xcrun simctl list runtimes \
      | grep -oE 'com.apple.CoreSimulator.SimRuntime.iOS-[0-9-]+' | tail -1)"
xcrun simctl create mini \
    com.apple.CoreSimulator.SimDeviceType.iPhone-13-mini "$RT"
xcrun simctl create se3 \
    com.apple.CoreSimulator.SimDeviceType.iPhone-SE-3rd-generation "$RT"
```

What the view actually gets, measured in landscape with the status bar and
the home indicator hidden, is **355** points of height on a mini, **375** on
an SE and **382** on an iPhone 17 - so the whole small-phone class lives
inside a 27-point band, and a card that fits one of them by a hair does not
fit the others at all. The garage's did not: it carries a shield, four
meters, two figures and a button, which comes to about 245 against a band
with 211 to give, and the car strip underneath ran off the bottom of both
small phones. Its two hard figures are now **dropped rather than shrunk**
below `GarageView.figuresFit`, the same call the title screen makes about its
hero car.

**A full-bleed row inside a rounded panel has to be clipped to it.**
`PanelSurface` is applied as a `background`, which does not mask its content,
so a row that reaches both edges of the card - a striped standings row, the
bright plate under the player's own - squares off the card's corners. It only
shows on the *last* row, because that is the one sitting inside the corner's
arc: the results table's bottom padding is a `snug` and its radius an
`rCard`, three times wider. Which meant the one player who saw it was the one
who finished last, with their own row lit gold and hanging out over the
shadow. `.clipShape` before the `.background`, so the panel keeps its
shadows.

### What lives where

| file | what it holds |
|---|---|
| `Theme.swift` | colours, metals, type scale, depth tokens, `Metrics` (the layout scale), formatting |
| `Materials.swift` | `Bevel`, `Gloss`, `Sheen`, `Grain`, `Vignette`, the composed `PanelSurface`/`GlassSurface`, `StrokeText`, and the racing motifs (`CheckerStrip`, `HazardStripes`, `SpeedLines`) |
| `Buttons.swift` | `ChunkyButtonStyle` and the round/glass variants, `ToyToggle`, `ToySegmented`, `ChipButton` |
| `Badges.swift` | coins, medals, stars, class shields, price tags, ribbons, segment meters |
| `Gauges.swift` | the race instruments: speedometer, fuel gauge, position plate, lap timer |
| `Backdrop.swift` | the painted scenes and the garage showroom |
| `Logo.swift` | the wordmark, built as a badge |

### Buttons have thickness

`ChunkyButtonStyle` is a moulded cap sitting on a darker base. A press moves
the cap down by exactly `depth`, and the shadow under the cap disappears in the
same frame - which is what makes it read as a button travelling rather than a
colour changing. `sheen: true` adds a slow light sweep and is reserved for the
one button the player should press next.

### The backdrops are scenes, not gradients

A flat gradient behind a menu is the clearest tell of a prototype, so each
screen gets a small painted world: a graded sky, a sun with turning rays, two
cloud banks, three ridgelines at different parallax depths, a themed scenery
band (palms, pines or round-canopy trees) and a play-mat road running
towards the viewer. `ScenePalette` carries the whole thing as data, so
`ScenePalette.forTheme(track.theme)` dresses any screen in a circuit's weather.

Two things are easy to get wrong here and both were:

* **Layers are placed from the top, never stacked.** A `ZStack` centres a child
  that is shorter than the stack, so an offset from a `ZStack` means "from the
  middle" - which silently pushes a horizon band half a screen down.
  `Backdrop.band(y:w:h:)` takes the distance from the top of the screen and
  means it, and `Backdrop.ridge(peak:…)` works an amplitude and a baseline back
  from how far above the horizon a range should reach.
* **Distance is three cues at once**: things further away move slower, lose
  contrast, and take on the horizon colour. `Ridge.haze(_:_:nearness:)` applies
  the last two, mixing the *colours* rather than stacking a translucent wash -
  a wash would let the sky show through the hills. `nearness` is 1 right in
  front of the viewer and 0 on the horizon; inverting it turns a green
  landscape beige.

### The car in the menus is the real model

A `RealityView` on iOS composites over whatever is behind it as long as no
environment is set, so `CarPreviewView` uses its studio image for lighting only
and the car stands directly on the painted scene - on the play-mat road on the
main menu, on a turntable in the garage. The rig is a three point studio: a key
light with a real shadow, a cool fill so the shadow side is not black, and a
rim light tinted with the car's own paint, which is what separates a dark body
from a dark room. Set `drawDome: true` to get the studio dome as an opaque
backdrop instead.

Two numbers there are load-bearing, and only one of them is written down. The
deck's **roughness** is: a polished deck mirrors the paint-tinted rim light
and the plinth turns into a gold platter under a yellow car, so it is matte.
The turntable's **radius** is not, and that is the point - it is worked out
from `CarCatalog` at build time, because the longest car in the catalogue has
to stand wholly on the deck and a radius written down goes stale the moment a
longer car is added with nothing to say so. It had: the deck sat at a flat
1.80 m, which is 3.6 m across, against Bolt's 4.35 m - so the quickest car in
the showroom was displayed with its rear wing and back wheel hanging over the
edge into the dark.

### The HUD

Laid out by how often each thing is asked for, and kept off the driving line -
the thumbs own the bottom corners and the player's own car owns the middle, so
the speedometer sits in the gap between the car and the pedals rather than
centred over the car it is reporting on.

| where | what | why |
|---|---|---|
| top left | position and lap | checked constantly, so it gets the metal of the place currently held |
| under it | fuel | segmented, with the last two segments printed red as the reserve |
| top centre | lap time, last lap, delta against best | a green delta is the most useful number a racer can be given |
| top right | the map | glanced at before a corner |
| bottom, right of the car | speed and turbo | felt rather than read: a needle on a fixed arc, red zone at the top |

Over the instruments sits a layer of screen effects - vignette, speed lines
that build with speed, a cyan wash under turbo, warm dust off the tarmac, a red
pulse on the last of the fuel and a flash across the line. They are what makes
60 km/h feel like 60 km/h. They also have to stay cheap: the HUD redraws every
simulation frame, so there is no blur, no wide shadow and no `TimelineView`
anywhere inside it. That includes the map: `CarDot` is drawn eight times a
frame, and the halo marking the player's own car is two flat rings rather than
a blurred disc, with a dark keyline in place of the contact shadow the rest of
the interface gets. The map's scale comes from `TrackData.extent`, worked out
once on load - fitting it used to mean rescanning all 2,700 centreline samples
every frame for a figure that cannot change.
The dial readout uses `CarSim.speedKph`, which exaggerates
by 1.9 for a toy-scale dial - the garage quotes the same number through
`CarDef.displayTopSpeed`, or the two screens contradict each other.

### Reduce Motion and VoiceOver

Two things a screen full of moving plastic owes the system.

**Reduce Motion** stops the layer that moves for its own sake and nothing
else: the drifting clouds, turning sun rays and scrolling road in `Backdrop`,
the showroom's light flicker, the badge and car breathing on the title
screen, the confetti on a podium, the light sweep across the primary button
(`Sheen`), the turntable the car stands on in the garage and on the title
screen, and in the HUD the speed lines, the lap flash and the pulse on the
fuel gauge and behind it - which stay lit, just still. The instruments are
information, so they are untouched, and so is the race itself.

The last three of those were the ones that got away, and each got away for
its own reason worth knowing:

* **The turntable** is driven by a `Timer` on `PreviewHolder`, which is not a
  `View` and so never saw the environment. It was the largest piece of
  standing motion in the interface and the only thing still moving on a
  screen that had been asked to hold still - with the badge breathing to a
  stop right beside it. It now settles at the three-quarter pose the spin
  starts from, which is what the stage is lit for; an unrotated pivot points
  the car straight down the lens.
* **`Sheen`** is a `TimelineView(.animation)`, so it repaints every display
  frame for as long as it is on screen, and it carries nothing - it is there
  to say "press this". It sits on the primary button of every screen, which
  includes RESUME, so it was also repainting at 60 Hz behind a *paused* race.
* **`FuelGauge`'s** pulse was armed unconditionally in `onAppear`. A race
  starts on a full tank, so that was a `repeatForever` with nothing to show,
  running for the whole race on a view that redraws every simulation frame.
  It is now started and stopped by the tank, and Reduce Motion holds the
  warning at its bright end rather than its dim one. `HUDView`'s screen-wide
  wash was already doing this correctly; the gauge was the half of the same
  warning that was not.

One threshold, not three: `RaceEngine.lowFuelFraction` is what the gauge, the
screen wash and the on-screen message all warn on. The gauge used to carry
its own 0.20 against the other two on 0.18, so in that band the instrument
flashed alone - which reads as an instrument fault, not as a low tank.

The confetti is thrown **once** rather than cycling. Its timeline used to run
for as long as the player sat reading their result, and it put every piece's
two face colours through a `UIColor` HSB round trip inside the draw closure -
some six thousand colour-space conversions a second for two constants per
piece. Both are struck in `init` now, and the view takes itself down once the
last piece is off the bottom.

**VoiceOver** cannot play a real-time racer, but nothing should be anonymous
to it either. Every control whose label is only an icon carries an
`accessibilityLabel` - pause, the two back buttons, the settings close, the
garage's car arrows, the steering pad and both pedals - and the HUD's effects
layer is hidden from it outright, because a vignette is not worth reading
out.

## Progression and the showroom

Coins come from finishing; a top-three finish clears a circuit and clears the
next one for entry. Cars are bought in `CarCatalog.order`, and the prices in
`CarDef.prices` run up that order, so **the order is the ladder**: every car
must be a better car than the one before it.

That is not a comment, it is a test. `CarDef.overall` is the weighted rating
the garage prints as a class shield (D…S), and `CarBalanceTests` asserts that
it rises at every step of the order and that the class never drops. The ladder
had drifted badly before those tests existed - Scoop cost 850 coins and was
the only class-D car in the game, worse than either free starter.

**What is *not* claimed is that the individual stats rise with it**, and this
is worth being exact about, because the obvious reading of "a better car" is
wrong here. Each step is a **trade**, and the numbers say so:

| | top speed | accel | grip | tank |
|---|---|---|---|---|
| chili (free) | 36.7 | 20.4 | 4.61 | 92 |
| sprout (350) | 32.6 | 18.7 | 5.36 | 110 |
| … | | | | |
| bolt (1400) | 42.2 | 25.7 | 4.98 | 88 |
| neo (2000) | 41.5 | 23.7 | 5.55 | 102 |

Sprout is four metres a second slower in a straight line than a car you were
given, and buys sixteen per cent more grip and a fifth more range with it.
Neo gives up a little of Bolt's pace for the same two things. Every one of
the derived figures - `topSpeed`, `acceleration`, `gripBase`, `maxYawRate`,
`fuelCapacity` - falls somewhere along the order, and it has to: a ladder
where each car beats the one below it at everything leaves the player nothing
to choose and no reason ever to keep an earlier car for a twisty circuit.

So the per-step promise `everyStepOfTheLadderBuysSomething` holds is the
weaker and truer one: **no step is a pure downgrade** - every car is strictly
better than the one before it in at least one figure the physics reads. The
comment on `derivedStatsAgreeWithTheRating` used to claim the strong version
while its assertions only ever compared the first car with the last, so the
strong version was neither true nor tested.

The stars beside the shield are the shield coarsened - D/C one, B two, A/S
three - rather than a scale of their own, so the two cannot disagree about
the shape of the ladder. They were `1 + Int(overall * 2.7)`, which is a
truncation of a range the catalogue does not use: `overall` runs 0.33 to 0.85
here, so **six of the eight cars came out at exactly two stars** while the
shield beside them read C, B, B, B, A, A, and Bolt sat seven thousandths
short of a third star. `starsCarryTheSameLadderAsTheShield` holds the range
open.

The stats themselves live in `gen_cars.py`, not in Swift, and reach the app
through `cars_manifest.json`. Change them there and regenerate.

The field is built to match: `RaceController.pickOpponents` fills the grid
from the cars nearest the player's own in class, widening with the circuit's
difficulty. Drawn freely from the catalogue it put a Neo on the grid of the
first race next to a free Bumble, and since the stats dominate these races the
finishing order simply read the price list back.

## Tests

```bash
xcodebuild test -project ToyCars.xcodeproj -scheme ToyCars \
    -destination 'platform=iOS Simulator,name=iPhone 17 Pro'
```

The suite is deliberately aimed at the things that are hard to see by playing:
the showroom ladder above, the self-consistency of the generated track files,
the behaviour of `TrackData.project` (see below), the race rules driven
through the same fixed timestep the game uses, and two things that fail
*silently* rather than visibly -

A test is only worth what its assertions say, not what its comment says.
`derivedStatsAgreeWithTheRating` promised in prose that "a car that scores
higher must not be slower, weaker or less grippy on track" and then compared
the first car in the order with the last - eight cars, one comparison, six
steps unchecked. The prose was also false, which is the more interesting
half: it had been written from what the ladder ought to look like rather than
from what the numbers do. When a claim and an assertion drift apart it is
worth checking which of the two is wrong before fixing the other.

* **`ProgressTests`** - that every profile the game can reach still encodes.
  `TrackRecord`'s times used to default to `.infinity`, which `JSONEncoder`
  refuses to write, and a retirement stores that default record untouched: so
  the encode threw, `save()` swallowed it with `try?`, and from the first DNF
  on an uncleared circuit nothing reached the disk again for the rest of the
  session - the coins from that race, any car bought after it, every setting
  and every unlock. The times are `Double?` now and `GameProgress.improve` is
  the only way one gets in, so nothing the format cannot represent can. The
  same bug had a second fuse in it: the `trackVersion` migration sets every
  record's times back, so the next time the tracks are rebuilt it would have
  shipped a build that could not save at all.
* **`RaceControllerTests`** - that a restart actually starts. The pause panel
  sets `paused` when it opens and only RESUME ever cleared it, so RESTART
  reloaded the race with `tick` still short-circuiting on the first line: the
  new race sat frozen on the grid with the controls dead.

### Why the projection window is short

`TrackData.project(_:hint:)` searches ±16 samples around the car's index from
the previous frame - about ±11 m, against the under half a metre a car covers
in a 1/120 s step. It used to search ±70, which was both the most expensive
thing in the simulation and wrong: on Sunset's tightest corner a car against
the barrier on the way in is physically *nearer* the centreline of the way
out, some 27 samples away, and the wide window took it. That moved the car 19 m
round the lap in one frame, and `CarSim` only rejects jumps past a quarter of
a lap, so the rest went into the standings as distance driven.

So the search is kept local on purpose, and it is not widened when the nearest
sample lands on the edge of the window; only a point further from the road
than any barrier - a fresh placement, never a car that drove there - falls
back to the global search. `projectionStaysNearAGoodHint` is the test that
holds this, and it fails at ±70.

One thing the geometry cannot fix: Sunset's tightest corner has a 7.1 m radius
while the barrier sits 11-13 m off the centreline, so the inside barrier line
folds back through itself and, at the apex, the inside of the road passes
within 1.7 m of the centre of curvature. A dozen centreline samples are all
but equidistant there and the nearest one flickers by about 2 m - with an
exhaustive search just the same. The road surface itself never folds
(`theRoadSurfaceNeverFoldsBackOnItself`), but there is not much room left, and
a future circuit with a corner tighter than the road is wide would break the
simulation and not only the mesh.

### The AI and fuel

A tank is worth about a lap and the races are two or three, so every car has
to pick up two or three canisters on the way round. The AI was not doing it,
and the reason was twofold: it re-chose its target on every frame, which is
120 times a second, so whenever two canisters scored alike the winner flipped
as the car moved and it weaved between the pair; and even when it did settle
on one, it steered at a point on the centreline twenty-odd metres ahead
carrying the canister's lateral offset, rather than at the canister, whose
pickup radius is 2.6 m. Cars went whole races collecting nothing and coasted
to a stop on the last lap.

It now commits to a target until that target is taken, passed or too far off,
aims at the canister itself once it is close, starts topping up as soon as the
tank is off full - what changes with the level in it is how far out of its way
it will go - and lifts off in the last quarter of a tank when there is nothing
in reach, since consumption is charged against throttle per second and limping
home a few seconds down beats stopping dead. Measured over 20 headless races
on Sunset, retirements went from 33 to 5. `AIDriverTests` holds the line.

Every random number an `AIDriver` draws comes from its own seeded `SplitMix`,
so a headless race replays exactly. It did not use to: the generator was
built in the initialiser, used for two values and thrown away, while the
mistakes each driver makes every few seconds came from the global one. Two
identical races came out differently, and the `seed` `AIDriverTests` passes
in reached nothing but the autopilot's noise phase.

### Finishing the race after the player has

The results table needs everyone's time, so when the player crosses the line
`fastForwardToFinish` drives the rest of the field home with the callbacks
unhooked. Three numbers keep it honest and they have to be read together:

* **The budget comes from the race that is left**, not from a constant. It
  used to be a flat 90 s - shorter than a whole race on any of the three
  circuits (132 s, 141 s, 159 s), so it could not finish one even from the
  start line. A player who ran dry on the first lap was handed a results
  table with six of the eight cars marked DNF, which is precisely the "this
  game is broken" reading the function exists to prevent. It is now the
  distance the last car still owes at a pessimistic 9 m/s, capped at 300 s.
* **It stops early when the field has stopped**, measured as the *total*
  distance still outstanding - a figure that only falls, because a car that
  finishes leaves the sum. Watching the leader instead does not work: the
  moment the leader crosses the line the set loses its best member and the
  figure jumps backwards, which reads as a stall. That bug put the six DNFs
  straight back after the budget had been fixed.
* **It stops rubber-banding.** The handicap is a gap to the *player's*
  progress, and by now the player is out of the race - home, or stopped on
  the road out of fuel - so that figure stands still while the field drives
  away from it. Every remaining car therefore read as a runaway leader and
  was held to 95.5 % for the whole catch-up, which put four per cent on
  every time the results table prints. `rubberBand` now applies only while
  the phase is `.racing`, which also covers the two and a half seconds
  between the flag and the results screen.
* **It runs at 1/60, not the live 1/120.** Nobody watches this simulation and
  only its finishing order is read back out, so it costs half as much. The
  forces are mild enough at that step that lap times move by hundredths.

And it is handed out in slices. The ordinary case - the player finishes and
the field is seconds behind - is well under a tenth of a second's work, but a
first-lap retirement on Frostpeak is eighteen thousand fixed steps, which
measured 145 ms in an optimised build: a freeze at the exact moment the
results screen slides in. `RaceView.finish` therefore stops the display link
first and then calls `catchUpToFinish`, which runs ~240 steps at a time and
yields between the slices, so the main thread is never held for more than a
couple of milliseconds. Slicing changes only *when* the steps run, never what
they compute, and `RaceEngineTests.slicingTheCatchUpChangesNothing` holds it
to that by comparing a sliced run against a one-shot one time for time.
`fastForwardToFinish` is the same catch-up in one go, for the tests.

The placing the results screen reports comes out of `snapshotStandings`, the
same sort that builds the table shown beside it, and is read *after* the
catch-up. Both of those matter: taking it from `CarSim.position` read it off
the previous step's sort - the one before the finish it was reporting - and
reading it before the catch-up gave a retirement a place from before the rest
of the field came home.

## Debug switches

The app can be launched straight into a race:

```bash
xcrun simctl launch booted cz.rob.ToyCars -race sunset -autopilot -fps
```

| switch | what it does |
|---|---|
| `-race <id>` | skips the menu and goes straight to the given track |
| `-car <id>` | unlocks and selects a car |
| `-laps <n>` | shortens the race |
| `-autopilot` | the AI drives the player's car (for testing) |
| `-steertest` | holds a gentle right-hand turn and logs `lateral` (heading check) |
| `-nofuel` | the player starts almost out of fuel (range test) |
| `-screen garage\|tracks\|settings\|results` | opens the given screen directly |
| `-pos <n>` | with `-screen results`, the finishing position to show; past the end of the field it shows a retirement |
| `-track <n>` | with `-screen tracks`, the circuit to open on - the way to look at a locked one |
| `-fps` | shows the frame counter |
| `-tut` | forces the intro tutorial |
| `-rich` | 5000 coins, for looking at the garage with everything affordable |

The switches compose: `-car` and `-rich` say nothing about *where* the app
should open, so they are read first and for every launch, whatever `-screen`
or `-race` does afterwards. They used not to - `-rich` was read only inside
the `-screen` branch and `-car` only inside the `-race` one, and the `-screen`
branch returns early - so `-rich` on its own left the player on 250 coins,
`-screen garage -car neo` opened the garage on whatever car the profile
already had, and `-race sunset -rich` handed over nothing. Each of those
still cost the session its writes, because the mere presence of the switch
sets `writesSuppressed`: the promise below was being kept while the switch
itself did nothing.

None of them write to the saved profile themselves. They are applied in
`AppModel.launch()` rather than in `RootView.init` - a `View` is a value the
system rebuilds whenever it likes, and the version that lived in that
initialiser called `markTutorialSeen()`, which writes to disk: one `-race`
launch turned the intro tutorial off for good.

Nor can they reach it later. The switches that hand things out - `-rich`,
`-car` - and the ones that change what a race means - `-laps`, `-nofuel` -
only ever changed `state` in memory, which was exactly as far as the promise
went: the first legitimate `save()` after one of them, and finishing a race
is enough, wrote the whole of `state` back out including the debug part. So
the presence of any switch in `GameProgress.debugArguments` now suppresses
every write for the rest of the session (`GameProgress.writesSuppressed`),
and `ProgressTests/aDebugLaunchWritesNothingToDisk` holds it to that. A debug
launch is a *look* at the game, not a session of playing it, and it leaves
the profile on disk exactly as it found it.

`-fps` is not in that list, because it changes nothing to save.

## Audio

No audio files. Engine, skid and wind are generated by an `AVAudioSourceNode`
in real time (two saws plus a fifth, filtered noise), and the sound effects
are short synthesised buffers. The engine pitch tracks the revs, including
the "gear changes".

The session is `.ambient` with `.mixWithOthers`, so the game never interrupts
the player's own music and honours the silent switch.

Four different things stop the engine from outside, and `GameAudio` has to
notice all of them, because `start()` returns early while it believes it is
already running - so a stop it missed is silence for the rest of the session,
which walking back out to the main menu cannot undo either.

| what happens | how it comes back |
|---|---|
| a phone call or Siri | `interruptionNotification`, resumed on `.ended` if the system says it may |
| headphones out, a speaker going away | `routeChangeNotification`, plus `AVAudioEngineConfigurationChange` for a graph the route took down with it |
| the app is sent to the background | **nothing is posted at all** - an `.ambient` session is simply torn down with the app, so the only signal is `scenePhase` going back to `.active`, which `RootView` turns into `GameAudio.resume()` |
| the audio server dies | `mediaServicesWereResetNotification` - everything built on it is invalid, so the graph is thrown away and made again |

Only the last of those rebuilds. The other three restart the engine on the
graph that is already there.
