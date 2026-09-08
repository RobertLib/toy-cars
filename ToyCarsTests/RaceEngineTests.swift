//
//  RaceEngineTests.swift
//  ToyCarsTests
//
//  The race rules, driven through the same fixed timestep the game uses.
//

import Foundation
import Testing
import simd
@testable import ToyCars

@MainActor
struct RaceEngineTests {

    private func makeEngine(carID: String = "bumble",
                            trackID: String = "sunset") throws -> RaceEngine {
        let track = try #require(TrackData.load(id: trackID))
        let player = try #require(CarCatalog.shared.car(carID))
        let others = CarCatalog.shared.cars.filter { $0.id != carID }
        return RaceEngine(track: track, playerCar: player,
                          opponents: Array(others.prefix(7)), difficulty: 0)
    }

    /// Winds past the intro shot and the starting lights, so a test that is
    /// about the racing rules does not have to know how long they take.
    private func startRacing(_ e: RaceEngine) {
        var guardrail = 0
        while e.phase != .racing && guardrail < 2000 {
            e.update(dt: 1.0 / 60.0)
            guardrail += 1
        }
    }

    /// Runs the engine forward in 60 Hz frames, as the display link would.
    private func run(_ e: RaceEngine, seconds: Double,
                     each: (RaceEngine) -> Void = { _ in }) {
        let frame = 1.0 / 60.0
        var t = 0.0
        while t < seconds {
            e.update(dt: frame)
            each(e)
            t += frame
        }
    }

    @Test func theGridStartsWholeAndBehindTheLine() throws {
        let e = try makeEngine()
        #expect(e.cars.count == 8)
        #expect(e.fieldSize == 8)
        #expect(e.phase == .intro)
        // Every car on its own slot, none of them on top of another.
        for i in 0..<e.cars.count {
            for j in (i + 1)..<e.cars.count {
                let d = simd_distance(e.cars[i].pos, e.cars[j].pos)
                #expect(d > 1.0, "cars \(i) and \(j) share a grid slot")
            }
        }
        #expect(e.cars.filter(\.isPlayer).count == 1)
    }

    @Test func theCountdownRunsAndThenTheRaceStarts() throws {
        let e = try makeEngine()
        run(e, seconds: 2.0)
        #expect(e.phase == .countdown || e.phase == .intro)
        run(e, seconds: 6.0)
        #expect(e.phase == .racing)
        #expect(e.raceTime > 0)
    }

