"""
gen_tracks.py - generates the 3 tracks (usdz + game JSON).

  Blender -b --factory-startup --python tools/blender/gen_tracks.py -- \
      [--only ID] [--preview] [--top] [--parts]

--preview renders the racing view, a scenic shot from the highest point of
the lap and the whole landscape from outside; --top adds the plan view and
--parts prints the triangle count per object.

A track is authored as two things: a landscape (`land`, see lib/tcland.py)
and a plan view - either `control`, the x/y points with their width and
surface, or a `route` of straights and arcs that is solved into those points
(`tcspline.route_control`), which is how a circuit with a deliberate shape -
a named sweeper, a hairpin of a given radius - is easiest to write and to
read back. The road's elevation is then *sampled from the landscape*,
so the track climbs and drops with the ground it is carved into instead of
lying on a plane. `max_grade` keeps it drivable; whatever the limiter takes
off becomes a cutting or an embankment in the terrain beside the road.
"""
import bpy
import sys
import os
import math
import random
import json

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
from lib import tcmesh as T          # noqa: E402
from lib import tcrender as R        # noqa: E402
from lib import tcspline as S        # noqa: E402
from lib import tctrack as K         # noqa: E402
from lib import tcland as LA         # noqa: E402
from lib import tcprops as P         # noqa: E402

OUT = os.path.abspath(os.path.join(_HERE, "..", "..", "ToyCars", "Assets3D"))
PREVIEW = os.path.abspath(os.path.join(_HERE, "..", "preview"))
TAU = math.pi * 2

A, D, SA, SN, ICE, W = (K.SURF_ASPHALT, K.SURF_DIRT, K.SURF_SAND,
                        K.SURF_SNOW, K.SURF_ICE, K.SURF_WOOD)

# CarSim holds the car inside `hw + wallMargin` (5 m past the road edge), so
# anything solid enough to notice - a boulder, a rock face - has to keep its
# whole body outside that, radius included. Small trackside dressing (cones,
# tyre stacks, snow banks) is deliberately allowed inside it.
WALL_CLEAR = 5.6


# ================================================================== TRACKS

TRACKS = {}

# ------------------------------------------------------------------ 1. Sunset
# A classic circuit thrown over rolling hills: a flat valley straight, a long
# climb up the eastern rise, a gravel loop over the middle mound and a fast
# descent down the western ridge.
TRACKS["sunset"] = dict(
    id="sunset", name="Sunset Circuit",
    subtitle="Classic circuit in the evening sun",
    laps=2, seed=11, difficulty=1,
    unlock=0, reward=120,
    max_grade=0.15,
    cans=26, boosts=14,   # some land in corners and are dropped
    land=dict(
        base=0.0, detail=2.6, detail_scale=0.0105, rough=0.55,
        # (x, y, radius, height) - a negative height digs a valley
        hills=[
            (130, 105, 310, 23),        # the hill in the north-east
            (205, -60, 270, 17),        # the eastern rise
            (-130, -110, 280, -5),      # the valley the start straight runs in
            (-182, 155, 250, 18),       # the western knoll
            (25, 55, 170, 13),          # the mound the gravel loop climbs
            (95, -35, 170, -7),         # a dip behind the eastern hairpin
            (150, -420, 320, 20),       # low hills south of the straight
        ],
        # (x0, y0, x1, y1, radius, height)
        ridges=[
            (-170, 48, -36, 92, 140, 12),
        ],
        peaks=[                         # distant hills on the horizon
            (-380, 300, 250, 86),
            (350, 390, 260, 78),
            (450, -210, 250, 72),
            (-410, -310, 260, 64),
        ],
    ),
    # (x, y, half width, surface)
    control=[
        (-113, -181, 8.4, A),
        (-31, -187, 8.4, A),
        (52, -183, 8.0, A),
        (115, -165, 7.4, A),
        (157, -132, 7.0, A),
        (177, -89, 6.8, A),
        (181, -41, 6.8, A),
        (163, -4, 6.6, A),
        (122, 8, 6.6, A),
        (78, -2, 6.8, D),
        (39, 6, 7.0, D),
        (10, 33, 6.8, D),
        (31, 66, 6.6, D),
        (2, 91, 6.6, D),
        (29, 122, 6.6, A),
        (72, 132, 6.6, A),
        (109, 157, 6.8, A),
        (103, 190, 6.4, A),
        (54, 200, 6.4, A),
        (4, 185, 6.8, A),
        (-25, 150, 7.0, A),
        (-60, 130, 7.0, A),
        (-107, 146, 6.6, A),
        (-150, 169, 6.4, A),
        (-177, 128, 6.6, A),
        (-165, 82, 7.0, A),
        (-126, 54, 7.2, A),
        (-87, 37, 7.2, A),
        (-60, 2, 7.4, A),
        (-80, -41, 7.4, A),
        (-122, -62, 7.6, A),
        (-163, -103, 7.8, A),
        (-165, -150, 8.0, A),
    ],
    ramps=[dict(s_frac=0.107, length=10.0, height=2.2),
           dict(s_frac=0.213, length=9.5, height=2.0),
           dict(s_frac=0.549, length=10.5, height=2.3)],
    start_s_frac=0.02,
    theme="classic",
    sky="#ffb46b", sky2="#4a7fd0", fog="#ffd9a8", fogDensity=0.0022,
    sun=[-0.42, -0.72, -0.55], sunColor="#fff0d0", sunIntensity=2.6,
    ambient="#8fb4e8", ambientIntensity=0.55,
)

