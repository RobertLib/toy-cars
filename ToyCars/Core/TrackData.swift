//
//  TrackData.swift
//  ToyCars
//
//  Loading a Blender-generated track and fast queries over its centreline.
//

import Foundation
import simd

/// Surface types - must match the constants in tools/blender/lib/tctrack.py.
enum Surface: Int, Sendable {
    case asphalt = 0, dirt = 1, sand = 2, snow = 3, ice = 4, wood = 5

    /// Grip multiplier. Ice slides, asphalt holds.
    var grip: Float {
        switch self {
        case .asphalt: return 1.00
        case .wood:    return 0.94
        case .dirt:    return 0.82
        case .sand:    return 0.76
        case .snow:    return 0.78
        case .ice:     return 0.46
        }
    }

    /// Top-speed multiplier.
    var rolling: Float {
        switch self {
        case .asphalt: return 1.00
        case .wood:    return 0.98
        case .dirt:    return 0.93
        case .sand:    return 0.88
        case .snow:    return 0.90
        case .ice:     return 1.02
        }
    }

    var dustColor: SIMD3<Float> {
        switch self {
        case .asphalt: return [0.55, 0.55, 0.58]
        case .wood:    return [0.68, 0.52, 0.32]
        case .dirt:    return [0.72, 0.55, 0.34]
        case .sand:    return [0.94, 0.86, 0.64]
        case .snow:    return [0.96, 0.98, 1.00]
        case .ice:     return [0.85, 0.94, 1.00]
        }
    }
}

struct GridSlot: Decodable, Sendable {
    let x: Float, y: Float, z: Float, yaw: Float
}

struct PickupDef: Decodable, Sendable {
    let s: Float
    let lat: Float
}

struct RampDef: Decodable, Sendable {
    let s0: Float
    let s1: Float
    let h: Float
}

/// Exactly what `export_track_json` writes in Blender.
struct TrackFile: Decodable, Sendable {
    let id: String
    let name: String
    let subtitle: String
    let theme: String
    let laps: Int
    let difficulty: Int
    let unlock: Int
    let reward: Int
    let startS: Float
    let grid: [GridSlot]
    let cans: [PickupDef]
    let boosts: [PickupDef]
    let ramps: [RampDef]

    let sky: String
    let sky2: String
    let fog: String
    let fogDensity: Float
    let sun: [Float]
    let sunColor: String
    let sunIntensity: Float
    let ambient: String
    let ambientIntensity: Float
    let water: Float?

    let step: Float
    let length: Float
    let count: Int
    let px: [Float], py: [Float], pz: [Float]
    let tx: [Float], tz: [Float]
    let hw: [Float]
    let surf: [Int]
    let curv: [Float]
    let line: [Float]

    /// `TrackData` indexes every one of these arrays directly by a sample
    /// number derived from `count`, so a file whose arrays are shorter than
    /// it claims would crash on load rather than simply fail to open.
    var isSelfConsistent: Bool {
        guard count > 1, step > 0, length > 0 else { return false }
        // `RaceScene` reads sun[0...2] straight out, so a short vector would
        // trap there rather than fail the load here.
        guard sun.count >= 3 else { return false }
        // `RaceEngine` puts the field on `grid` and indexes it by
        // `grid.count - 1`, which on an empty grid is -1: a trap on the way
        // into the race rather than a circuit that declines to open.
        guard !grid.isEmpty else { return false }
        let n = count
        return px.count >= n && py.count >= n && pz.count >= n
            && tx.count >= n && tz.count >= n && hw.count >= n
            && surf.count >= n && curv.count >= n && line.count >= n
    }
}

/// The centreline plus the queries used by physics, AI and camera.
final class TrackData: @unchecked Sendable {
    let file: TrackFile
    let count: Int
    let step: Float
    let length: Float

    /// Margin beyond the road edge where the barrier sits.
    let wallMargin: Float = 5.0

    private(set) var positions: [SIMD3<Float>] = []
    private(set) var tangents: [SIMD2<Float>] = []
    private(set) var normals: [SIMD2<Float>] = []
    private(set) var headings: [Float] = []
    /// Road gradient along the centreline (dy/ds). Positive = climbing.
    private(set) var slopes: [Float] = []
    /// How fast that gradient changes (d2y/ds2). Negative on a crest.
    private(set) var slopeRates: [Float] = []

    /// Spatial hash for finding the nearest point quickly without a hint.
    private var cellSize: Float = 12
    private var buckets: [Int64: [Int32]] = [:]

    /// How far the circuit reaches in the ground plane. The maps fit
    /// themselves to it, and the one in the HUD redraws every frame - it used
    /// to rescan all 2,700 samples each time to work out something that
    /// cannot change.
    struct Extent {
        var minX: Float, minZ: Float, width: Float, height: Float
    }
    private(set) var extent = Extent(minX: 0, minZ: 0, width: 1, height: 1)

