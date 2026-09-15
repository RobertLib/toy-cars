"""Terrain-led water authoring, independent of Blender and the runtime renderer.

Valleys follow the negative terrain gradient; erosion bends the channel within
that valley without changing its downhill profile. Inland standing water is the
connected flooded component of a sculpted depression, at one constant elevation.
Nothing is mirrored around a road or positioned by a track point number.
"""
import math
from collections import deque


class Watershed:
    SOURCES = ((-10., 10.), (18., 47.), (35., -35.))
    SEA_LEVEL = -6.85

    @staticmethod
    def smooth_curve(values, t):
        """Nonperiodic, smoothly joined erosion controls along a valley."""
        p = max(0., min(len(values)-1.000001, t*(len(values)-1)))
        i = int(p)
        f = p-i
        # Catmull-Rom has continuous tangents through alternating outer banks.
        a,b,c,d = (values[max(0,min(len(values)-1,j))] for j in (i-1,i,i+1,i+2))
        return .5*((2*b)+(-a+c)*f+(2*a-5*b+4*c-d)*f*f+(-a+3*b-3*c+d)*f*f*f)

    def __init__(self, theme, terrain):
        self.theme = theme
        self.terrain = terrain
        self.source = self.SOURCES[theme]
        self.lake_level = None
        self.lake_cells = set()
        self.lake_bounds = None
        self.route = []
        x, z = self.source
        for _ in range(400):
            height = self.ground(x, z)
            self.route.append((x, height - .08, z))
            if math.hypot(x / 128, z / 126) >= 1.015:
                break
            dx = (self.ground(x + .15, z) - self.ground(x - .15, z)) / .3
            dz = (self.ground(x, z + .15) - self.ground(x, z - .15)) / .3
            slope = math.hypot(dx, dz)
            if slope < .003:
                break
            step = min(1.25, slope * 70)
            nx, nz = x - dx / slope * step, z - dz / slope * step
            if self.ground(nx, nz) >= height:
                break
            x, z = nx, nz
        if theme == 0:
            # A closed lowland depression. Flood only its connected component,
            # not every unrelated patch at the same altitude near the map edge.
            x, floor, z = self.route[-1]
            self.lake_level = floor + .34
            queue = deque([(round(x), round(z))])
            while queue:
                cell = queue.popleft()
                if cell in self.lake_cells:
                    continue
                a, b = cell
                if self.ground(a, b) >= self.lake_level:
                    continue
                if math.hypot(a / 128, b / 126) >= .985:
                    raise ValueError('The country pond must be enclosed by its basin')
                self.lake_cells.add(cell)
                queue.extend(((a-1, b), (a+1, b), (a, b-1), (a, b+1)))
            self.lake_bounds = (min(a for a, b in self.lake_cells),
                                max(a for a, b in self.lake_cells),
                                min(b for a, b in self.lake_cells),
                                max(b for a, b in self.lake_cells))
            # The stream joins the pond at its upstream shore, then stays level.
            self.route = [(a, max(y, self.lake_level), b) for a, y, b in self.route]
            first_lake = next(i for i, p in enumerate(self.route) if p[1] == self.lake_level)
            self.route = self.route[:first_lake + 2]
        # Small-scale erosion is absent from the broad terrain height function.
        # Give each valley its own bends, then excavate the actual bed under the
        # resulting water profile in build_assets. No upward water or floating
        # decorative curves: the road and both banks use this same channel.
        bends = ((0.,1.8,-3.8,4.6,1.3,-3.2,3.6,-1.4,0.),
                 (0.,-2.5,5.8,1.5,-6.2,-3.5,3.8,0.),
                 (0.,.8,-2.1,.4,2.5,-1.2,-.6,2.,-.9,0.))[theme]
        valley = self.route
        self.route = []
        for i,p in enumerate(valley):
            a,b = valley[max(0,i-1)],valley[min(len(valley)-1,i+1)]
            dx,dz = b[0]-a[0],b[2]-a[2]
            length = math.hypot(dx,dz)
            offset = self.smooth_curve(bends,i/(len(valley)-1))
            self.route.append((p[0]-dz/length*offset,p[1],p[2]+dx/length*offset))
        self.segments = []
        distance = 0.
        for a, b in zip(self.route, self.route[1:]):
            dx, dz = b[0]-a[0], b[2]-a[2]
            length = math.hypot(dx, dz)
            # Spring heads are narrow; channels widen gradually downstream.
            fraction = distance / max(1.,len(self.route)*1.25)
            if theme == 0:
                width = .72 + min(.35,distance*.005)
                width *= self.smooth_curve((.8,1.25,.75,1.1,.72,1.3,.85,1.),fraction)
            elif theme == 1:
                # Sand bars pinch the upper creek; the tidal mouth broadens.
                width = .85 + 1.6*min(1.,fraction)**2
                width *= self.smooth_curve((1.,.75,1.25,.85,1.1,.8,1.3),fraction)
            else:
                # Rock constrictions alternate with small scour pools.
                width = .68*self.smooth_curve((.8,1.3,.75,1.7,.8,1.1,1.65,.9,1.2),fraction)
            width *= min(1., .18 + distance / 7.)
            speed = [.85, 1., 1.2][theme]
            self.segments.append((a, b, dx, dz, length, width,
                                  (dx / length * speed, dz / length * speed)))
            distance += length
        self.length = distance
        # Sparse authoring grid keeps terrain and water queries local.
        self.cells = {}
        for segment in self.segments:
            a, b, _, _, _, width, _ = segment
            for iz in range(math.floor((min(a[2],b[2])-width-10)/8),
                            math.floor((max(a[2],b[2])+width+10)/8)+1):
                for ix in range(math.floor((min(a[0],b[0])-width-10)/8),
                                math.floor((max(a[0],b[0])+width+10)/8)+1):
                    self.cells.setdefault((ix,iz),[]).append(segment)
        margin = max(s[5] for s in self.segments)+2
        self.bounds = (min(p[0] for p in self.route)-margin, max(p[0] for p in self.route)+margin,
                       min(p[2] for p in self.route)-margin, max(p[2] for p in self.route)+margin)
        if self.lake_bounds:
            self.bounds = tuple(f(a,b) for a,b,f in zip(self.bounds,self.lake_bounds,
                                (min,max,min,max)))
            self.bounds = (self.bounds[0]-3,self.bounds[1]+3,self.bounds[2]-3,self.bounds[3]+3)

    def ground(self, x, z):
        radius = math.hypot(x / 128, z / 126)
        edge = max(0., (radius - .87) / .13)
        y = self.terrain(x, z, self.theme) - .14 - edge * edge * 5
        if self.theme == 0 and x < -70 and -20 < z < 25:
            # Uneven sediment shelves and two promontories interrupt the old
            # elliptical contour. Flooding still finds one connected basin.
            for cx,cz,rx,rz,height in ((-86,5,7,3,.58),(-99,-2,4,4,.48),
                                       (-104,10,6,3,.28),(-92,10,4,4,-.14),
                                       (-101,6,4,3,-.12)):
                y += height*math.exp(-((x-cx)/rx)**2-((z-cz)/rz)**2)
        return y

    def sample(self, x, z):
        best = (-1000., 0., (0., 0.))
        for a, b, dx, dz, length, width, flow in self.cells.get((math.floor(x/8), math.floor(z/8)), ()):
            f = max(0., min(1., ((x-a[0])*dx+(z-a[2])*dz)/(length*length)))
            distance = math.hypot(x-a[0]-dx*f, z-a[2]-dz*f)
            # Opposite banks erode differently; avoid a constant-width ribbon.
            bank = math.sin(x*.91+z*.47+self.theme*2)*.10
            bank += math.sin(z*1.27-x*.38)*.055
            edge = width-distance+bank
            if edge > best[0]:
                level = a[1]+(b[1]-a[1])*f
                if self.theme == 1 and level < self.SEA_LEVEL:
                    level = self.SEA_LEVEL
                    flow = (.18,.08)
                best = (edge, level, flow)
        if self.lake_bounds:
            x0,x1,z0,z1 = self.lake_bounds
            if x0-3 < x < x1+3 and z0-3 < z < z1+3:
                edge = (self.lake_level-self.ground(x,z))*9
                # Respect the connected flooded component, including narrow
                # inlets. Dry samples still carry this level so clipping at a
                # shore cannot interpolate toward a fictitious zero altitude.
                if edge>0 and (round(x),round(z)) not in self.lake_cells:
                    if not any((round(x)+a,round(z)+b) in self.lake_cells
                               for a,b in ((-1,0),(1,0),(0,-1),(0,1))):
                        return best
                if edge>0 or edge>best[0]:
                    best = (edge, self.lake_level, (0.,0.))
        return best

    def road_height(self, x, z, height):
        edge, level, _ = self.sample(x,z)
        # The ribbon has vertices on its two edges. Seat the whole cross-section
        # under even a narrow oblique stream; dipping only its centre leaves a
        # dry triangular dam between the edge vertices. Blend dry approaches.
        blend = max(0., min(1., (edge+8.)/3.))
        blend = blend*blend*(3.-2.*blend)
        # A ford is a dipped, cross-sloped section of road flush with the bed,
        # never a raised road acting as a dam between two artificial pools.
        return height + (level-.34-height)*blend