# ------------------------------------------------------------------ 2. Cove
# The fast one. Where Sunset snakes, the cove is a shoreline blast: a 300 m
# beach straight, a 220 m wooden sweeper right round the point, two gentle
# dune kinks and exactly one slow corner - the hairpin against the rocks at
# the western end. Five hundred and fifty metres wide and two hundred deep,
# because a coast road is a strip and not a loop - and flat, because it is a
# beach. Three laps, since the lap is the shortest in the game.
TRACKS["cove"] = dict(
    id="cove", name="Palm Cove",
    subtitle="Sandy cove, palm trees and piers",
    laps=3, seed=23, difficulty=2,
    unlock=1, reward=180,
    max_grade=0.075,
    cans=22, boosts=12,   # some land in corners and are dropped
    land=dict(
        # The sea is a trench dug along the shore - the waterline is simply
        # where the ground drops below `water_level`, so a straight trench
        # gives a straight beach, and the point off the east end gets its
        # own. The road corridor itself stays within a couple of metres of
        # sea level: what a beach road looks like is flat tarmac with the
        # dunes rising *beside* it, so the tall ground is all kept beyond
        # the loop, where the road never has to climb it.
        base=1.9, detail=1.3, detail_scale=0.0145, rough=0.25,
        hills=[
            (10, 55, 185, 8),           # the dune the boardwalk crosses
            (-165, 25, 165, 9),         # dunes inside the hairpin
            (150, 20, 150, 5),          # a low dune in the middle of the loop
            (0, 300, 230, 26),          # the dune wall along the back
            (-250, 250, 210, 22),
            (250, 240, 200, 18),
            (-420, 85, 220, 26),        # the rocky point at the west end
        ],
        ridges=[
            (-350, -400, 400, -400, 300, -30),   # the sea along the south
            (545, -300, 545, 300, 300, -28),     # and off the eastern point
            (-60, 95, 130, 60, 90, 5),           # the dune ridge behind
        ],
        peaks=[                         # low headlands, no mountains
            (-470, 260, 210, 30),
            (-430, -230, 220, 22),
        ],
    ),
    # ("s", length, half width, surface) / ("a", turn, radius, half width, ...)
    route=[
        ("s", 70, 8.6, SA),         # the beach straight - start and finish
        ("a", -10, 260, 8.6, SA),   # it bends with the bay
        ("s", 60, 8.4, SA),
        ("a", 10, 260, 8.2, SA),
        ("s", None, 8.0, SA),
        ("a", 180, 70, 7.4, W),     # the Boardwalk - a flat-out 180 on planks
        ("s", 70, 7.2, SA),
        ("a", -20, 170, 7.2, SA),   # the dune kinks
        ("s", 55, 7.2, SA),
        ("a", 20, 170, 7.2, SA),
        ("s", 45, 7.0, W),
        ("a", -55, 52, 6.6, W),     # the jetty ess
        ("a", 55, 52, 6.6, W),
        ("s", 85, 7.0, SA),
        ("a", 180, 21, 6.2, SA),    # Rock Hairpin - the only slow corner
        ("s", 55, 7.0, SA),
        ("a", -90, 44, 6.8, SA),
        ("s", None, 7.6, SA),
        ("a", 90, 50, 8.0, SA),     # the long left back onto the beach
    ],
    route_start=(-102.0, -93.5),
    ramps=[dict(s_frac=0.190, length=10.0, height=2.2),
           dict(s_frac=0.410, length=9.5, height=2.0),
           dict(s_frac=0.710, length=10.0, height=2.1)],
    start_s_frac=0.03,
    theme="beach",
    sky="#67d0f2", sky2="#0f7fbf", fog="#cdeffb", fogDensity=0.0018,
    sun=[-0.30, -0.62, -0.72], sunColor="#fff8e2", sunIntensity=3.0,
    ambient="#a8dcf5", ambientIntensity=0.70,
    water_level=-1.4,
)

# ------------------------------------------------------------------ 3. Frost
# The hard one, and a road rather than a circuit: out of the valley and up
# the face of the massif on four stacked switchbacks - 160 degree turns at a
# 20 m radius, the tightest in the game - across the icy plateau at the top
# and then a 370 m sweeping descent back down the western shoulder. Five
# hundred metres tall and under four hundred wide, which is what a pass looks
# like from above; 87 m of climb a lap, at the 19 % the grade limiter allows.
# The grid is down on the valley floor - the only flat ground on the track -
# so a lap starts with the climb and ends flat out downhill.
TRACKS["frost"] = dict(
    id="frost", name="Frostpeak",
    subtitle="Frozen pass full of ice patches",
    laps=2, seed=37, difficulty=3,
    unlock=2, reward=250,
    max_grade=0.19,
    cans=28, boosts=15,   # some land in corners and are dropped
    land=dict(
        base=1.0, detail=3.0, detail_scale=0.0095, rough=0.95,
        hills=[
            (10, 330, 470, 62),         # the massif the switchbacks climb
            (-120, -300, 320, -10),     # the valley floor
            (-250, 60, 240, 18),        # the shoulder the descent runs down
            (150, -60, 200, 8),         # a shelf under the lower switchbacks
            (60, 90, 170, -7),          # a hollow between the upper ones
        ],
        ridges=[
            (-70, 150, 60, 210, 130, 14),   # the spur below the plateau
        ],
        peaks=[
            (170, 430, 260, 128),
            (-330, 400, 250, 116),
            (350, 130, 240, 104),
            (-390, -120, 250, 96),
            (60, -540, 300, 92),        # far enough out that its skirt
                                        # does not tilt the valley floor
            (400, -330, 250, 92),
        ],
    ),
    route=[
        ("s", None, 8.0, SN),       # the valley link
        ("a", 90, 50, 7.2, SN),     # the valley gate - into the climb
        ("s", 28, 7.0, SN),
        ("a", -80, 42, 6.8, SN),
        ("s", 80, 6.8, SN),         # first traverse
        ("a", 160, 20, 6.0, SN),    # switchback 1
        ("s", 125, 6.8, ICE),       # the shaded traverse - iced over
        ("a", -160, 20, 6.0, SN),   # switchback 2
        ("s", 80, 6.8, SN),
        ("a", 160, 21, 6.0, SN),    # switchback 3
        ("s", 125, 6.8, SN),
        ("a", -160, 21, 6.0, ICE),  # switchback 4, on ice
        ("s", 60, 6.6, SN),
        ("a", 80, 44, 6.6, SN),     # out onto the plateau
        ("s", 40, 6.2, ICE),        # the plateau - the narrowest road here
        ("a", 90, 52, 6.4, SN),     # the crest
        ("s", 165, 6.6, SN),        # the top traverse, along the summit
        ("a", 90, 70, 7.0, SN),     # and over the edge
        ("s", 70, 7.2, SN),         # the descent
        ("a", 20, 180, 7.2, SN),    # which swings out over the shoulder
        ("s", 70, 7.4, SN),
        ("a", -20, 180, 7.4, SN),
        ("s", None, 7.4, SN),       # the run to the line
        ("a", 90, 60, 7.8, SN),     # the valley corner
    ],
    route_start=(-150.0, -249.0),
    ramps=[dict(s_frac=0.155, length=10.0, height=2.2),
           dict(s_frac=0.410, length=9.5, height=2.0),
           dict(s_frac=0.650, length=11.0, height=2.6)],
    start_s_frac=0.035,
    theme="winter",
    sky="#bfe3f7", sky2="#5f9fd8", fog="#e8f4fb", fogDensity=0.0032,
    sun=[-0.36, -0.52, -0.78], sunColor="#eaf2ff", sunIntensity=2.2,
    ambient="#c8dcf0", ambientIntensity=0.85,
)