    /// The grid must not roll away while the lights are on.
    @Test func nobodyMovesDuringTheCountdown() throws {
        let e = try makeEngine()
        let before = e.cars.map(\.pos)
        run(e, seconds: 2.5)
        for (i, p) in e.cars.enumerated() {
            #expect(simd_distance(p.pos, before[i]) < 0.5,
                    "car \(i) crept forward before the start")
        }
    }

    /// The regression this suite exists for: the warning used to be pushed
    /// on every frame the tank sat under 18 %, so it reappeared every couple
    /// of seconds for the rest of the race.
    @Test func theLowFuelWarningIsShownOnce() throws {
        let e = try makeEngine()
        startRacing(e)
        #expect(e.phase == .racing)

        let warning = String(localized: "Running out of fuel!")
        e.player.fuel = e.player.def.fuelCapacity * 0.15

        var seen = Set<UUID>()
        run(e, seconds: 8.0) { engine in
            // Keep the tank in the warning band without ever emptying it, so
            // the only thing under test is how often the message is pushed.
            engine.player.fuel = engine.player.def.fuelCapacity * 0.15
            for m in engine.messages where m.text == warning {
                seen.insert(m.id)
            }
        }
        #expect(seen.count == 1,
                "the low-fuel warning was pushed \(seen.count) times")
    }

    /// Refuelling re-arms it, so a second dry spell is still announced.
    @Test func theLowFuelWarningComesBackAfterARefuel() throws {
        let e = try makeEngine()
        startRacing(e)
        let warning = String(localized: "Running out of fuel!")
        var seen = Set<UUID>()
        let collect: (RaceEngine) -> Void = { engine in
            for m in engine.messages where m.text == warning {
                seen.insert(m.id)
            }
        }

        e.player.fuel = e.player.def.fuelCapacity * 0.15
        run(e, seconds: 2.0, each: collect)
        #expect(seen.count == 1)

        e.player.fuel = e.player.def.fuelCapacity            // picked up a can
        run(e, seconds: 2.0, each: collect)
        e.player.fuel = e.player.def.fuelCapacity * 0.15     // and down again
        run(e, seconds: 2.0, each: collect)
        #expect(seen.count == 2, "the second dry spell went unannounced")
    }

    /// Standings are a total order: eight cars, eight distinct places.
    @Test func everyCarGetsItsOwnPlace() throws {
        let e = try makeEngine()
        run(e, seconds: 12.0)
        let places = e.cars.map(\.position)
        #expect(Set(places).count == e.cars.count, "duplicate places: \(places)")
        #expect(places.min() == 1)
        #expect(places.max() == e.cars.count)
    }

    /// Progress is measured from the finish line, not from wherever a car
    /// happened to be parked, so the standings rank the road rather than the
    /// starting grid.
    ///
    /// The grid is 19.6 m deep. Crediting every car with zero at the lights
    /// handed the back row that 19.6 m for nothing: it was shown leading
    /// before anyone had moved, and after forty seconds of racing a car
    /// genuinely third came out fifth. Both halves below read the truth off
    /// the road - how far each car still is from the line - and never off
    /// `progress`, which is the thing under test.
    @Test func standingsRankTheRoadAndNotTheGrid() throws {
        let e = try makeEngine()
        let L = e.track.length
        let startS = e.track.file.startS

        /// Distance from a car to the finish line, looking forwards. On the
        /// grid this is what the car still owes; once it is round and into
        /// the far half of a lap it is what is left of that lap. Comparing it
        /// between cars is only meaningful when they are in the same one of
        /// those two situations, which is why the two halves are separate.
        func toLine(_ c: CarSim) -> Float {
            (startS - c.proj.s + L).truncatingRemainder(dividingBy: L)
        }

        /// Reported order has to agree with distance-to-line for every pair
        /// that is genuinely apart. A metre or two is a real tie and the sort
        /// may break it either way.
        func checkOrder(_ group: [CarSim], _ what: String) {
            for a in group {
                for b in group where b.slot > a.slot {
                    let gap = toLine(b) - toLine(a)
                    guard abs(gap) > 2 else { continue }
                    #expect((gap > 0) == (a.position < b.position), """
                        \(what): slot \(a.slot) is \(toLine(a)) m from the \
                        line at P\(a.position), slot \(b.slot) is \
                        \(toLine(b)) m at P\(b.position)
                        """)
                }
            }
        }

        // Every car starts behind the line by exactly what it has yet to make
        // up, so pole leads the moment the lights go out.
        for c in e.cars {
            #expect(abs(c.progress + toLine(c)) < 0.5, """
                slot \(c.slot) starts at \(c.progress), \(toLine(c)) m \
                from the line
                """)
        }
        startRacing(e)
        e.update(dt: 1.0 / 60.0)
        checkOrder(e.cars, "on the grid")

        // Forty seconds in, the field is round the far side of lap one: same
        // lap, all past the line, so distance-to-line ranks them outright.
        run(e, seconds: 40.0)
        let field = e.cars.filter { $0.lap == 0 && $0.passedHalf }
        #expect(field.count >= 6, "only \(field.count) cars to compare")
        checkOrder(field, "into the lap")
    }

    /// Every canister stands *on* the surface a car drives over, which over
    /// a jump is the ramp and not the road buried beneath it.
    ///
    /// Palm Cove has one at s=262, on the lip of a 2.2 m ramp. Lifting it
    /// off the centreline alone put it a metre inside the ramp body - and a
    /// ramp is solid across 98 % of the road, so the crate was invisible
    /// while still collecting, because the pickup test is a distance in the
    /// ground plane. The AI crossed the track for forty litres of nothing.
    @Test(arguments: ["sunset", "cove", "frost"])
    func everyCanisterStandsOnTheSurfaceTheCarDrivesOn(id: String) throws {
        let e = try makeEngine(trackID: id)
        let track = e.track
        #expect(!e.pickups.isEmpty)
        for p in e.pickups {
            let ramp = track.rampHeight(atS: p.s).height
            let ground = track.pointAt(s: p.s, lat: p.lat).y + ramp
            let above = p.pos.y - ground
            // Clear of the surface, and not floating out of reach either.
            #expect(above > 0.2 && above < 1.2, """
                \(id): canister at s=\(p.s) is \(above) m above the surface \
                it stands on (ramp there is \(ramp) m)
                """)
        }
    }

    /// The AI drives: after a dozen seconds the field has left the grid and
    /// is still on the road rather than in the scenery.
    @Test func theFieldDrivesAndStaysOnTheRoad() throws {
        let e = try makeEngine()
        run(e, seconds: 14.0)
        for car in e.cars where !car.isPlayer {
            #expect(car.progress > 20,
                    "\(car.def.id) covered only \(car.progress) m")
            let limit = e.track.halfWidth(car.proj.index) + e.track.wallMargin
            #expect(abs(car.proj.lateral) <= limit + 0.5,
                    "\(car.def.id) is \(car.proj.lateral) m off, wall \(limit)")
            #expect(car.pos.y.isFinite && car.speed.isFinite)
        }
    }

    /// The clamp on the fixed-step loop must not let the simulation explode
    /// when frames are dropped.
    @Test func aStutteringFrameRateDoesNotBreakThePhysics() throws {
        let e = try makeEngine()
        for _ in 0..<200 { e.update(dt: 0.4) }   // 2.5 fps
        for car in e.cars {
            #expect(car.pos.x.isFinite && car.pos.z.isFinite)
            #expect(car.speed < car.def.topSpeed * 2)
        }
    }

    // ------------------------------------------------------- the finish

    /// The catch-up brings the field home from the worst case there is: the
    /// player retiring on the first lap of the longest circuit. Six of eight
    /// cars marked DNF is what this exists to prevent.
    @Test func theCatchUpBringsTheWholeFieldHome() throws {
        let e = try makeEngine(trackID: "frost")
        startRacing(e)
        run(e, seconds: 3.0)
        e.fastForwardToFinish()
        let stranded = e.cars.filter { !$0.finished && !$0.isPlayer }
        #expect(stranded.isEmpty,
                "\(stranded.count) opponents never finished")
        #expect(e.finalStandings.count == e.cars.count)
    }

    /// Slicing the catch-up changes only *when* its steps run, never what
    /// they compute - so a sliced run and a one-shot run have to produce the
    /// same finishing order and the same times to the last digit.
    @Test func slicingTheCatchUpChangesNothing() async throws {
        func fingerprint(_ e: RaceEngine) -> [String] {
            e.finalStandings.map {
                "\($0.name)|\($0.finished)|\(String(format: "%.6f", $0.time))"
            }
        }
        func wound(to seconds: Double) throws -> RaceEngine {
            let e = try makeEngine(trackID: "cove")
            startRacing(e)
            run(e, seconds: seconds)
            return e
        }

        let a = try wound(to: 3.0)
        a.fastForwardToFinish()

        let b = try wound(to: 3.0)
        await b.catchUpToFinish()

        #expect(fingerprint(b) == fingerprint(a),
                "sliced \(fingerprint(b))\nvs one-shot \(fingerprint(a))")
    }

    /// The catch-up must not handicap a field that is racing nobody.
    ///
    /// The rubber band is measured as a gap to the player's own progress,
    /// and once the player is out of the race - home, or stopped on the road
    /// out of fuel - that figure stands still while the field drives away
    /// from it. Every remaining car then reads as a runaway leader and is
    /// held to 95.5 % for the whole catch-up, so the times the results table
    /// prints come out slower than the same race run live.
    @Test func theCatchUpDoesNotHandicapTheFieldBehindAStoppedPlayer() throws {
        let e = try makeEngine()
        startRacing(e)
        run(e, seconds: 3.0)
        // The player retires on the spot; the field still has to come home.
        e.player.fuel = 0
        run(e, seconds: 14.0)
        #expect(e.phase == .finished, "the retirement was never called")
        for c in e.cars where !c.isPlayer {
            #expect(e.rubberBand(for: c) == 1.0,
                    "\(c.def.id) is still being handicapped after the flag")
        }
        e.fastForwardToFinish()
        for c in e.cars where !c.isPlayer {
            #expect(c.finished, "\(c.def.id) never came home")
        }
    }

    /// The placing the results screen reports comes out of the table it shows
    /// beside it, so the two cannot disagree.
    @Test func theReportedPlacingMatchesTheStandings() throws {
        let e = try makeEngine()
        startRacing(e)
        run(e, seconds: 3.0)
        e.fastForwardToFinish()
        let row = e.finalStandings.firstIndex { $0.isPlayer }
        #expect(row != nil)
        #expect(e.playerFinalPosition == (row ?? -1) + 1)
    }

    /// Out of fuel with the tank still empty: the race has to end in bounded
    /// time. The fuse used to wait for the car to come to a complete stop,
    /// and rolling resistance is gentle enough that a car which ran dry at
    /// racing speed glided for the better part of a minute first.
    @Test func runningDryEndsTheRaceWithoutAVeryLongWait() throws {
        let e = try makeEngine()
        startRacing(e)
        // empty the player's tank and hold the throttle down
        e.player.fuel = 0
        var elapsed = 0.0
        let frame = 1.0 / 60.0
        while e.phase == .racing && elapsed < 30 {
            e.setPlayerInput(DriverInput(throttle: 1, steer: 0,
                                         handbrake: false))
            e.update(dt: frame)
            elapsed += frame
        }
        #expect(e.phase == .finished,
                "still racing \(elapsed) s after the tank ran dry")
        #expect(elapsed < 20, "took \(elapsed) s to call it")
    }
}