    init(file: TrackFile) {
        self.file = file
        self.count = file.count
        self.step = file.step
        self.length = file.length
        positions.reserveCapacity(count)
        tangents.reserveCapacity(count)
        normals.reserveCapacity(count)
        headings.reserveCapacity(count)
        for i in 0..<count {
            positions.append(SIMD3(file.px[i], file.py[i], file.pz[i]))
            let t = SIMD2(file.tx[i], file.tz[i])
            tangents.append(t)
            // left-hand normal in the XZ plane (matches Blender's convention)
            normals.append(SIMD2(t.y, -t.x))
            headings.append(atan2(t.x, t.y))
        }
        buildSlopes()
        buildBuckets()
        buildExtent()
    }

    private func buildExtent() {
        var mnx = Float.greatestFiniteMagnitude, mnz = mnx
        var mxx = -mnx, mxz = -mnx
        for p in positions {
            mnx = min(mnx, p.x); mxx = max(mxx, p.x)
            mnz = min(mnz, p.z); mxz = max(mxz, p.z)
        }
        extent = Extent(minX: mnx, minZ: mnz,
                        width: max(1, mxx - mnx), height: max(1, mxz - mnz))
    }

    /// The elevation profile differentiated once and twice. The car leans
    /// with the first and trades speed for height by it; the second says
    /// whether a crest falls away faster than gravity can follow.
    private func buildSlopes() {
        var raw = [Float](repeating: 0, count: count)
        for i in 0..<count {
            raw[i] = (positions[wrap(i + 1)].y - positions[wrap(i - 1)].y)
                / (2 * step)
        }
        // one smoothing pass - heights arrive rounded to the millimetre and
        // the second derivative would magnify that
        slopes = (0..<count).map {
            (raw[wrap($0 - 1)] + 2 * raw[$0] + raw[wrap($0 + 1)]) / 4
        }
        slopeRates = (0..<count).map {
            (slopes[wrap($0 + 1)] - slopes[wrap($0 - 1)]) / (2 * step)
        }
    }