# ================================================================== THEMES

def make_palette(theme_id):
    """Materials for a given theme. The colours are deliberately bright and cheerful."""
    M = T.material
    pal = dict(
        metal=M("Metal", "#b9c2cf", roughness=0.3, metallic=0.85),
        tyre=M("Tyre", "#2b2e35", roughness=0.85),
        wood=M("Wood", "#b07a45", roughness=0.75),
        lamp=M("Lamp", "#fff4d0", roughness=0.2, emission="#ffe9a8",
               emission_strength=1.4),
        can=M("Can", "#ff4d3d", roughness=0.35, metallic=0.25),
        can_dark=M("CanDark", "#3a3f4b", roughness=0.5),
        can_light=M("CanLight", "#ffe066", roughness=0.4),
        cone=M("ConeOrange", "#ff7a2f", roughness=0.55),
        hay=M("Hay", "#e0bd63", roughness=0.9),
        banner=M("Banner", "#ff3d6e", roughness=0.6),
        roof=M("Roof", "#e8583f", roughness=0.55),
        stand=M("Stand", "#7d8899", roughness=0.7),
        seat_a=M("SeatA", "#ff6b6b", roughness=0.7),
        seat_b=M("SeatB", "#4ecdc4", roughness=0.7),
        umb_a=M("UmbA", "#ff5f8d", roughness=0.6),
        umb_b=M("UmbB", "#fff0d8", roughness=0.6),
        ice=M("Ice", "#bfe9ff", roughness=0.12, metallic=0.0, clearcoat=0.9),
        snow=M("Snow", "#f4faff", roughness=0.62),
        rock=M("Rock", "#8d8a86", roughness=0.9),
        window=M("Window", "#3a4c60", roughness=0.12, clearcoat=0.8),
        pole=M("Pole", "#f2f4f7", roughness=0.5),
        pole_tip=M("PoleTip", "#ff5a3d", roughness=0.5),
    )
    if theme_id == "classic":
        pal.update(
            bark=M("Bark", "#7a5334", roughness=0.85),
            leaf=M("Leaf", "#4cb14a", roughness=0.75),
            road=M("Asphalt", "#4a4f58", roughness=0.72),
            road_dirt=M("Dirt", "#a9784b", roughness=0.9),
            grass_a=M("GrassA", "#59bd58", roughness=0.85),
            grass_b=M("GrassB", "#50b04f", roughness=0.85),
            grass_c=M("GrassC", "#66c862", roughness=0.85),
            soil=M("Soil", "#b79263", roughness=0.9),
            kerb_a=M("KerbA", "#e8402f", roughness=0.5),
            kerb_b=M("KerbB", "#fdfdfd", roughness=0.5),
            line=M("Line", "#fdfdfd", roughness=0.45),
            check_a=M("CheckA", "#22252c", roughness=0.5),
            check_b=M("CheckB", "#fdfdfd", roughness=0.5),
            border=M("Border", "#2f333b", roughness=0.7),
            barrier_a=M("BarA", "#e8402f", roughness=0.55),
            barrier_b=M("BarB", "#fdfdfd", roughness=0.55),
            ramp=M("Ramp", "#c07d33", roughness=0.72),
            ramp_trim=M("RampTrim", "#fdfdfd", roughness=0.5),
        )
    elif theme_id == "beach":
        pal.update(
            bark=M("PalmBark", "#a9793f", roughness=0.85),
            leaf=M("PalmLeaf", "#3fbf72", roughness=0.7),
            road=M("SandRoad", "#cd9a5d", roughness=0.94),
            road_wood=M("Boardwalk", "#a9743c", roughness=0.8),
            road_wood2=M("Boardwalk2", "#b17c43", roughness=0.8),
            sand_a=M("SandA", "#f8e6b4", roughness=0.92),
            sand_b=M("SandB", "#efd9a0", roughness=0.92),
            sand_wet=M("SandWet", "#d6b97c", roughness=0.7),
            grass_a=M("DuneGrass", "#8fc96a", roughness=0.85),
            water=M("Water", "#2fb4e0", roughness=0.06, metallic=0.0,
                    clearcoat=1.0),
            water_deep=M("WaterDeep", "#1176b4", roughness=0.08),
            kerb_a=M("KerbA2", "#ff6b6b", roughness=0.5),
            kerb_b=M("KerbB2", "#fdfdfd", roughness=0.5),
            line=M("Line2", "#fffaf0", roughness=0.5),
            check_a=M("CheckA2", "#22252c", roughness=0.5),
            check_b=M("CheckB2", "#fdfdfd", roughness=0.5),
            border=M("Border2", "#8a5a2c", roughness=0.8),
            barrier_a=M("BarA2", "#ff8a4c", roughness=0.55),
            barrier_b=M("BarB2", "#fff3dd", roughness=0.55),
            ramp=M("Ramp2", "#b5793c", roughness=0.8),
            ramp_trim=M("RampTrim2", "#fff4dd", roughness=0.5),
        )
    else:  # winter
        pal.update(
            bark=M("PineBark", "#6a4a33", roughness=0.85),
            leaf=M("PineLeaf", "#2e7d5b", roughness=0.75),
            road=M("SnowRoad", "#a6bed4", roughness=0.74),
            road_ice=M("IceRoad", "#8fc9e8", roughness=0.13, clearcoat=0.95),
            snow_a=M("SnowA", "#dceafa", roughness=0.64),
            snow_b=M("SnowB", "#cfe0f4", roughness=0.64),
            snow_c=M("SnowC", "#f2f8ff", roughness=0.58),
            rock_dark=M("RockDark", "#7c7f8a", roughness=0.9),
            kerb_a=M("KerbA3", "#3b7dd8", roughness=0.5),
            kerb_b=M("KerbB3", "#fdfdfd", roughness=0.5),
            line=M("Line3", "#8fb8dd", roughness=0.5),
            check_a=M("CheckA3", "#22252c", roughness=0.5),
            check_b=M("CheckB3", "#fdfdfd", roughness=0.5),
            border=M("Border3", "#6b8ba8", roughness=0.7),
            barrier_a=M("BarA3", "#3b7dd8", roughness=0.55),
            barrier_b=M("BarB3", "#fdfdfd", roughness=0.55),
            ramp=M("Ramp3", "#7fa8cc", roughness=0.7),
            ramp_trim=M("RampTrim3", "#fdfdfd", roughness=0.5),
        )
    return pal


