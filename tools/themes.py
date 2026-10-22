"""The ten circuits: layout, terrain, palette and dressing for each environment.

Pure Python, so the layout regression and the Blender exporter share it. The
first three themes keep the exact values they were tuned with; the later ones
describe their scenery by style names that tools/biomes.py builds.
"""
import math
from layouts import knots as layout_knots
import layouts


def smoothstep(a, b, x):
    t = max(0., min(1., (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def spline_point(knots, t):
    q = t * len(knots); i = int(q) % len(knots); u = q - int(q)
    p = [knots[(i + j) % len(knots)] for j in [-1, 0, 1, 2]]
    return [.5 * ((2 * p[1][a]) + (-p[0][a] + p[2][a]) * u + (2 * p[0][a] - 5 * p[1][a] + 4 * p[2][a] - p[3][a]) * u * u
                  + (-p[0][a] + 3 * p[1][a] - 3 * p[2][a] + p[3][a]) * u * u * u) for a in [0, 1]]


class RoadField:
    """Horizontal distance to the centreline on a 2 m grid, for road-aware landforms."""
    CELL = 2.

    def __init__(self, knots, extent):
        samples = [spline_point(knots, i / (len(knots) * 24)) for i in range(len(knots) * 24)]
        self.extent = extent
        n = int(2 * extent / self.CELL) + 2
        self.n = n
        buckets = {}
        for p in samples:
            buckets.setdefault((int((p[0] + extent) // 16), int((p[1] + extent) // 16)), []).append(p)
        self.grid = []
        for j in range(n):
            z = -extent + j * self.CELL
            row = []
            for i in range(n):
                x = -extent + i * self.CELL
                bx, bz = int((x + extent) // 16), int((z + extent) // 16)
                best = 1e9
                for r in range(0, 8):
                    for dx in range(-r, r + 1):
                        for dz in range(-r, r + 1):
                            if max(abs(dx), abs(dz)) != r:
                                continue
                            for p in buckets.get((bx + dx, bz + dz), ()):
                                d = (p[0] - x) ** 2 + (p[1] - z) ** 2
                                if d < best:
                                    best = d
                    if best < ((r) * 16.) ** 2:
                        break
                row.append(min(math.sqrt(best), 128.))
            self.grid.append(row)

    def __call__(self, x, z):
        fx = (x + self.extent) / self.CELL; fz = (z + self.extent) / self.CELL
        i = max(0, min(self.n - 2, int(fx))); j = max(0, min(self.n - 2, int(fz)))
        u = max(0., min(1., fx - i)); v = max(0., min(1., fz - j))
        g = self.grid
        return (g[j][i] * (1 - u) * (1 - v) + g[j][i + 1] * u * (1 - v) +
                g[j + 1][i] * (1 - u) * v + g[j + 1][i + 1] * u * v)


class Theme:
    """Defaults describe a new-style circuit; legacy circuits override them."""
    legacy = False
    stations = None          # Path stations; new circuits space them about 1.6 m apart
    shape = 2.               # Island outline: |x/rx|^p + |z/rz|^p = 1
    bank = .55               # Crossfall per unit of heading change, and its limit
    bank_limit = .33
    smooth = 9.              # Vertical smoothing of the road profile (m), new circuits only
    max_grade = .11
    ramp_height = 1.45
    ramp_count = 3
    can_count = 6
    crowd_groups = 15
    benches = 5
    trees = 105
    patches = 115
    boulders = 40
    sea = False
    lava = False             # Liquid above the sea is lava (src/tracks.c has the same flag)
    four_lane = (.055, .09, .205, .24)
    passing = 65.            # Shortest full-width four-lane stretch, metres
    can_spacing = .6         # Minimum gap between pickups, as a share of the even spacing

    def __init__(self, **kw):
        self.__dict__.update(kw)
        self._field = None

    def rail_station(self, path):
        """Path index of the level crossing."""
        site = self.rail
        if 'index' in site:
            return site['index']
        return int(round(site['fraction'] * len(path))) % len(path)

    def road_distance(self, x, z):
        if self._field is None:
            self._field = RoadField(self.knots, max(self.radius) + 8)
        return self._field(x, z)

    def island(self, x, z):
        """1 on the outline of the island, less inside it."""
        rx, rz = self.radius
        p = self.shape
        return (abs(x / rx) ** p + abs(z / rz) ** p) ** (1 / p)

    def within(self, x, z, a, b):
        """Inside an ellipse authored for the 128 x 126 m legacy island, scaled to this one."""
        if self.legacy:
            return (x / a) ** 2 + (z / b) ** 2 < 1
        return self.island(x, z) < (a / 128 + b / 126) * .5

    def sx(self, v):
        return v if self.legacy else v * self.radius[0] / 128

    def sz(self, v):
        return v if self.legacy else v * self.radius[1] / 126

    def edge_radius(self, angle):
        """Distance from the centre to the island outline in a direction."""
        rx, rz = self.radius
        p = self.shape
        c, s = abs(math.cos(angle)), abs(math.sin(angle))
        return 1 / ((c / rx) ** p + (s / rz) ** p) ** (1 / p)


# --- Legacy circuits (values as tuned in earlier releases) -------------------------
COUNTRY_KNOTS = [
    (-62, -81), (-18, -81), (43, -76), (80, -50), (88, -5),
    (77, 45), (46, 78), (1, 84), (-43, 67), (-78, 39),
    (-83, 2), (-65, -22), (-29, -10), (10, 19), (44, 39),
    (61, 17), (47, -10), (12, -24), (-22, -38), (-43, -15),
    (-31, 17), (-1, 37), (19, 13), (12, -12), (-18, -50),
    (-60, -42), (-80, -63),
]
BEACH_KNOTS = [
    (100.8, 0), (87.02, 38.75), (53.98, 59.92), (19.38, 59.58), (-5.04, 47.82),
    (-21.28, 36.85), (-38.98, 28.34), (-61.26, 12.99), (-78.96, -16.8), (-77.06, -56.0),
    (-50.4, -87.25), (-9.97, -94.75), (24.98, -76.72), (41.89, -46.59), (44.02, -19.6),
    (42.56, 0), (44.02, 19.6), (41.89, 46.59), (24.98, 76.72), (-9.97, 94.75),
    (-50.4, 87.25), (-77.06, 56.0), (-78.96, 16.8), (-61.26, -12.99), (-38.98, -28.34),
    (-21.28, -36.85), (-5.04, -47.82), (19.38, -59.58), (53.98, -59.92), (87.02, -38.75),
]
WINTER_KNOTS = [
    (-80, -80), (-40, -80), (30, -75), (80, -65), (82, -35),
    (50, -20), (-45, -20), (-75, 0), (-62, 27), (-20, 30),
    (55, 30), (80, 50), (65, 78), (20, 84), (-35, 65),
    (-55, 35), (-35, 0), (-10, -30), (-15, -57), (-48, -60),
    (-78, -45), (-95, -58),
]


def legacy(index, key, title, knots, half, four_lane, terrain, colors, **kw):
    return Theme(index=index, key=key, title=title, legacy=True, knots=knots, radius=(128, 126),
                 stations=640, half=half, four_lane=four_lane, terrain=terrain, ground=index + 1,
                 colors=colors, smooth=0., ramps=[(99, 1.40 if index != 2 else 1.65),
                                                  (296, 1.40 if index != 2 else 1.65),
                                                  (486, 1.40 if index != 2 else 1.65)],
                 cans=[40, 130, 240, 360, 455, 580], signs=[45, 150, 240, 345, 410, 555],
                 crowd=[8, 58, 95, 116, 170, 218, 282, 309, 360, 405, 470, 503, 550, 601, 628],
                 broadcast=[12, 70, 205, 355, 500], **kw)


COUNTRY = legacy(
    0, 'country', 'Harvest Hills', COUNTRY_KNOTS, 5.2, (.055, .09, .205, .24),
    lambda x, z: 4.0 + 4.4 * math.sin(x * .025 + .8) * math.cos(z * .029) + 2.1 * math.sin(z * .055 + x * .02),
    dict(grass_name='Meadow', grass=(.48, .64, .30), road=(.28, .31, .29), curb=(.80, .31, .18),
         bed=(.43, .44, .33), edge=(.51, .37, .21), foundation=(.41, .44, .31), steel=(.52, .20, .12),
         boulder=(.59, .60, .46), pebble=(.40, .43, .34), spring=(.43, .46, .35), path=(.56, .51, .37),
         foliage=(.29, .43, .16), dry=(.64, .53, .27), flower=(.77, .69, .86), stone=(.47, .49, .37)),
    edge_depth=-7, outfit='meadow', rail=dict(index=192, angle=125, before=38, after=34, kind='steam',
    limit=.9, ballast=(.45, .43, .40), sleeper=(.36, .25, .17), stone=(.66, .58, .48), trim=(.52, .42, .33)),
    water=dict(source=(-10., 10.), lake=True, bends=(0., 1.8, -3.8, 4.6, 1.3, -3.2, 3.6, -1.4, 0.), style='meadow',
               speed=.85, title='Watershed — spring to basin', destination='sheltered pond with sediment shelves',
               character='meadow meanders'))
BEACH = legacy(
    1, 'beach', 'Sunshine Coast', BEACH_KNOTS, 5.2, (.06, .095, .25, .285),
    lambda x, z: 3.5 + 3.4 * math.sin(x * .027) * math.cos(z * .031) + 1.8 * math.sin(z * .038 + .6),
    dict(grass_name='Golden sand', grass=(.83, .73, .49), road=(.53, .44, .31), curb=(.89, .43, .18),
         bed=(.63, .56, .39), edge=(.72, .59, .36), foundation=(.68, .57, .36), steel=(.16, .45, .49),
         boulder=(.68, .57, .38), pebble=(.57, .52, .38), spring=(.57, .53, .40), path=(.72, .65, .46),
         foliage=(.43, .50, .25), dry=(.70, .63, .39), flower=(.91, .78, .48), stone=(.64, .57, .43)),
    edge_depth=-9, sea=True, outfit='beach', rail=dict(index=555, angle=90, before=23, after=23, kind='road'),
    water=dict(source=(18., 47.), bends=(0., -2.5, 5.8, 1.5, -6.2, -3.5, 3.8, 0.), style='sand', speed=1.,
               title='Watershed — downhill outlet', destination='tidal estuary', character='sand-bar creek'))
WINTER = legacy(
    2, 'winter', 'Alpine Rush', WINTER_KNOTS, 5.5, (.395, .425, .45, .475),
    lambda x, z: 8 + 7.2 * math.sin(x * .023 + .5) * math.cos(z * .029) + 3.3 * math.cos(z * .041 + x * .014),
    dict(grass_name='Powder snow', grass=(.76, .87, .87), road=(.37, .48, .54), curb=(.21, .50, .64),
         bed=(.43, .53, .55), edge=(.44, .62, .67), foundation=(.66, .78, .80), steel=(.27, .37, .49),
         boulder=(.59, .72, .78), pebble=(.46, .56, .59), spring=(.89, .95, .94), path=(.73, .80, .80),
         foliage=(.17, .30, .25), dry=(.42, .35, .27), flower=(.76, .84, .82), stone=(.46, .54, .57)),
    edge_depth=-7, outfit='winter', rail=dict(index=48, angle=125, before=30, after=38, kind='alpine',
    ballast=(.66, .70, .73), sleeper=(.42, .36, .31), stone=(.55, .58, .62), trim=(.43, .47, .52), cap=True),
    water=dict(source=(35., -35.), bends=(0., .8, -2.1, .4, 2.5, -1.2, -.6, 2., -.9, 0.), style='rock', speed=1.2,
               title='Watershed — downhill outlet', destination='rocky meltwater outlet',
               character='confined alpine channel'))
WINTER.passing = 44.  # The switchbacks' compact passing straight


# --- Later circuits ---------------------------------------------------------------------
def canyon_terrain(T):
    def height(x, z):
        floor = 2.6 + 1.5 * math.sin(x * .029 + .4) * math.cos(z * .025) + .8 * math.sin(z * .052 + x * .017)
        d = T.road_distance(x, z)
        # Mesas and buttes rise wherever the canyon floor opens up; their
        # plateau heights vary, and the cliffs step back in sandstone terraces.
        plateau = 9 + 4.5 * (math.sin(x * .043 + 1.3) * math.cos(z * .037 - .4) + 1)
        rise = smoothstep(23, 31, d)
        cliff = plateau * rise
        step = 2.6
        terraced = math.floor(cliff / step) * step + step * smoothstep(.55, 1, (cliff / step) % 1)
        return floor + terraced * .85 + cliff * .15
    return height


CANYON = Theme(
    index=3, key='canyon', title='Canyon Dash', knots=layout_knots(layouts.CANYON),
    radius=(161, 140), shape=2.2, half=5.0, ground=4,
    colors=dict(grass_name='Red sand', grass=(.80, .50, .31), road=(.40, .33, .29), curb=(.76, .22, .12),
                bed=(.62, .45, .30), edge=(.58, .30, .18), foundation=(.66, .42, .27), steel=(.46, .26, .18),
                boulder=(.66, .38, .24), pebble=(.55, .39, .27), spring=(.52, .38, .26), path=(.74, .55, .38),
                foliage=(.45, .48, .27), dry=(.70, .55, .33), flower=(.93, .55, .30), stone=(.66, .36, .22),
                plumage=(.20, .17, .15)),
    edge_depth=-9, trees=95, boulders=70, patches=150, ramp_count=3, can_count=6, crowd_groups=17, benches=6,
    tree='desert', sway=.3, scenery='canyon', banks='canyon', patch='desert', outfit='desert',
    wildlife=['coyote'] * 4 + ['jackrabbit'] * 6 + ['lizard'] * 6 + ['vulture'] * 8,
    rail=dict(fraction=.62, angle=110, before=30, after=34, kind='steam', ballast=(.55, .40, .30),
              sleeper=(.36, .25, .17), stone=(.70, .42, .28), trim=(.56, .32, .22)),
    water=dict(source=(-58., -82.), bends=(0., 1.5, -3., 3.5, -2., 2.5, -1., 0.), style='sand', speed=.8,
               title='Watershed — desert wash', destination='wash beyond the mesas',
               character='sandy wash with a ford'))
CANYON.terrain = canyon_terrain(CANYON)

def autumn_terrain(T):
    def height(x, z):
        y = 5 + 4.2 * math.sin(x * .021 + 1.0) * math.cos(z * .024 - .3) + 2.4 * math.sin(z * .047 + x * .018)
        # Wooded ridges rise between the roads; valleys keep the creek.
        d = T.road_distance(x, z)
        return y + 4.5 * smoothstep(22, 48, d) * (.6 + .4 * math.sin(x * .05 + z * .03))
    return height


AUTUMN = Theme(
    index=4, key='autumn', passing=40., title='Maple Woods', knots=layout_knots(layouts.AUTUMN),
    radius=(176, 153), shape=2.6, half=4.9, ground=5,
    colors=dict(grass_name='Forest floor', grass=(.47, .45, .23), road=(.26, .27, .27), curb=(.78, .30, .10),
                bed=(.40, .38, .28), edge=(.44, .32, .20), foundation=(.40, .37, .25), steel=(.30, .32, .22),
                boulder=(.46, .48, .40), pebble=(.42, .42, .36), spring=(.44, .45, .38), path=(.55, .45, .32),
                foliage=(.36, .42, .18), dry=(.62, .42, .18), flower=(.85, .55, .20), stone=(.48, .47, .40),
                plumage=(.08, .08, .09)),
    edge_depth=-8, trees=330, boulders=45, patches=170, ramp_count=4, can_count=6, crowd_groups=18, benches=6,
    tree='autumn', sway=.55, scenery='autumn', banks='autumn', patch='autumn', outfit='winter',
    wildlife=['deer'] * 5 + ['fox'] * 4 + ['squirrel'] * 6 + ['duck'] * 5 + ['crow'] * 8,
    rail=dict(fraction=.47, angle=115, before=30, after=34, kind='alpine', ballast=(.47, .44, .40),
              sleeper=(.34, .24, .16), stone=(.55, .52, .46), trim=(.42, .38, .33)),
    water=dict(source=(110., 105.), lake=True, bends=(0., 2., -3.5, 3., -2.5, 3.5, -1.5, 0.), style='meadow',
               speed=.9, title='Watershed — forest creek', destination='woodland pond',
               character='leafy meanders'))
AUTUMN.terrain = autumn_terrain(AUTUMN)

def jungle_terrain(T):
    def height(x, z):
        y = 4 + 3.6 * math.sin(x * .024) * math.cos(z * .021 + .5) + 2.2 * math.cos(z * .05 + x * .02)
        d = T.road_distance(x, z)
        y += 3.5 * smoothstep(22, 46, d) * (.5 + .5 * math.sin(x * .07 - z * .04))
        # The temple stands on a levelled plaza inside the ring road.
        plaza = smoothstep(46, 30, math.hypot(x, z - 4))
        return y + (6.5 - y) * plaza
    return height


JUNGLE = Theme(
    index=5, key='jungle', title='Jungle Ruins', knots=layout_knots(layouts.JUNGLE),
    radius=(172, 156), shape=2.4, half=4.8, ground=6,
    colors=dict(grass_name='Jungle floor', grass=(.30, .42, .18), road=(.45, .33, .24), curb=(.30, .52, .22),
                bed=(.36, .32, .22), edge=(.38, .28, .18), foundation=(.42, .33, .22), steel=(.28, .40, .22),
                boulder=(.42, .46, .36), pebble=(.40, .38, .30), spring=(.40, .42, .32), path=(.52, .44, .32),
                foliage=(.22, .44, .16), dry=(.45, .40, .22), flower=(.90, .30, .20), stone=(.50, .52, .42),
                plumage=(.20, .40, .70)),
    edge_depth=-8, trees=320, boulders=30, patches=190, ramp_count=4, can_count=6, crowd_groups=19, benches=6,
    tree='jungle', sway=.6, scenery='jungle', banks='jungle', patch='jungle', outfit='beach',
    wildlife=['parrot'] * 8 + ['toucan'] * 4 + ['frog'] * 6 + ['tapir'] * 3 + ['emerald snake'] * 4,
    rail=dict(fraction=.45, angle=110, before=26, after=28, kind='steam', window=.2, ballast=(.42, .38, .32),
              sleeper=(.30, .22, .14), stone=(.56, .55, .46), trim=(.38, .44, .30)),
    water=dict(source=(113., 46.), bends=(0., 2.5, -4., 4.5, -3., 4., -2., 0.), style='river', speed=1.,
               title='Watershed — jungle river', destination='river mouth beyond the trees',
               character='broad jungle river'))
JUNGLE.terrain = jungle_terrain(JUNGLE)

def sakura_terrain(T):
    def height(x, z):
        y = 3 + 2.2 * math.sin(x * .02 + .3) * math.cos(z * .026) + 1.2 * math.sin(z * .05 - x * .015)
        # Temple hill under the comb of hairpins, a pagoda hill in the east.
        y += 15 * math.exp(-((x + 105) ** 2 + (z + 15) ** 2) / 55 ** 2)
        y += 9 * math.exp(-((x - 130) ** 2 + (z - 55) ** 2) / 40 ** 2)
        d = T.road_distance(x, z)
        return y + 2.5 * smoothstep(24, 50, d)
    return height


SAKURA = Theme(
    index=6, key='sakura', passing=40., title='Sakura Hills', knots=layout_knots(layouts.SAKURA),
    radius=(208, 164), shape=3.0, half=4.8, ground=7,
    colors=dict(grass_name='Spring lawn', grass=(.50, .66, .32), road=(.30, .31, .33), curb=(.82, .20, .14),
                bed=(.40, .44, .36), edge=(.46, .36, .26), foundation=(.42, .46, .34), steel=(.66, .16, .10),
                boulder=(.52, .53, .50), pebble=(.48, .48, .46), spring=(.50, .52, .48), path=(.76, .72, .62),
                foliage=(.32, .50, .22), dry=(.60, .55, .38), flower=(.55, .35, .80), stone=(.50, .51, .48),
                plumage=(.52, .62, .22)),
    edge_depth=-8, trees=260, boulders=35, patches=170, ramp_count=4, can_count=6, crowd_groups=20, benches=7,
    tree='sakura', sway=.5, scenery='sakura', banks='sakura', patch='sakura', outfit='meadow',
    wildlife=['crane'] * 6 + ['sika deer'] * 5 + ['white-eye'] * 10,
    rail=dict(fraction=.3, angle=110, before=28, after=30, kind='alpine', window=.2, ballast=(.52, .52, .50),
              sleeper=(.34, .26, .20), stone=(.60, .60, .58), trim=(.70, .18, .12)),
    water=dict(source=(-60., 60.), lake=True, bends=(0., 2., -3., 3.5, -2., 2.5, -1., 0.), style='meadow',
               speed=.8, title='Watershed — garden stream', destination='koi pond',
               character='garden stream'))
SAKURA.terrain = sakura_terrain(SAKURA)

def harbor_terrain(T):
    def height(x, z):
        # Level quays and streets, a little higher inland and in a park.
        y = 2.2 + .5 * math.sin(x * .02) * math.cos(z * .025) + 1.6 * smoothstep(-40, 120, z)
        d = T.road_distance(x, z)
        return y + .8 * smoothstep(30, 60, d) * math.sin(x * .03 + z * .02)
    return height


def harbor_tint(x, y, z):
    # Paving everywhere, with lawns in the parks between the blocks.
    park = math.sin(x * .021 + 1.3) * math.cos(z * .018 - .4)
    return (.30, .44, .22) if park > .72 else (.55, .55, .53)


HARBOR = Theme(
    index=7, key='harbor', title='Harbor Lights', knots=layout_knots(layouts.HARBOR),
    radius=(186, 164), shape=3.4, half=4.6, ground=8,
    colors=dict(grass_name='City paving', grass=(.55, .55, .53), road=(.21, .22, .24), curb=(.86, .72, .14),
                bed=(.36, .38, .38), edge=(.40, .40, .40), foundation=(.50, .50, .48), steel=(.24, .30, .38),
                boulder=(.50, .50, .50), pebble=(.42, .42, .42), spring=(.46, .46, .46), path=(.62, .60, .56),
                foliage=(.28, .44, .20), dry=(.50, .46, .36), flower=(.86, .40, .30), stone=(.48, .48, .48),
                plumage=(.92, .93, .92)),
    edge_depth=-9, sea=True, trees=120, boulders=0, patches=150, ramp_count=4, can_count=6, crowd_groups=21, benches=8,
    tree='harbor', sway=.4, scenery='harbor', banks='harbor', patch='harbor', outfit='meadow', tint=harbor_tint,
    wildlife=['gull'] * 12 + ['pigeon'] * 8 + ['cat'] * 3,
    rail=dict(fraction=.86, angle=100, before=28, after=30, kind='freight', window=.25, ballast=(.42, .42, .42),
              sleeper=(.32, .28, .24), stone=(.50, .48, .46), trim=(.36, .34, .34)),
    water=dict(source=(122., 9.), bends=(0., 2., -3.2, 3.6, -2.6, 2.8, -1.5, 0.), style='rock', speed=1.,
               title='Watershed — park creek', destination='harbour basin', character='stony park creek'))
HARBOR.terrain = harbor_terrain(HARBOR)

def highland_terrain(T):
    def height(x, z):
        y = 10 + 9 * math.sin(x * .014 + .7) * math.cos(z * .016) + 5 * math.sin(z * .03 + x * .011)
        # The castle hill inside the keyhole, and the loch basin in the west lobe.
        y += 14 * math.exp(-(x ** 2 + (z - 112) ** 2) / 42 ** 2)
        y -= 7 * math.exp(-((x + 138) ** 2 + (z - 40) ** 2) / 26 ** 2)
        d = T.road_distance(x, z)
        return y + 4 * smoothstep(24, 55, d) * (.6 + .4 * math.cos(x * .04 - z * .03))
    return height


HIGHLAND = Theme(
    index=8, key='highland', title='Highland Loch', knots=layout_knots(layouts.HIGHLAND),
    radius=(218, 210), shape=3.0, half=4.6, ground=9,
    colors=dict(grass_name='Moorland', grass=(.44, .50, .30), road=(.33, .34, .33), curb=(.30, .32, .52),
                bed=(.36, .34, .28), edge=(.38, .32, .24), foundation=(.40, .42, .32), steel=(.20, .26, .40),
                boulder=(.52, .52, .48), pebble=(.42, .42, .38), spring=(.46, .46, .42), path=(.58, .52, .40),
                foliage=(.32, .40, .20), dry=(.58, .48, .28), flower=(.62, .40, .64), stone=(.50, .50, .46),
                plumage=(.30, .20, .12)),
    edge_depth=-9, trees=150, boulders=70, patches=210, ramp_count=5, can_count=6, crowd_groups=22, benches=8,
    walls=14, tree='highland', sway=.45, scenery='highland', banks='highland', patch='highland', outfit='winter',
    wildlife=['sheep'] * 10 + ['highland cow'] * 4 + ['red deer'] * 4 + ['mountain hare'] * 4 + ['eagle'] * 5,
    rail=dict(fraction=.35, angle=110, before=30, after=34, kind='steam', window=.25, ballast=(.46, .44, .42),
              sleeper=(.32, .24, .16), stone=(.50, .50, .50), trim=(.38, .38, .40)),
    water=dict(source=(-120., 115.), lake=True, bends=(0., 1.5, -2.5, 2., -1.5, 0.), style='meadow', speed=.9,
               title='Watershed — burn to the loch', destination='loch in the glen', character='moorland burn'))
HIGHLAND.terrain = highland_terrain(HIGHLAND)


def volcano_terrain(T):
    def height(x, z):
        r = math.hypot(x, z)
        # The cone rises from black-sand beaches; a flat crater holds the lava lake.
        cone = 46 * (1 - smoothstep(30, 215, r)) ** 1.35
        crater = 40.5 - 12 * smoothstep(42, 26, r)
        y = min(cone + 2, crater) if r < 42 else cone + 2
        y += 1.6 * math.sin(x * .05 + z * .03) * math.cos(z * .04 - x * .02) * smoothstep(40, 80, r)
        d = T.road_distance(x, z)
        return y + 2.5 * smoothstep(24, 50, d) * smoothstep(50, 90, r)
    return height


def volcano_tint(x, y, z):
    # Black sand by the sea, dark lava fields, ash grey up the cone.
    if y < 1.5:
        return (.20, .19, .19)
    ash = smoothstep(14, 32, y)
    return tuple(a + (b - a) * ash for a, b in zip((.20, .18, .17), (.40, .38, .36)))


VOLCANO = Theme(
    index=9, key='volcano', title='Volcano Peak', knots=layout_knots(layouts.VOLCANO),
    radius=(225, 227), shape=2.2, half=4.5, ground=10,
    colors=dict(grass_name='Volcanic ash', grass=(.24, .22, .21), road=(.27, .26, .26), curb=(.90, .30, .08),
                bed=(.20, .12, .10), edge=(.16, .14, .14), foundation=(.26, .24, .23), steel=(.40, .14, .08),
                boulder=(.17, .16, .16), pebble=(.20, .18, .18), spring=(.24, .12, .10), path=(.32, .30, .29),
                foliage=(.26, .38, .22), dry=(.36, .30, .24), flower=(.88, .20, .10), stone=(.16, .15, .15),
                plumage=(.06, .06, .07)),
    edge_depth=-9, sea=True, trees=110, boulders=90, patches=200, ramp_count=5, can_count=6, crowd_groups=24, benches=8,
    tree='volcano', sway=.35, scenery='volcano', banks='volcano', patch='volcano', outfit='meadow', tint=volcano_tint,
    wildlife=['raven'] * 8 + ['goat'] * 6 + ['black lizard'] * 5,
    rail=dict(fraction=.12, angle=110, before=30, after=34, kind='steam', window=.3, ballast=(.24, .22, .21),
              sleeper=(.20, .16, .12), stone=(.20, .19, .19), trim=(.36, .16, .10)),
    lava=True,
    water=dict(source=(0., 30.), lake=True, bends=(0., 0.), style='rock', speed=.5,
               title='Lava — vent to the crater lake', destination='lava lake in the crater',
               character='molten rock'))
VOLCANO.terrain = volcano_terrain(VOLCANO)

THEMES = [COUNTRY, BEACH, WINTER, CANYON, AUTUMN, JUNGLE, SAKURA, HARBOR, HIGHLAND, VOLCANO]
NAMES = tuple(t.key for t in THEMES)
