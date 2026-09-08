//
//  TrackDataTests.swift
//  ToyCarsTests
//
//  The shipped circuits and the queries the whole simulation is built on.
//  `project(_:hint:)` is the hot one: it searches a short window around the
//  last known index, and these tests are what pin that down - the answer must
//  stay next to the hint, and a car's distance round the lap must move
//  smoothly, on all three circuits.
//

import Testing
import simd
@testable import ToyCars

/// On the main actor like every other suite here. `TrackData` is
/// main-actor-isolated by default under this project's concurrency settings,
/// so a nonisolated suite cannot touch it at all once the Swift 6 language
/// mode is on - and the nested helper functions below could not touch it even
/// from a nonisolated test, which is what made this the one file that stood
/// between the target and that mode.
@MainActor
struct TrackDataTests {

    /// `nonisolated` because `@Test(arguments:)` evaluates this outside the
    /// actor to build the test cases, before any of them runs. It is a list
    /// of strings, so there is nothing to protect.
    nonisolated static let ids = ["sunset", "cove", "frost"]

    private func load(_ id: String) throws -> TrackData {
        try #require(TrackData.load(id: id), "track_\(id).json is missing")
    }

    // ------------------------------------------------------------ data

    @Test(arguments: ids)
    func trackFileIsSelfConsistent(id: String) throws {
        let t = try load(id)
        let f = t.file
        #expect(f.count > 0)
        for (name, n) in [("px", f.px.count), ("py", f.py.count),
                          ("pz", f.pz.count), ("tx", f.tx.count),
                          ("tz", f.tz.count), ("hw", f.hw.count),
                          ("surf", f.surf.count), ("curv", f.curv.count),
                          ("line", f.line.count)] {
            #expect(n == f.count, "\(id).\(name) has \(n), expected \(f.count)")
        }
        // The polyline is resampled at a constant step, so the two agree.
        #expect(abs(f.length - Float(f.count) * f.step) < f.step)
        #expect(f.startS >= 0 && f.startS < f.length)
        #expect(f.laps >= 1)
        // A full grid: the race always fields eight cars.
        #expect(f.grid.count >= 8, "\(id) has only \(f.grid.count) grid slots")
        for s in f.surf {
            #expect(Surface(rawValue: s) != nil,
                    "\(id) uses unknown surface \(s)")
        }
        for hw in f.hw { #expect(hw > 0) }
    }