def theme_config(theme_id, pal, spec):
    """
    How the ground beside the road behaves, and which material each patch of
    terrain gets. The rule is handed the distance to the road, the road's
    half width, the height above the road, some noise, the position, the
    steepness of the ground and its altitude - the last two are what let a
    hillside turn to rock where it is too steep to hold grass or snow.

    `verge` must stay above CarSim.wallMargin (5 m): the flat apron has to
    reach past the invisible wall, or the car would visibly float over the
    hillside when it is pushed off the road.
    """
    if theme_id == "classic":
        mats = [pal["grass_a"], pal["grass_b"], pal["grass_c"], pal["soil"],
                pal["rock"]]

        def rules(d, hw, rel, nv, x, y, slope, alt):
            if d < hw + 4.8 + nv * 1.2:
                return 2                  # light apron next to the track
            if slope > 0.50:
                return 4                  # rock where the hillside is steep
            if alt > 46:
                return 4                  # bare rock on the tops
            if slope > 0.50 and nv > 0.40:
                return 3                  # bare soil on the steeper banks
            if nv > 0.80:
                return 3
            return 0 if nv > -0.16 else 1
        return dict(terrain_rules=rules, _terrain_mats=mats,
                    verge=6.2, falloff=16.0, max_bank=1.20, verge_drop=0.10)
    if theme_id == "beach":
        mats = [pal["sand_a"], pal["sand_b"], pal["sand_wet"], pal["grass_a"],
                pal["rock"]]

        def rules(d, hw, rel, nv, x, y, slope, alt):
            if alt < 0.25:
                return 2                  # wet sand down at the waterline
            if d < hw + 4.4 + nv * 1.2:
                return 0
            if slope > 0.44:
                return 4                  # the rocky headland
            if alt > 6.5 and nv > 0.38:
                return 3                  # marram grass on the dunes
            return 1 if nv > 0.12 else 0
        return dict(terrain_rules=rules, _terrain_mats=mats,
                    verge=6.4, falloff=26.0, max_bank=0.70, verge_drop=0.08)
    mats = [pal["snow_a"], pal["snow_b"], pal["snow_c"], pal["rock_dark"],
            pal["ice"]]

    def rules(d, hw, rel, nv, x, y, slope, alt):
        if d < hw + 4.4 + nv * 1.2:
            return 2
        if slope > 0.50:
            return 3                      # rock faces hold no snow
        if alt > 55:
            return 2                      # bright snow on the tops
        if nv > 0.70:
            return 4                      # ice patches
        return 0 if nv > -0.12 else 1
    return dict(terrain_rules=rules, _terrain_mats=mats,
                verge=6.0, falloff=22.0, max_bank=1.20, verge_drop=0.12)


def road_materials(theme_id, pal):
    if theme_id == "classic":
        return {A: pal["road"], D: pal["road_dirt"]}
    if theme_id == "beach":
        return {SA: pal["road"], W: pal["road_wood"], A: pal["road"]}
    return {SN: pal["road"], ICE: pal["road_ice"], A: pal["road"]}


# ================================================================== PROPS