    static func load(id: String) -> TrackData? {
        guard let url = Bundle.main.url(forResource: "track_\(id)",
                                        withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let file = try? JSONDecoder().decode(TrackFile.self, from: data),
              file.isSelfConsistent
        else { return nil }
        return TrackData(file: file)
    }

    // ------------------------------------------------------------ queries

    @inline(__always) func wrap(_ i: Int) -> Int {
        let m = i % count
        return m < 0 ? m + count : m
    }

    @inline(__always) func index(atS s: Float) -> Int {
        var v = s.truncatingRemainder(dividingBy: length)
        if v < 0 { v += length }
        return wrap(Int((v / step).rounded()))
    }

    @inline(__always) func halfWidth(_ i: Int) -> Float { file.hw[wrap(i)] }
    @inline(__always) func curvature(_ i: Int) -> Float { file.curv[wrap(i)] }
    @inline(__always) func surface(_ i: Int) -> Surface {
        Surface(rawValue: file.surf[wrap(i)]) ?? .asphalt
    }
    @inline(__always) func racingLine(_ i: Int) -> Float { file.line[wrap(i)] }
    @inline(__always) func slope(_ i: Int) -> Float { slopes[wrap(i)] }
    @inline(__always) func slopeRate(_ i: Int) -> Float { slopeRates[wrap(i)] }
    @inline(__always) func position(_ i: Int) -> SIMD3<Float> { positions[wrap(i)] }
    @inline(__always) func heading(_ i: Int) -> Float { headings[wrap(i)] }

    /// A point offset `lat` metres to the left of the centre, with optional lift.
    func point(_ i: Int, lat: Float, lift: Float = 0) -> SIMD3<Float> {
        let p = position(i)
        let n = normals[wrap(i)]
        return SIMD3(p.x + n.x * lat, p.y + lift, p.z + n.y * lat)
    }

    func pointAt(s: Float, lat: Float, lift: Float = 0) -> SIMD3<Float> {
        point(index(atS: s), lat: lat, lift: lift)
    }

    /// The result of projecting a car onto the centreline.
    struct Projection {
        var index: Int
        var lateral: Float      // positive = left of the direction of travel
        var s: Float            // distance travelled along the track
        var height: Float       // road height at that point
        var forward: Float      // projection onto the tangent (positive = right way)
    }

    /// Projection with a hint - searches only around the last index (O(1)).
    ///
    /// The window used to be ±70 samples (±49 m), which is two orders of
    /// magnitude more than a car can travel in one 1/120 s step (under half a
    /// metre, so well under one sample). Scanning it was the single most
    /// expensive thing in the simulation - eight cars times 141 samples every
    /// step - and it is what made `fastForwardToFinish` stall the main thread.
    /// A short window is also *safer* on a hairpin, where a long one can lock
    /// on to the wrong side of the bend.
    ///
    /// Being local is the whole point, so the window is deliberately *not*
    /// widened when the nearest sample sits at its edge. On that hairpin the
    /// genuinely nearest point is on the other pass, and taking it would move
    /// the car 19 m round the lap in a single frame - straight into
    /// `progress`, which only rejects jumps past a quarter of a lap. A car
    /// covers well under one sample per step, so a hint that is 16 samples
    /// out of date is not a case that arises; a car that is nowhere near its
    /// hint at all has been placed there, and the distance check catches it.
    func project(_ p: SIMD3<Float>, hint: Int, window: Int = 16) -> Projection {
        var bestI = hint
        var bestD = Float.greatestFiniteMagnitude
        for k in -window...window {
            let i = wrap(hint + k)
            let q = positions[i]
            let dx = q.x - p.x, dz = q.z - p.z
            let d = dx * dx + dz * dz
            if d < bestD { bestD = d; bestI = i }
        }
        // Further from the road than any barrier: a fresh placement, not a
        // car that has driven here, so the hint means nothing.
        if bestD > 900 { bestI = nearestGlobal(p) }
        return refine(p, around: bestI)
    }

    func projectGlobal(_ p: SIMD3<Float>) -> Projection {
        refine(p, around: nearestGlobal(p))
    }

    private func refine(_ p: SIMD3<Float>, around i: Int) -> Projection {
        let c = positions[i]
        let t = tangents[i]
        let n = normals[i]
        let dx = p.x - c.x, dz = p.z - c.z
        let along = dx * t.x + dz * t.y
        let lat = dx * n.x + dz * n.y
        // interpolate height between neighbouring samples
        let j = wrap(i + (along >= 0 ? 1 : -1))
        let f = min(1, abs(along) / step)
        let h = c.y + (positions[j].y - c.y) * f
        var s = Float(i) * step + along
        if s < 0 { s += length }
        if s >= length { s -= length }
        return Projection(index: i, lateral: lat, s: s, height: h,
                          forward: along)
    }

    // ------------------------------------------------------------ hash

    private func key(_ x: Float, _ z: Float) -> Int64 {
        let ix = Int64((x / cellSize).rounded(.down))
        let iz = Int64((z / cellSize).rounded(.down))
        return (ix &* 73_856_093) ^ (iz &* 19_349_663)
    }

    private func buildBuckets() {
        for i in 0..<count {
            let p = positions[i]
            buckets[key(p.x, p.z), default: []].append(Int32(i))
        }
    }

    /// Rings of cells outwards from `p` until no wider ring could hold
    /// anything nearer.
    ///
    /// The stopping rule used to be "found something and we are at least two
    /// rings out", which is both too eager and far too slow: it accepted a
    /// candidate that a wider ring could still beat, and because the flag was
    /// only set when a ring produced a *closer* point, a hit in ring 1 left
    /// it walking all 23 remaining rings - some seventeen thousand cell
    /// lookups - for an answer it already had. Everything outside ring `r` is
    /// at least `r * cellSize` away, so that is the bound to test against.
    private func nearestGlobal(_ p: SIMD3<Float>) -> Int {
        var best = 0
        var bestD = Float.greatestFiniteMagnitude
        var ring = 0
        while ring < 24 {
            for gx in -ring...ring {
                for gz in -ring...ring {
                    // only this ring's shell; the inside is already scanned
                    if ring > 0 && max(abs(gx), abs(gz)) != ring { continue }
                    let k = key(p.x + Float(gx) * cellSize,
                                p.z + Float(gz) * cellSize)
                    guard let list = buckets[k] else { continue }
                    for idx in list {
                        let q = positions[Int(idx)]
                        let d = (q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z)
                        if d < bestD { bestD = d; best = Int(idx) }
                    }
                }
            }
            let safe = Float(ring) * cellSize
            if bestD <= safe * safe { break }
            ring += 1
        }
        return best
    }

    // ------------------------------------------------------------ ramps

    /// Ramp height at a point on the track (0 = no ramp) and its slope.
    func rampHeight(atS s: Float) -> (height: Float, slope: Float) {
        for r in file.ramps {
            if s >= r.s0 && s <= r.s1 {
                // u^2 profile - the ramp ends at its steepest (the take-off lip)
                let len = max(0.001, r.s1 - r.s0)
                let u = (s - r.s0) / len
                return (r.h * u * u, 2 * r.h * u / len)
            }
        }
        return (0, 0)
    }

    /// Distance to the end of the ramp - used for the take-off.
    func rampEnd(atS s: Float) -> Float? {
        for r in file.ramps where s >= r.s0 && s <= r.s1 { return r.s1 }
        return nil
    }
}