    @Test(arguments: ids)
    func pickupsAndRampsSitOnTheTrack(id: String) throws {
        let t = try load(id)
        let f = t.file
        for c in f.cans {
            #expect(c.s >= 0 && c.s < f.length, "\(id) canister off the lap")
            #expect(abs(c.lat) <= t.halfWidth(t.index(atS: c.s)),
                    "\(id) canister outside the road")
        }
        for b in f.boosts {
            #expect(b.s >= 0 && b.s < f.length, "\(id) boost pad off the lap")
        }
        for r in f.ramps {
            #expect(r.s1 > r.s0, "\(id) ramp ends before it starts")
            #expect(r.h > 0)
        }
    }

    /// No two canisters close enough to be taken together.
    ///
    /// `gen_tracks.py` lays them one per slot around the lap and then jitters
    /// each one, and the jitter used to be a fraction of the *lap* rather than
    /// of the slot: ±0.03 L is ±57 m on Frostpeak against a slot only 68 m
    /// wide, so two neighbours could swap ends and land on top of each other.
    /// They did - two crates 1.4 m apart, drawn as one object and collected
    /// as one, handing over 80 litres from a single spot on a track where the
    /// thirstiest car burns 1.6 a second. `RaceEngine` collects anything
    /// inside 2.6 m, so twice that is the floor; the generator now keeps them
    /// an order of magnitude clear of it.
    @Test(arguments: ids)
    func canistersAreNeverCloseEnoughToTakeTogether(id: String) throws {
        let t = try load(id)
        // The pickup radius in `RaceEngine.updateRace`. Two canisters closer
        // than twice it share ground a single car can stand on.
        let reach: Float = 2.6 * 2
        let pts = t.file.cans.map { t.pointAt(s: $0.s, lat: $0.lat) }
        for i in pts.indices {
            for j in (i + 1)..<pts.count {
                let d = SIMD2(pts[i].x - pts[j].x, pts[i].z - pts[j].z)
                let gap = simd_length(d)
                #expect(gap > reach, """
                    \(id) has canisters \(gap) m apart (s = \
                    \(t.file.cans[i].s) and \(t.file.cans[j].s)); \
                    anything inside \(reach) m is one pickup, not two
                    """)
            }
        }
    }

    // ------------------------------------------------------------ queries

    @Test(arguments: ids)
    func indexWrapsInBothDirections(id: String) throws {
        let t = try load(id)
        #expect(t.wrap(-1) == t.count - 1)
        #expect(t.wrap(t.count) == 0)
        #expect(t.index(atS: 0) == 0)
        #expect(t.index(atS: t.length) == 0)
        #expect(t.index(atS: -t.step) == t.count - 1)
    }

    /// The contract the whole simulation rests on: as a car moves a few
    /// centimetres, its distance round the lap moves a few centimetres too.
    /// `CarSim` accumulates `progress` from that difference frame to frame and
    /// only rejects jumps past a quarter of the lap, so anything smaller is
    /// credited to the standings as if the car had driven it.
    ///
    /// Sampled across the road, the edges included, because that is where a
    /// car comes nearest the far side of a hairpin: Sunset has a corner whose
    /// two passes are about 19 m apart, and a search window wide enough to see
    /// both jumps to the wrong one. That is the failure this guards.
    ///
    /// The tolerance is loose on purpose. At the apex of Sunset's tightest
    /// corner - 7.1 m radius - the inside lane passes within 1.7 m of the
    /// centre of curvature, where a dozen centreline samples are all but
    /// equidistant and the nearest one flickers by ±2 m. That is the geometry,
    /// not the search: an exhaustive scan of every sample on the track
    /// flickers in exactly the same place. A wrong branch is an order of
    /// magnitude bigger and still caught.
    @Test(arguments: ids)
    func projectionStaysContinuousAcrossTheRoad(id: String) throws {
        let t = try load(id)
        for lane in [Float(0), 0.9, -0.9] {
            func roadPoint(_ i: Int) -> SIMD3<Float> {
                t.point(i, lat: lane * t.halfWidth(i))
            }
            var hint = t.projectGlobal(roadPoint(0)).index
            var lastS = t.project(roadPoint(0), hint: hint).s
            for i in 1...t.count {
                let k = t.wrap(i)
                let proj = t.project(roadPoint(k), hint: hint)
                let ds = forwardDistance(from: lastS, to: proj.s,
                                         length: t.length)
                #expect(abs(ds) <= 6,
                        "\(id) lane \(lane) @\(k): s moved \(ds) m")
                hint = proj.index
                lastS = proj.s
            }
        }
    }

    /// The search is local, and that is the point of it.
    ///
    /// A car carries its own index forward from the previous frame, and it
    /// moves less than one sample per step, so the answer is always a few
    /// samples from the hint. It must never be somewhere else - and on a
    /// hairpin "somewhere else" is a real option: at Sunset's tightest corner
    /// a car against the barrier on the way in is physically *nearer* the
    /// centreline of the way out, about 27 samples away. The old ±70 sample
    /// window could see that far and took it, which teleported the car's
    /// distance round the lap by some 19 m - straight into `progress`, which
    /// only rejects jumps past a quarter of a lap. A window shorter than the
    /// gap cannot make that mistake.
    @Test(arguments: ids)
    func projectionStaysNearAGoodHint(id: String) throws {
        let t = try load(id)
        let window = 16
        for i in 0..<t.count {
            let hw = t.halfWidth(i)
            for lat in [Float(0), hw, -hw,
                        hw + t.wallMargin, -(hw + t.wallMargin)] {
                let proj = t.project(t.point(i, lat: lat), hint: i)
                let raw = abs(proj.index - i)
                let off = min(raw, t.count - raw)
                #expect(off <= window,
                        "\(id) @\(i) lat \(lat): landed on \(proj.index)")
            }
        }
    }

    /// The road is an offset curve of the centreline, so a corner tighter than
    /// the road is wide would fold the tarmac back through itself - and the
    /// projection of anything standing on that fold is ambiguous. Sunset's
    /// tightest corner is 6.8 m, against a half width of 6.1 m, so this holds
    /// today with little room to spare; a tighter corner in a future track
    /// would break the simulation, not just the mesh.
    @Test(arguments: ids)
    func theRoadSurfaceNeverFoldsBackOnItself(id: String) throws {
        let t = try load(id)
        for i in 0..<t.count {
            let c = abs(t.curvature(i))
            guard c > 1e-9 else { continue }
            #expect(t.halfWidth(i) < 1 / c,
                    "\(id) @\(i): \(t.halfWidth(i)) m wide, \(1 / c) m radius")
        }
    }

    /// Stepping the hint forwards the way a car does keeps the projection
    /// locked on, and the lateral offset comes back out as it went in.
    @Test(arguments: ids)
    func projectionTracksACarAlongTheLap(id: String) throws {
        let t = try load(id)
        var hint = t.projectGlobal(t.point(0, lat: 0)).index
        for i in 0..<t.count {
            let lat: Float = 3.0
            let p = t.point(i, lat: lat)
            let proj = t.project(p, hint: hint)
            #expect(abs(proj.lateral - lat) < 0.6,
                    "\(id) @\(i): lateral \(proj.lateral), expected \(lat)")
            hint = proj.index
        }
    }

    /// A hint from the far side of the circuit - a respawn, or the first
    /// frame - must not leave the car projected onto the wrong corner.
    @Test(arguments: ids)
    func projectionRecoversFromAHopelessHint(id: String) throws {
        let t = try load(id)
        for i in stride(from: 0, to: t.count, by: 37) {
            let p = t.point(i, lat: 0)
            let wrong = t.wrap(i + t.count / 2)
            let proj = t.project(p, hint: wrong)
            let global = t.projectGlobal(p)
            #expect(abs(proj.s - global.s) < t.step * 1.5,
                    "\(id) @\(i): did not recover from hint \(wrong)")
        }
    }

    @Test(arguments: ids)
    func rampHeightIsZeroOffTheRamps(id: String) throws {
        let t = try load(id)
        for r in t.file.ramps {
            #expect(t.rampHeight(atS: r.s0).height == 0)     // starts flat
            #expect(t.rampHeight(atS: r.s1).height > 0)      // ends at the lip
            #expect(t.rampEnd(atS: (r.s0 + r.s1) / 2) == r.s1)
        }
        // A point well clear of every ramp is flat.
        let clear = t.file.ramps.allSatisfy { $0.s0 > 1.0 }
        if clear { #expect(t.rampHeight(atS: 0.5).height == 0) }
    }

    // ------------------------------------------------------------ helpers

    @Test func forwardDistanceTakesTheShortWayRound() {
        let len: Float = 100
        #expect(forwardDistance(from: 10, to: 20, length: len) == 10)
        #expect(forwardDistance(from: 20, to: 10, length: len) == -10)
        // Across the start line, both ways.
        #expect(forwardDistance(from: 95, to: 5, length: len) == 10)
        #expect(forwardDistance(from: 5, to: 95, length: len) == -10)
    }

    @Test func everySurfaceHasSaneCoefficients() {
        for raw in 0...5 {
            // Not `#require`: the enum covers 0...5 exactly, so the compiler
            // knows this cannot be nil and says so.
            let s = Surface(rawValue: raw) ?? .asphalt
            #expect(s.grip > 0 && s.grip <= 1)
            #expect(s.rolling > 0 && s.rolling <= 1.1)
        }
        // Ice is the slippery one, asphalt the grippy one.
        #expect(Surface.ice.grip < Surface.snow.grip)
        #expect(Surface.asphalt.grip == 1.0)
    }
}