def scatter_props(cl, tf, theme, land, rnd, pal, spec):
    """Scatters the surroundings according to the theme. Returns a list of objects."""
    tid = spec["theme"]
    n = len(cl)
    objs = []

    def ground(x, y):
        return K.terrain_height(cl, tf, theme, land, x, y)[0]

    def steepness(x, y, e=3.0):
        gx = (ground(x + e, y) - ground(x - e, y)) / (2 * e)
        gy = (ground(x, y + e) - ground(x, y - e)) / (2 * e)
        return math.hypot(gx, gy)

    def along(count, lat_min, lat_max, factory, sides=(1, -1), zoff=0.0,
              min_gap=0.0, max_slope=None, keep_out=0.0):
        """
        Places `count` objects beside the track. `max_slope` rejects ground
        too steep to stand something on - a grandstand or a tree half buried
        in a hillside reads as a bug, a rock does not. `keep_out` is the
        object's own radius, pushed out so it cannot reach into the strip the
        car is allowed to drive on.
        """
        placed = []
        tries = 0
        while len(placed) < count and tries < count * 18:
            tries += 1
            i = rnd.randrange(n)
            side = rnd.choice(sides)
            hw = cl.halfwidth(i)
            if keep_out:
                lat = side * (hw + WALL_CLEAR + keep_out
                              + rnd.uniform(lat_min, lat_max))
            else:
                lat = side * (hw + rnd.uniform(lat_min, lat_max))
            x, y, _ = cl.offset_point(i, lat)
            _, d = tf.nearest(x, y)
            if d < hw + lat_min * 0.7:
                continue
            if min_gap > 0 and any((x - px) ** 2 + (y - py) ** 2 < min_gap ** 2
                                   for px, py in placed):
                continue
            if max_slope is not None and steepness(x, y) > max_slope:
                continue
            ob = factory()
            if ob is None:
                continue
            _place(ob, x, y, ground(x, y) + zoff, rnd.uniform(0, TAU))
            objs.append(ob)
            placed.append((x, y))

    def on_slopes(count, lat_min, lat_max, factory, min_slope=0.30,
                  min_gap=6.0, zoff=0.0, keep_out=0.0):
        """The opposite selection: only where the ground really does fall away."""
        placed = []
        tries = 0
        while len(placed) < count and tries < count * 26:
            tries += 1
            i = rnd.randrange(n)
            side = rnd.choice((1, -1))
            hw = cl.halfwidth(i)
            lat = side * (hw + WALL_CLEAR + keep_out
                          + rnd.uniform(lat_min, lat_max))
            x, y, _ = cl.offset_point(i, lat)
            _, d = tf.nearest(x, y)
            if d < hw + lat_min * 0.7:
                continue
            if steepness(x, y) < min_slope:
                continue
            if any((x - px) ** 2 + (y - py) ** 2 < min_gap ** 2
                   for px, py in placed):
                continue
            ob = factory()
            if ob is None:
                continue
            _place(ob, x, y, ground(x, y) + zoff, rnd.uniform(0, TAU))
            objs.append(ob)
            placed.append((x, y))

    def _place(ob, x, y, z, rot):
        T.rotate_mesh(ob, rot, 'Z')
        T.move(ob, (x, y, z))

    if tid == "classic":
        along(110, 5.0, 60.0, lambda: P.tree_round(
            pal, h=rnd.uniform(4.2, 7.6), rnd=rnd), min_gap=7.0,
            max_slope=0.42)
        along(66, 3.0, 14.0, lambda: P.bush(pal, r=rnd.uniform(0.7, 1.4),
                                            rnd=rnd), min_gap=4.0)
        along(40, 1.4, 3.0, lambda: P.tyre_stack(
            pal, count=rnd.randint(2, 4), r=0.52), min_gap=3.0,
            max_slope=0.30)
        along(28, 1.6, 3.2, lambda: P.hay_bale(pal), min_gap=5.0,
              max_slope=0.30)
        along(48, 1.2, 2.4, lambda: P.cone_marker(pal), min_gap=6.0)
        along(16, 6.0, 12.0, lambda: P.lamp_post(pal, h=rnd.uniform(4.5, 6.0)),
              min_gap=22.0, max_slope=0.34)
        along(26, 5.0, 11.0, lambda: P.flag_pole(pal, h=rnd.uniform(3.4, 4.6)),
              min_gap=12.0, max_slope=0.36)
        along(34, 0.0, 34.0, lambda: P.rock(pal, r=rnd.uniform(0.6, 1.8),
                                            rnd=rnd), min_gap=8.0,
              keep_out=1.8)
        on_slopes(50, 0.0, 46.0, lambda: P.crag(
            pal, r=rnd.uniform(1.6, 3.4), h=rnd.uniform(2.4, 6.0), rnd=rnd),
            min_slope=0.42, min_gap=10.0, zoff=-0.3, keep_out=3.4)
        on_slopes(40, 0.0, 5.0, lambda: P.crag(          # the cutting wall
            pal, r=rnd.uniform(1.2, 2.2), h=rnd.uniform(1.8, 4.0), rnd=rnd),
            min_slope=0.50, min_gap=4.5, zoff=-0.3, keep_out=2.2)
        along(240, 0.8, 10.0, lambda: P.tuft(pal, h=rnd.uniform(0.35, 0.75),
                                             rnd=rnd), min_gap=1.6)
        along(70, 1.2, 8.0, lambda: P.flower(
            pal, h=rnd.uniform(0.28, 0.46), rnd=rnd,
            key=rnd.choice(["banner", "cone", "seat_b"])), min_gap=2.4)
    elif tid == "beach":
        along(92, 5.0, 46.0, lambda: P.palm(pal, h=rnd.uniform(5.0, 8.5),
                                            rnd=rnd), min_gap=7.0,
              max_slope=0.34)
        along(52, 3.0, 16.0, lambda: P.bush(pal, r=rnd.uniform(0.6, 1.2),
                                            rnd=rnd), min_gap=4.0)
        along(34, 4.0, 18.0, lambda: P.umbrella(pal, h=rnd.uniform(2.0, 2.8),
                                                r=rnd.uniform(1.2, 1.8)),
              min_gap=9.0, max_slope=0.22)
        along(36, 1.4, 3.0, lambda: P.tyre_stack(pal, count=rnd.randint(2, 3),
                                                 r=0.5), min_gap=4.0,
              max_slope=0.30)
        along(44, 1.2, 2.6, lambda: P.cone_marker(pal), min_gap=6.0)
        along(44, 0.0, 36.0, lambda: P.rock(pal, r=rnd.uniform(0.5, 1.6),
                                            rnd=rnd), min_gap=6.0,
              keep_out=1.6)
        on_slopes(30, 0.0, 40.0, lambda: P.crag(
            pal, r=rnd.uniform(1.6, 3.0), h=rnd.uniform(2.4, 5.6), rnd=rnd),
            min_slope=0.34, min_gap=12.0, zoff=-0.2, keep_out=3.0)
        along(26, 2.0, 5.0, lambda: P.buoy(pal, h=rnd.uniform(1.1, 1.7)),
              min_gap=8.0)
        along(22, 5.0, 12.0, lambda: P.flag_pole(pal, h=rnd.uniform(3.0, 4.2),
                                                 key="umb_a"), min_gap=12.0,
              max_slope=0.30)
        along(190, 1.0, 11.0, lambda: P.tuft(pal, h=rnd.uniform(0.4, 0.85),
                                             rnd=rnd), min_gap=1.8)
        along(56, 2.0, 12.0, lambda: P.rock(pal, r=rnd.uniform(0.25, 0.5),
                                            rnd=rnd), min_gap=2.2)
    else:
        along(120, 4.5, 58.0, lambda: P.tree_pine(pal, h=rnd.uniform(5.5, 10.0),
                                                  rnd=rnd), min_gap=6.5,
              max_slope=0.46)
        along(46, 1.6, 3.4, lambda: P.snow_bank(pal, w=rnd.uniform(2.6, 4.0),
                                                h=rnd.uniform(0.7, 1.2),
                                                rnd=rnd), min_gap=4.0,
              max_slope=0.34)
        along(44, 0.0, 34.0, lambda: P.rock(pal, r=rnd.uniform(0.8, 2.2),
                                            rnd=rnd, mat_key="rock_dark"),
              min_gap=7.0, keep_out=2.2)
        on_slopes(64, 0.0, 48.0, lambda: P.crag(
            pal, r=rnd.uniform(1.6, 3.4), h=rnd.uniform(3.0, 7.5), rnd=rnd,
            mat_key="rock_dark"), min_slope=0.40, min_gap=11.0, zoff=-0.2,
            keep_out=3.4)
        along(18, 4.0, 10.0, lambda: P.snowman(pal, h=rnd.uniform(1.6, 2.4)),
              min_gap=16.0, max_slope=0.24)
        along(40, 1.2, 2.6, lambda: P.cone_marker(pal), min_gap=6.0)
        along(16, 6.0, 12.0, lambda: P.lamp_post(pal, h=rnd.uniform(4.5, 6.0)),
              min_gap=22.0, max_slope=0.34)
        along(170, 1.0, 11.0, lambda: P.snow_mound(pal, r=rnd.uniform(0.35, 0.8),
                                                   rnd=rnd), min_gap=2.0)
    return objs


def place_landmarks(cl, tf, theme, land, pal, spec, rnd):
    """The large structures - grandstands, the start gate, the ice arch."""
    objs = []

    def ground(x, y):
        return K.terrain_height(cl, tf, theme, land, x, y)[0]

    def steepness(x, y, e=4.0):
        gx = (ground(x + e, y) - ground(x - e, y)) / (2 * e)
        gy = (ground(x, y + e) - ground(x, y - e)) / (2 * e)
        return math.hypot(gx, gy)

    def flat_spot(i, lat, want=0.16):
        """
        Nudges a building along the track and away from it until it stands on
        something reasonably level - on a hillside a fifteen metre grandstand
        would otherwise bury one end.
        """
        best = None
        for step in (0, 8, -8, 16, -16, 26, -26, 38, -38):
            ii = (i + int(step / cl.step)) % len(cl)
            for f in (1.0, 1.2, 0.85, 1.45, 1.7):
                x, y, _ = cl.offset_point(ii, lat * f)
                sl = steepness(x, y)
                if best is None or sl < best[0]:
                    best = (sl, ii, lat * f)
                if sl <= want:
                    return ii, lat * f
        return best[1], best[2]

    def at(i, lat, factory, zoff=0.0, level=True):
        if level:
            i, lat = flat_spot(i, lat)
        x, y, _ = cl.offset_point(i, lat)
        ob = factory()
        tx, ty = cl.tangent(i)
        ang = math.atan2(ty, tx) - math.pi / 2
        if lat < 0:
            ang += math.pi
        T.rotate_mesh(ob, ang, 'Z')
        T.move(ob, (x, y, ground(x, y) + zoff))
        objs.append(ob)
        return ob

    start_i = cl.index_at_s(spec["start_s_frac"] * cl.length)
    tid = spec["theme"]
    gate_span = cl.halfwidth(start_i) * 2 + 6.0
    x, y, z = cl.pos(start_i)
    gate = P.start_gate(pal, span=gate_span, h=6.4,
                        banner="banner" if tid != "winter" else "kerb_a")
    tx, ty = cl.tangent(start_i)
    # the gate must stand across the track, hence +90 degrees
    T.rotate_mesh(gate, math.atan2(ty, tx) + math.pi / 2, 'Z')
    T.move(gate, (x, y, z - 0.1))
    objs.append(gate)

    if tid == "classic":
        for frac, lat in ((0.042, 1), (0.33, -1), (0.60, -1), (0.84, 1)):
            i = cl.index_at_s(frac * cl.length)
            hw = cl.halfwidth(i)
            at(i, lat * (hw + 19.0),
               lambda: P.grandstand(pal, w=rnd.uniform(14, 19),
                                    rows=rnd.randint(4, 5), rnd=rnd))
        i = cl.index_at_s(0.075 * cl.length)
        at(i, cl.halfwidth(i) + 15.0,
           lambda: P.hut(pal, w=11.0, d=6.5, h=3.6, rnd=rnd))
        i = cl.index_at_s(0.46 * cl.length)
        at(i, -(cl.halfwidth(i) + 24.0), lambda: P.water_tower(pal, h=8.5))
        for frac in (0.24, 0.71):
            i = cl.index_at_s(frac * cl.length)
            at(i, cl.halfwidth(i) + 16.0, lambda: P.tower(pal, h=5.6, rnd=rnd))
    elif tid == "beach":
        for frac in (0.05, 0.42, 0.66):
            i = cl.index_at_s(frac * cl.length)
            hw = cl.halfwidth(i)
            at(i, (hw + 18.0),
               lambda: P.grandstand(pal, w=15, rows=4, rnd=rnd))
        for frac in (0.16, 0.52, 0.78, 0.93):
            i = cl.index_at_s(frac * cl.length)
            at(i, -(cl.halfwidth(i) + 13.0),
               lambda: P.tower(pal, h=5.0, rnd=rnd))
        for frac in (0.11, 0.35, 0.60, 0.86):
            i = cl.index_at_s(frac * cl.length)
            at(i, (cl.halfwidth(i) + 12.0),
               lambda: P.hut(pal, w=5.0, d=4.0, h=2.6, roof="banner",
                             rnd=rnd))
    else:
        for frac in (0.30, 0.63):
            i = cl.index_at_s(frac * cl.length)
            x, y, z = cl.pos(i)
            arch = P.ice_arch(pal, span=cl.halfwidth(i) * 2 + 7.0, h=7.5)
            tx, ty = cl.tangent(i)
            T.rotate_mesh(arch, math.atan2(ty, tx) + math.pi / 2, 'Z')
            T.move(arch, (x, y, z))
            objs.append(arch)
        i = cl.index_at_s(0.05 * cl.length)
        at(i, cl.halfwidth(i) + 18.0,
           lambda: P.grandstand(pal, w=15, rows=4, rnd=rnd))
        for frac in (0.15, 0.40, 0.55, 0.75, 0.90):
            i = cl.index_at_s(frac * cl.length)
            at(i, ((-1) ** int(frac * 10)) * (cl.halfwidth(i) + 15.0),
               lambda: P.hut(pal, w=7.0, d=5.5, h=3.0, wall="wood",
                             roof="kerb_a", rnd=rnd))
    return objs


# ================================================================== ASSEMBLY

def pickups_for(cl, spec, rnd):
    """Fuel canisters and boost pads laid out around the track."""
    L = cl.length
    cans = []
    count = spec.get("cans", 14)
    for k in range(count):
        # The jitter is a fraction of the car's *own slot*, not of the lap.
        # As a fraction of the lap it was ±0.03 L, which on Frostpeak is
        # ±57 m against a slot only 68 m wide - so two neighbours could swap
        # ends of their slots and land on top of each other. They did: two
        # canisters 1.4 m apart, close enough to read as one crate drawn
        # twice and to be collected together by a single 2.6 m pickup radius,
        # which is 80 litres from one spot. Kept inside ±0.3 of a slot, two
        # canisters are never closer than 0.4 of one - 27 m on that circuit -
        # and the layout is otherwise unchanged, since this draws the same
        # number of random values as before.
        s = ((k + 0.5 + rnd.uniform(-0.3, 0.3)) / count) * L
        i = cl.index_at_s(s)
        hw = cl.halfwidth(i)
        lat = rnd.uniform(-0.55, 0.55) * hw
        cans.append(dict(s=round(s, 2), lat=round(lat, 2)))
    boosts = []
    bc = spec.get("boosts", 5)
    for k in range(bc):
        s = ((k + 0.25) / bc) * L + rnd.uniform(-8, 8)
        i = cl.index_at_s(s % L)
        if abs(cl.curvature[i]) > 0.02:
            continue
        boosts.append(dict(s=round(s % L, 2),
                           lat=round(rnd.uniform(-0.4, 0.4) * cl.halfwidth(i),
                                     2)))
    return cans, boosts


def build_boost_pads(cl, boosts, pal, name="Boosts"):
    parts = []
    m = T.material("BoostPad", "#2fe0ff", roughness=0.25, emission="#2fe0ff",
                   emission_strength=2.2)
    m2 = T.material("BoostPadBase", "#123244", roughness=0.5)
    for b in boosts:
        i = cl.index_at_s(b["s"])
        w = 3.4
        ln = 7.0
        steps = max(3, int(ln / cl.step))
        verts, faces = [], []
        for k in range(steps + 1):
            j = (i + k) % len(cl)
            c = cl.offset_point(j, b["lat"], 0.075)
            nx, ny = cl.normal(j)
            verts.append((c[0] + nx * w * 0.5, c[1] + ny * w * 0.5, c[2]))
            verts.append((c[0] - nx * w * 0.5, c[1] - ny * w * 0.5, c[2]))
        for k in range(steps):
            a = k * 2
            bb = (k + 1) * 2
            faces.append((a, a + 1, bb + 1, bb))
        parts.append(T.mesh_from_pydata("Pad", verts, faces, m2))
        # arrows
        for k in range(3):
            j = (i + int(k * steps / 3) + 2) % len(cl)
            c = cl.offset_point(j, b["lat"], 0.085)
            nx, ny = cl.normal(j)
            tx, ty = cl.tangent(j)
            av, af = [], []
            tip = (c[0] + tx * 1.5, c[1] + ty * 1.5, c[2])
            l1 = (c[0] + nx * 1.3 - tx * 0.5, c[1] + ny * 1.3 - ty * 0.5, c[2])
            r1 = (c[0] - nx * 1.3 - tx * 0.5, c[1] - ny * 1.3 - ty * 0.5, c[2])
            l2 = (c[0] + nx * 0.55, c[1] + ny * 0.55, c[2])
            r2 = (c[0] - nx * 0.55, c[1] - ny * 0.55, c[2])
            av = [tip, l1, l2, r2, r1]
            af = [(0, 1, 2), (0, 2, 3), (0, 3, 4)]
            parts.append(T.mesh_from_pydata("Arrow", av, af, m))
    if not parts:
        return None
    return T.join(parts, name)


def make_centerline(spec, land):
    """
    Control points carry only the plan view; the height comes from the
    landscape. An optional fifth element lifts a point off the natural ground
    (a causeway, a pier) if a track ever needs it.

    A spec gives its plan view either as `control` - the points themselves -
    or as a `route` of straights and arcs, which is the same thing said in
    the language a circuit is actually designed in (see
    `tcspline.route_control`).
    """
    control = spec.get("control")
    if control is None:
        control = S.route_control(spec["route"],
                                  start=spec.get("route_start", (0.0, 0.0)),
                                  heading=spec.get("route_heading", 0.0))
    cps = []
    for cp in control:
        x, y, hw, surf = cp[0], cp[1], cp[2], cp[3]
        dz = cp[4] if len(cp) > 4 else 0.0
        cps.append((x, y, land.landforms(x, y) + dz, hw, surf))
    cl = S.Centerline(cps, step=0.7)
    cl.smooth_elevation(passes=3, radius=6)
    cl.limit_grade(spec.get("max_grade", 0.16))
    cl.smooth_elevation(passes=2, radius=4)
    cl.smooth_width(passes=2, radius=5)
    cl.clamp_width_to_curvature()
    cl._finish()
    return cl


def build_track(spec, do_preview=False, do_top=False):
    T.reset_scene()
    T.clear_material_cache()
    rnd = random.Random(spec["seed"])
    pal = make_palette(spec["theme"])
    land = LA.LandField(seed=spec["seed"], **spec["land"])
    cl = make_centerline(spec, land)
    tf = K.TrackField(cl)
    theme = theme_config(spec["theme"], pal, spec)

    objs = []
    plank = (W, pal["road_wood2"]) if spec["theme"] == "beach" else None
    road = K.build_road(cl, road_materials(spec["theme"], pal), plank=plank,
                        plank_len=1.05)
    objs.append(road)
    objs.append(K.build_edge_border(cl, pal["border"]))
    objs.append(K.build_edge_lines(cl, pal["line"]))
    objs.append(K.build_kerbs(cl, pal["kerb_a"], pal["kerb_b"]))
    objs.append(K.build_start_line(cl, spec["start_s_frac"] * cl.length, pal))

    ramps = []
    for r in spec.get("ramps", []):
        s0 = r["s_frac"] * cl.length
        ramps.append(dict(s0=s0, s1=s0 + r["length"], height=r["height"]))
    ramp_ob = K.build_ramps(cl, ramps, pal)
    if ramp_ob:
        objs.append(ramp_ob)

    cans, boosts = pickups_for(cl, spec, rnd)
    pads = build_boost_pads(cl, boosts, pal)
    if pads:
        objs.append(pads)

    bar = K.build_barriers(cl, pal)
    if bar:
        objs.append(bar)
    if spec["theme"] == "winter":
        poles = K.build_edge_poles(cl, pal)
        if poles:
            objs.append(poles)

    terrain, floor = K.build_terrain(cl, tf, theme, land, cell=3.4,
                                    margin=56.0)
    objs.append(terrain)
    base_mat = theme["_terrain_mats"][0]
    objs.append(K.build_base_plane(cl, mat=base_mat, floor=floor,
                                   drop=spec.get("base_drop", 1.6)))

    if spec.get("water_level") is not None:
        objs.append(K.build_water(cl, spec["water_level"], mat=pal["water"]))

    props = scatter_props(cl, tf, theme, land, rnd, pal, spec)
    marks = place_landmarks(cl, tf, theme, land, pal, spec, rnd)
    if props:
        objs.append(T.join(props, "Props"))
    if marks:
        objs.append(T.join(marks, "Landmarks"))

    return cl, tf, theme, land, pal, objs, cans, boosts, ramps


def start_grid(cl, spec, count=8):
    """The starting grid - two rows behind the start line."""
    s0 = spec["start_s_frac"] * cl.length
    grid = []
    for k in range(count):
        row = k // 2
        col = -1 if k % 2 == 0 else 1
        s = s0 - 6.0 - row * 6.5
        i = cl.index_at_s(s % cl.length)
        hw = cl.halfwidth(i)
        lat = col * hw * 0.34
        x, y, z = cl.offset_point(i, lat)
        tx, ty = cl.tangent(i)
        grid.append(dict(x=round(x, 3), y=round(z, 3), z=round(-y, 3),
                         yaw=round(math.atan2(tx, ty), 4)))
    return grid


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only = argv[argv.index("--only") + 1] if "--only" in argv else None
    do_top = "--top" in argv
    do_preview = "--preview" in argv

    manifest = []
    for tid, spec in TRACKS.items():
        if only and tid != only:
            continue
        (cl, tf, theme, land, pal, objs, cans, boosts,
         ramps) = build_track(spec)

        line = cl.racing_line()
        meta = dict(
            id=spec["id"], name=spec["name"], subtitle=spec["subtitle"],
            theme=spec["theme"], laps=spec["laps"],
            difficulty=spec["difficulty"], unlock=spec["unlock"],
            reward=spec["reward"],
            startS=round(spec["start_s_frac"] * cl.length, 2),
            grid=start_grid(cl, spec),
            cans=cans, boosts=boosts,
            ramps=[dict(s0=round(r["s0"], 2), s1=round(r["s1"], 2),
                        h=r["height"]) for r in ramps],
            sky=spec["sky"], sky2=spec["sky2"], fog=spec["fog"],
            fogDensity=spec["fogDensity"], sun=spec["sun"],
            sunColor=spec["sunColor"], sunIntensity=spec["sunIntensity"],
            ambient=spec["ambient"], ambientIntensity=spec["ambientIntensity"],
            water=spec.get("water_level"),
        )
        jpath = os.path.join(OUT, "Tracks", "track_%s.json" % tid)
        K.export_track_json(cl, meta, jpath, racing_line=line)
        upath = os.path.join(OUT, "Tracks", "track_%s.usdz" % tid)
        bad = sum(T.check_normals(o, "%s/%s" % (tid, o.name))
                  for o in bpy.context.scene.objects if o.type == 'MESH')
        T.export_usdz(upath)
        tris = T.tri_count()
        if "--parts" in argv:
            for o in sorted(bpy.context.scene.objects,
                            key=lambda o: -len(o.data.polygons)
                            if o.type == 'MESH' else 0):
                if o.type != 'MESH':
                    continue
                n = sum(len(pp.vertices) - 2 for pp in o.data.polygons)
                print("        %-12s %7d tris" % (o.name, n))
        lo, hi, grade, climb = cl.elevation_stats()
        print("TRACK %-7s len=%6.1fm pts=%d tris=%-7d normals=%s -> %s"
              % (tid, cl.length, len(cl), tris,
                 "OK" if bad == 0 else "%d BAD" % bad, upath))
        print("        elevation %.1f..%.1f m (span %.1f), climb/lap %.0f m, "
              "steepest %.0f%%" % (lo, hi, hi - lo, climb, grade * 100))
        manifest.append(dict(id=tid, name=spec["name"],
                             subtitle=spec["subtitle"], theme=spec["theme"],
                             laps=spec["laps"], unlock=spec["unlock"],
                             difficulty=spec["difficulty"],
                             reward=spec["reward"],
                             length=round(cl.length, 1), tris=tris))

        if do_top:
            R.setup_studio(bg=spec["sky"], strength=1.2)
            xs = [cl.pos(i)[0] for i in range(len(cl))]
            ys = [cl.pos(i)[1] for i in range(len(cl))]
            cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
            size = max(max(xs) - min(xs), max(ys) - min(ys)) * 1.25
            R.render(os.path.join(PREVIEW, "track_%s_top.png" % tid),
                     cam_loc=(cx, cy, 460), look_at=(cx, cy, 0),
                     res=(760, 760), ortho=size)
        if do_preview:
            if not do_top:
                R.setup_studio(bg=spec["sky"], strength=1.2)
            # the racing view: roughly where ChaseCamera sits
            i = cl.index_at_s(cl.length * 0.06)
            px, py, pz = cl.pos(i)
            tx, ty = cl.tangent(i)
            R.render(os.path.join(PREVIEW, "track_%s_cam.png" % tid),
                     cam_loc=(px - tx * 30, py - ty * 30, pz + 17),
                     look_at=(px + tx * 34, py + ty * 34, pz + 2),
                     res=(900, 560), lens=26)
            # a scenic view from the highest point of the lap, so the relief
            # and the horizon can actually be judged
            hi_i = max(range(len(cl)), key=lambda k: cl.pos(k)[2])
            px, py, pz = cl.pos(hi_i)
            tx, ty = cl.tangent(hi_i)
            R.render(os.path.join(PREVIEW, "track_%s_view.png" % tid),
                     cam_loc=(px - tx * 110, py - ty * 110, pz + 62),
                     look_at=(px + tx * 90, py + ty * 90, pz - 6),
                     res=(1000, 560), lens=30)
            # and the whole landscape from outside, mountains included
            xs = [cl.pos(k)[0] for k in range(len(cl))]
            ys = [cl.pos(k)[1] for k in range(len(cl))]
            cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
            span = max(max(xs) - min(xs), max(ys) - min(ys))
            R.render(os.path.join(PREVIEW, "track_%s_land.png" % tid),
                     cam_loc=(cx, cy - span * 1.25, span * 0.55),
                     look_at=(cx, cy, 0), res=(1000, 520), lens=34)

    if not only:
        with open(os.path.join(OUT, "tracks_manifest.json"), "w") as f:
            json.dump(manifest, f, indent=1)
    print("DONE tracks")


main()
